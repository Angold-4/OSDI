/* This file contains the terminal driver, both for the IBM console and regular
 * ASCII terminals.  It handles only the device-independent part of a TTY, the
 * device dependent parts are in console.c, rs232.c, etc.  This file contains
 * two main entry points, tty_task() and tty_wakeup(), and several minor entry
 * points for use by the device-dependent code.
 *
 * The device-independent part accepts "keyboard" input from the device-
 * dependent part, performs input processing (special key interpretation),
 * and sends the input to a process reading from the TTY.  Output to a TTY
 * is sent to the device-dependent code for output processing and "screen"
 * display.  Input processing is done by the device by calling 'in_process'
 * on the input characters, output processing may be done by the device itself
 * or by calling 'out_process'.  The TTY takes care of input queuing, the
 * device does the output queuing.  If a device receives an external signal,
 * like an interrupt, then it causes tty_wakeup() to be run by the CLOCK task
 * to, you guessed it, wake up the TTY to check if input or output can
 * continue.
 *
 * The valid messages and their parameters are:
 *
 *   notify from HARDWARE:       output has been completed or input has arrived
 *   notify from SYSTEM  :      e.g., MINIX wants to shutdown; run code to 
 *   				cleanly stop
 *   DEV_READ:       a process wants to read from a terminal
 *   DEV_WRITE:      a process wants to write on a terminal
 *   DEV_IOCTL:      a process wants to change a terminal's parameters
 *   DEV_OPEN:       a tty line has been opened
 *   DEV_CLOSE:      a tty line has been closed
 *   DEV_SELECT:     start select notification request
 *   DEV_STATUS:     FS wants to know status for SELECT or REVIVE
 *   CANCEL:         terminate a previous incomplete system call immediately
 *
 *    m_type      TTY_LINE   IO_ENDPT    COUNT   TTY_SPEKS  ADDRESS
 * -----------------------------------------------------------------
 * | HARD_INT    |         |         |         |         |         |
 * |-------------+---------+---------+---------+---------+---------|
 * | SYS_SIG     | sig set |         |         |         |         |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_READ    |minor dev| proc nr |  count  |         | buf ptr |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_WRITE   |minor dev| proc nr |  count  |         | buf ptr |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_IOCTL   |minor dev| proc nr |func code|erase etc|         |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_OPEN    |minor dev| proc nr | O_NOCTTY|         |         |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_CLOSE   |minor dev| proc nr |         |         |         |
 * |-------------+---------+---------+---------+---------+---------|
 * | DEV_STATUS  |         |         |         |         |         |
 * |-------------+---------+---------+---------+---------+---------|
 * | CANCEL      |minor dev| proc nr |         |         |         |
 * -----------------------------------------------------------------
 *
 * Changes:
 *   Jan 20, 2004   moved TTY driver to user-space  (Jorrit N. Herder)
 *   Sep 20, 2004   local timer management/ sync alarms  (Jorrit N. Herder)
 *   Jul 13, 2004   support for function key observers  (Jorrit N. Herder)  
 */

#include "../drivers.h"
#include <termios.h>
#include <sys/ioc_tty.h>
#include <signal.h>
#include <minix/callnr.h>
#include <minix/sys_config.h>
#include <minix/tty.h>
#include <minix/keymap.h>
#include <minix/endpoint.h>
#include "tty.h"

#include <sys/time.h>
#include <sys/select.h>

extern int irq_hook_id;

unsigned long kbd_irq_set = 0;
unsigned long rs_irq_set = 0;

/* Address of a tty structure. */
#define tty_addr(line)	(&tty_table[line])

/* Macros for magic tty types. */
#define isconsole(tp)	((tp) < tty_addr(NR_CONS))
#define ispty(tp)	((tp) >= tty_addr(NR_CONS+NR_RS_LINES))

/* Macros for magic tty structure pointers. */
#define FIRST_TTY	tty_addr(0)
#define END_TTY		tty_addr(sizeof(tty_table) / sizeof(tty_table[0]))

/* A device exists if at least its 'devread' function is defined. */
#define tty_active(tp)	((tp)->tty_devread != NULL)

/* RS232 lines or pseudo terminals can be completely configured out. */
#if NR_RS_LINES == 0
#define rs_init(tp)	((void) 0)
#endif

#if NR_PTYS == 0
#define pty_init(tp)	((void) 0)
#define do_pty(tp, mp)	((void) 0)
#endif

struct kmessages kmess;

FORWARD _PROTOTYPE( void tty_timed_out, (timer_t *tp)			);
FORWARD _PROTOTYPE( void expire_timers, (void)				);
FORWARD _PROTOTYPE( void settimer, (tty_t *tty_ptr, int enable)		);
FORWARD _PROTOTYPE( void do_cancel, (tty_t *tp, message *m_ptr)		);
FORWARD _PROTOTYPE( void do_ioctl, (tty_t *tp, message *m_ptr, int s)	);
FORWARD _PROTOTYPE( void do_open, (tty_t *tp, message *m_ptr)		);
FORWARD _PROTOTYPE( void do_close, (tty_t *tp, message *m_ptr)		);
FORWARD _PROTOTYPE( void do_read, (tty_t *tp, message *m_ptr, int s)	);
FORWARD _PROTOTYPE( void do_write, (tty_t *tp, message *m_ptr, int s)	);
FORWARD _PROTOTYPE( void do_select, (tty_t *tp, message *m_ptr)		);
FORWARD _PROTOTYPE( void do_status, (message *m_ptr)			);
FORWARD _PROTOTYPE( void in_transfer, (tty_t *tp)			);
FORWARD _PROTOTYPE( int tty_echo, (tty_t *tp, int ch)			);
FORWARD _PROTOTYPE( void rawecho, (tty_t *tp, int ch)			);
FORWARD _PROTOTYPE( int back_over, (tty_t *tp)				);
FORWARD _PROTOTYPE( int back_over_word, (tty_t *tp)				);
FORWARD _PROTOTYPE( void reprint, (tty_t *tp)				);
FORWARD _PROTOTYPE( void dev_ioctl, (tty_t *tp)				);
FORWARD _PROTOTYPE( void setattr, (tty_t *tp)				);
FORWARD _PROTOTYPE( void tty_icancel, (tty_t *tp)			);
FORWARD _PROTOTYPE( void tty_init, (void)				);

/* Default attributes. */
PRIVATE struct termios termios_defaults = {
  TINPUT_DEF, TOUTPUT_DEF, TCTRL_DEF, TLOCAL_DEF, TSPEED_DEF, TSPEED_DEF,
  {
	TEOF_DEF, TEOL_DEF, TERASE_DEF, TINTR_DEF, TKILL_DEF, TMIN_DEF,
	TQUIT_DEF, TTIME_DEF, TSUSP_DEF, TSTART_DEF, TSTOP_DEF,
	TREPRINT_DEF, TLNEXT_DEF, TDISCARD_DEF, TERASEWORD_DEF,
  },
};
PRIVATE struct winsize winsize_defaults;	/* = all zeroes */

/* Global variables for the TTY task (declared extern in tty.h). */
PUBLIC tty_t tty_table[NR_CONS+NR_RS_LINES+NR_PTYS];
PUBLIC int ccurrent;			/* currently active console */
PUBLIC timer_t *tty_timers;		/* queue of TTY timers */
PUBLIC clock_t tty_next_timeout;	/* time that the next alarm is due */
PUBLIC struct machine machine;		/* kernel environment variables */
PUBLIC u32_t system_hz;

extern PUBLIC unsigned info_location;
extern PUBLIC phys_bytes vid_size;     /* 0x2000 for color or 0x0800 for mono */
extern PUBLIC phys_bytes vid_base;


/*===========================================================================*
 *				tty_task				     *
 *===========================================================================*/
PUBLIC int main(void)
{
/* Main routine of the terminal task. */

  message tty_mess;		/* buffer for all incoming messages */
  unsigned line;
  int r, s;
  register tty_t *tp;

  /* Get kernel environment (protected_mode, pc_at and ega are needed). */ 
  if (OK != (s=sys_getmachine(&machine))) {
    panic("TTY","Couldn't obtain kernel environment.", s);
  }

  /* Initialize the TTY driver. */
  tty_init();

  /* Final one-time keyboard initialization. */
  kb_init_once();

  while (TRUE) {
	int adflag = 0;

	/* Check for and handle any events on any of the ttys. */
	for (tp = FIRST_TTY; tp < END_TTY; tp++) {
		if (tp->tty_events) handle_events(tp);
	}

	/* Get a request message. */
	r= receive(ANY, &tty_mess);
	if (r != 0)
		panic("TTY", "receive failed with %d", r);

	/* First handle all kernel notification types that the TTY supports. 
	 *  - An alarm went off, expire all timers and handle the events. 
	 *  - A hardware interrupt also is an invitation to check for events. 
	 *  - A new kernel message is available for printing.
	 *  - Reset the console on system shutdown. 
	 * Then see if this message is different from a normal device driver
	 * request and should be handled separately. These extra functions
	 * do not operate on a device, in constrast to the driver requests. 
	 */

	if (is_notify(tty_mess.m_type)) {
		switch (_ENDPOINT_P(tty_mess.m_source)) {
			case CLOCK:
				/* run watchdogs of expired timers */
				expire_timers();
				break;
			case RS_PROC_NR:
				notify(tty_mess.m_source);
				break;
			case HARDWARE: 
				/* hardware interrupt notification */
				
				/* fetch chars from keyboard */
				if (tty_mess.NOTIFY_ARG & kbd_irq_set)
					kbd_interrupt(&tty_mess);
#if NR_RS_LINES > 0
				/* serial I/O */
				if (tty_mess.NOTIFY_ARG & rs_irq_set)
					rs_interrupt(&tty_mess);
#endif
				/* run watchdogs of expired timers */
				expire_timers();
				break;
			case PM_PROC_NR:
				/* switch to primary console */
				cons_stop();
				break;
			case SYSTEM:
				/* system signal */
				if (sigismember((sigset_t*)&tty_mess.NOTIFY_ARG,
								SIGKMESS))
					do_new_kmess(&tty_mess);
				break;
			default:
				/* do nothing */
				break;
		}

		/* done, get new message */
		continue;
	}

	switch (tty_mess.m_type) { 
	case DIAGNOSTICS_OLD: 		/* a server wants to print some */
#if 0
		if (tty_mess.m_source != LOG_PROC_NR)
		{
			printf("[%d ", tty_mess.m_source);
		}
#endif
		do_diagnostics(&tty_mess, 0);
		continue;
	case DIAGNOSTICS_S_OLD: 
	case ASYN_DIAGNOSTICS_OLD: 
		do_diagnostics(&tty_mess, 1);
		continue;
	case GET_KMESS:
		do_get_kmess(&tty_mess);
		continue;
	case GET_KMESS_S:
		do_get_kmess_s(&tty_mess);
		continue;
	case FKEY_CONTROL:		/* (un)register a fkey observer */
		do_fkey_ctl(&tty_mess);
		continue;
	default:			/* should be a driver request */
		;			/* do nothing; end switch */
	}

	/* Only device requests should get to this point. All requests, 
	 * except DEV_STATUS, have a minor device number. Check this
	 * exception and get the minor device number otherwise.
	 */
	if (tty_mess.m_type == DEV_STATUS) {
		do_status(&tty_mess);
		continue;
	}
	line = tty_mess.TTY_LINE;
	if (line == KBD_MINOR) {
		do_kbd(&tty_mess);
		continue;
	} else if (line == KBDAUX_MINOR) {
		do_kbdaux(&tty_mess);
		continue;
	} else if (line == VIDEO_MINOR) {
		do_video(&tty_mess);
		continue;
	} else if ((line - CONS_MINOR) < NR_CONS) {
		tp = tty_addr(line - CONS_MINOR);
	} else if (line == LOG_MINOR) {
		tp = tty_addr(0);
	} else if ((line - RS232_MINOR) < NR_RS_LINES) {
		tp = tty_addr(line - RS232_MINOR + NR_CONS);
	} else if ((line - TTYPX_MINOR) < NR_PTYS) {
		tp = tty_addr(line - TTYPX_MINOR + NR_CONS + NR_RS_LINES);
	} else if ((line - PTYPX_MINOR) < NR_PTYS) {
		tp = tty_addr(line - PTYPX_MINOR + NR_CONS + NR_RS_LINES);
		if (tty_mess.m_type != DEV_IOCTL_S) {
			do_pty(tp, &tty_mess);
			continue;
		}
	} else {
		tp = NULL;
	}

	/* If the device doesn't exist or is not configured return ENXIO. */
	if (tp == NULL || ! tty_active(tp)) {
		printf("Warning, TTY got illegal request %d from %d\n",
			tty_mess.m_type, tty_mess.m_source);
		if (tty_mess.m_source != LOG_PROC_NR)
		{
			tty_reply(TASK_REPLY, tty_mess.m_source,
						tty_mess.IO_ENDPT, ENXIO);
		}
		continue;
	}

	/* Execute the requested device driver function. */
	switch (tty_mess.m_type) {
	    case DEV_READ_S:	 do_read(tp, &tty_mess, 1);	  break;
	    case DEV_WRITE_S:	 do_write(tp, &tty_mess, 1);	  break;
	    case DEV_IOCTL_S:	 do_ioctl(tp, &tty_mess, 1);	  break;
	    case DEV_OPEN:	 do_open(tp, &tty_mess);	  break;
	    case DEV_CLOSE:	 do_close(tp, &tty_mess);	  break;
	    case DEV_SELECT:	 do_select(tp, &tty_mess);	  break;
	    case CANCEL:	 do_cancel(tp, &tty_mess);	  break;
	    default:		
		printf("Warning, TTY got unexpected request %d from %d\n",
			tty_mess.m_type, tty_mess.m_source);
	    tty_reply(TASK_REPLY, tty_mess.m_source,
						tty_mess.IO_ENDPT, EINVAL);
	}
  }

  return 0;
}

/*===========================================================================*
 *				do_status				     *
 *===========================================================================*/
PRIVATE void do_status(m_ptr)
message *m_ptr;
{
  register struct tty *tp;
  int event_found;
  int status;
  int ops;
  
  /* Check for select or revive events on any of the ttys. If we found an, 
   * event return a single status message for it. The FS will make another 
   * call to see if there is more.
   */
  event_found = 0;
  for (tp = FIRST_TTY; tp < END_TTY; tp++) {
	if ((ops = select_try(tp, tp->tty_select_ops)) && 
			tp->tty_select_proc == m_ptr->m_source) {

		/* I/O for a selected minor device is ready. */
		m_ptr->m_type = DEV_IO_READY;
		m_ptr->DEV_MINOR = tp->tty_minor;
		m_ptr->DEV_SEL_OPS = ops;

		tp->tty_select_ops &= ~ops;	/* unmark select event */
  		event_found = 1;
		break;
	}
	else if (tp->tty_inrevived && tp->tty_incaller == m_ptr->m_source) {
		
		/* Suspended request finished. Send a REVIVE. */
		m_ptr->m_type = DEV_REVIVE;
  		m_ptr->REP_ENDPT = tp->tty_inproc;
  		m_ptr->REP_IO_GRANT = tp->tty_in_vir_g;
  		m_ptr->REP_STATUS = tp->tty_incum;

		tp->tty_inleft = tp->tty_incum = 0;
		tp->tty_inrevived = 0;		/* unmark revive event */
  		event_found = 1;
  		break;
	}
	else if (tp->tty_outrevived && tp->tty_outcaller == m_ptr->m_source) {
		
		/* Suspended request finished. Send a REVIVE. */
		m_ptr->m_type = DEV_REVIVE;
  		m_ptr->REP_ENDPT = tp->tty_outproc;
  		m_ptr->REP_IO_GRANT = tp->tty_out_vir_g;
  		m_ptr->REP_STATUS = tp->tty_outcum;

		tp->tty_outcum = 0;
		tp->tty_outrevived = 0;		/* unmark revive event */
  		event_found = 1;
  		break;
	}
	else if (tp->tty_iorevived && tp->tty_iocaller == m_ptr->m_source) {
		/* Suspended request finished. Send a REVIVE. */
		m_ptr->m_type = DEV_REVIVE;
  		m_ptr->REP_ENDPT = tp->tty_ioproc;
  		m_ptr->REP_IO_GRANT = tp->tty_iovir_g;
  		m_ptr->REP_STATUS = tp->tty_iostatus;
		tp->tty_iorevived = 0;		/* unmark revive event */
  		event_found = 1;
  		break;
	}
  }

#if NR_PTYS > 0
  if (!event_found)
  	event_found = pty_status(m_ptr);
#endif
  if (!event_found)
	event_found= kbd_status(m_ptr);

  if (! event_found) {
	/* No events of interest were found. Return an empty message. */
  	m_ptr->m_type = DEV_NO_STATUS;
  }

  /* Almost done. Send back the reply message to the caller. */
  status = sendnb(m_ptr->m_source, m_ptr);
  if (status != OK) {
	printf("tty`do_status: send to %d failed: %d\n",
		m_ptr->m_source, status);
  }
}

/*===========================================================================*
 *				do_read					     *
 *===========================================================================*/
PRIVATE void do_read(tp, m_ptr, safe)
register tty_t *tp;		/* pointer to tty struct */
register message *m_ptr;	/* pointer to message sent to the task */
int safe;			/* use safecopies? */
{
/* A process wants to read from a terminal. */
  int r;

  /* Check if there is already a process hanging in a read, check if the
   * parameters are correct, do I/O.
   */
  if (tp->tty_inleft > 0) {
	r = EIO;
  } else
  if (m_ptr->COUNT <= 0) {
	r = EINVAL;
  } else {
	/* Copy information from the message to the tty struct. */
	tp->tty_inrepcode = TASK_REPLY;
	tp->tty_incaller = m_ptr->m_source;
	tp->tty_inproc = m_ptr->IO_ENDPT;
	tp->tty_in_vir_g = (vir_bytes) m_ptr->ADDRESS;
	tp->tty_in_vir_offset = 0;
	tp->tty_in_safe = safe;
	tp->tty_inleft = m_ptr->COUNT;

	if (!(tp->tty_termios.c_lflag & ICANON)
					&& tp->tty_termios.c_cc[VTIME] > 0) {
		if (tp->tty_termios.c_cc[VMIN] == 0) {
			/* MIN & TIME specify a read timer that finishes the
			 * read in TIME/10 seconds if no bytes are available.
			 */
			settimer(tp, TRUE);
			tp->tty_min = 1;
		} else {
			/* MIN & TIME specify an inter-byte timer that may
			 * have to be cancelled if there are no bytes yet.
			 */
			if (tp->tty_eotct == 0) {
				settimer(tp, FALSE);
				tp->tty_min = tp->tty_termios.c_cc[VMIN];
			}
		}
	}

	/* Anything waiting in the input buffer? Clear it out... */
	in_transfer(tp);
	/* ...then go back for more. */
	handle_events(tp);
	if (tp->tty_inleft == 0)  {
  		if (tp->tty_select_ops)
  			select_retry(tp);
		return;			/* already done */
	}

	/* There were no bytes in the input queue available, so suspend
	 * the caller.
	 */
	r = SUSPEND;				/* suspend the caller */
	tp->tty_inrepcode = TTY_REVIVE;
  }
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, r);
  if (tp->tty_select_ops)
  	select_retry(tp);
}

/*===========================================================================*
 *				do_write				     *
 *===========================================================================*/
PRIVATE void do_write(tp, m_ptr, safe)
register tty_t *tp;
register message *m_ptr;	/* pointer to message sent to the task */
int safe;
{
/* A process wants to write on a terminal. */
  int r;

  /* Check if there is already a process hanging in a write, check if the
   * parameters are correct, do I/O.
   */
  if (tp->tty_outleft > 0) {
	r = EIO;
  } else
  if (m_ptr->COUNT <= 0) {
	r = EINVAL;
  } else {
	/* Copy message parameters to the tty structure. */
	tp->tty_outrepcode = TASK_REPLY;
	tp->tty_outcaller = m_ptr->m_source;
	tp->tty_outproc = m_ptr->IO_ENDPT;
	tp->tty_out_vir_g = (vir_bytes) m_ptr->ADDRESS;
	tp->tty_out_vir_offset = 0;
	tp->tty_out_safe = safe;
	tp->tty_outleft = m_ptr->COUNT;

	/* Try to write. */
	handle_events(tp);
	if (tp->tty_outleft == 0) 
		return;	/* already done */

	/* None or not all the bytes could be written, so suspend the
	 * caller.
	 */
	r = SUSPEND;				/* suspend the caller */
	tp->tty_outrepcode = TTY_REVIVE;
  }
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, r);
}

/*===========================================================================*
 *				do_ioctl				     *
 *===========================================================================*/
PRIVATE void do_ioctl(tp, m_ptr, safe)
register tty_t *tp;
message *m_ptr;			/* pointer to message sent to task */
int safe;
{
/* Perform an IOCTL on this terminal. Posix termios calls are handled
 * by the IOCTL system call
 */

  int r;
  union {
	int i;
  } param;
  size_t size;

  /* Size of the ioctl parameter. */
  switch (m_ptr->TTY_REQUEST) {
    case TCGETS:        /* Posix tcgetattr function */
    case TCSETS:        /* Posix tcsetattr function, TCSANOW option */ 
    case TCSETSW:       /* Posix tcsetattr function, TCSADRAIN option */
    case TCSETSF:	/* Posix tcsetattr function, TCSAFLUSH option */
        size = sizeof(struct termios);
        break;

    case TCSBRK:        /* Posix tcsendbreak function */
    case TCFLOW:        /* Posix tcflow function */
    case TCFLSH:        /* Posix tcflush function */
    case TIOCGPGRP:     /* Posix tcgetpgrp function */
    case TIOCSPGRP:	/* Posix tcsetpgrp function */
        size = sizeof(int);
        break;

    case TIOCGWINSZ:    /* get window size (not Posix) */
    case TIOCSWINSZ:	/* set window size (not Posix) */
        size = sizeof(struct winsize);
        break;

#if (MACHINE == IBM_PC)
    case KIOCSMAP:	/* load keymap (Minix extension) */
        size = sizeof(keymap_t);
        break;

    case TIOCSFON:	/* load font (Minix extension) */
        size = sizeof(u8_t [8192]);
        break;

#endif
    case TCDRAIN:	/* Posix tcdrain function -- no parameter */
    default:		size = 0;
  }

  r = OK;
  switch (m_ptr->TTY_REQUEST) {
    case TCGETS:
	/* Get the termios attributes. */
	if(safe) {
	    r = sys_safecopyto(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &tp->tty_termios, (vir_bytes) size, D);
	} else {
	    r = sys_vircopy(SELF, D, (vir_bytes) &tp->tty_termios,
		m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS, 
		(vir_bytes) size);
	}
	break;

    case TCSETSW:
    case TCSETSF:
    case TCDRAIN:
	if (tp->tty_outleft > 0) {
		/* Wait for all ongoing output processing to finish. */
		tp->tty_iocaller = m_ptr->m_source;
		tp->tty_ioproc = m_ptr->IO_ENDPT;
		tp->tty_ioreq = m_ptr->REQUEST;
		tp->tty_iovir_g = (vir_bytes) m_ptr->ADDRESS;
		tp->tty_io_safe = safe;
		r = SUSPEND;
		break;
	}
	if (m_ptr->TTY_REQUEST == TCDRAIN) break;
	if (m_ptr->TTY_REQUEST == TCSETSF) tty_icancel(tp);
	/*FALL THROUGH*/
    case TCSETS:
	/* Set the termios attributes. */
	if(safe) {
	    r = sys_safecopyfrom(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &tp->tty_termios, (vir_bytes) size, D);
	} else {
	    r = sys_vircopy( m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS,
	        SELF, D, (vir_bytes) &tp->tty_termios, (vir_bytes) size);
	}
	if (r != OK) break;
	setattr(tp);
	break;

    case TCFLSH:
	if(safe) {
	   r = sys_safecopyfrom(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &param.i, (vir_bytes) size, D);
	} else {
	   r = sys_vircopy(m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS,
		SELF, D, (vir_bytes) &param.i, (vir_bytes) size);
	}
	if (r != OK) break;
	switch (param.i) {
	    case TCIFLUSH:	tty_icancel(tp);		 	    break;
	    case TCOFLUSH:	(*tp->tty_ocancel)(tp, 0);		    break;
	    case TCIOFLUSH:	tty_icancel(tp); (*tp->tty_ocancel)(tp, 0); break;
	    default:		r = EINVAL;
	}
	break;

    case TCFLOW:
	if(safe) {
	   r = sys_safecopyfrom(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &param.i, (vir_bytes) size, D);
	} else {
	    r = sys_vircopy( m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS,
		SELF, D, (vir_bytes) &param.i, (vir_bytes) size);
	}
	if (r != OK) break;
	switch (param.i) {
	    case TCOOFF:
	    case TCOON:
		tp->tty_inhibited = (param.i == TCOOFF);
		tp->tty_events = 1;
		break;
	    case TCIOFF:
		(*tp->tty_echo)(tp, tp->tty_termios.c_cc[VSTOP]);
		break;
	    case TCION:
		(*tp->tty_echo)(tp, tp->tty_termios.c_cc[VSTART]);
		break;
	    default:
		r = EINVAL;
	}
	break;

    case TCSBRK:
	if (tp->tty_break != NULL) (*tp->tty_break)(tp,0);
	break;

    case TIOCGWINSZ:
	if(safe) {
	   r = sys_safecopyto(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &tp->tty_winsize, (vir_bytes) size, D);
	} else {
	   r = sys_vircopy(SELF, D, (vir_bytes) &tp->tty_winsize,
		m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS, 
		(vir_bytes) size);
	}
	break;

    case TIOCSWINSZ:
	if(safe) {
	   r = sys_safecopyfrom(m_ptr->IO_ENDPT, (vir_bytes) m_ptr->ADDRESS, 0,
		(vir_bytes) &tp->tty_winsize, (vir_bytes) size, D);
	} else {
	   r = sys_vircopy( m_ptr->IO_ENDPT, D, (vir_bytes) m_ptr->ADDRESS,
		SELF, D, (vir_bytes) &tp->tty_winsize, (vir_bytes) size);
	}
	sigchar(tp, SIGWINCH, 0);
	break;

#if (MACHINE == IBM_PC)
    case KIOCSMAP:
	/* Load a new keymap (only /dev/console). */
	if (isconsole(tp)) r = kbd_loadmap(m_ptr, safe);
	break;

    case TIOCSFON_OLD:
	printf("TTY: old TIOCSFON ignored.\n");
	break;
    case TIOCSFON:
	/* Load a font into an EGA or VGA card (hs@hck.hr) */
	if (isconsole(tp)) r = con_loadfont(m_ptr);
	break;
#endif

#if (MACHINE == ATARI)
    case VDU_LOADFONT:
	r = vdu_loadfont(m_ptr);
	break;
#endif

/* These Posix functions are allowed to fail if _POSIX_JOB_CONTROL is 
 * not defined.
 */
    case TIOCGPGRP:     
    case TIOCSPGRP:	
    default:
	r = ENOTTY;
  }

  /* Send the reply. */
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, r);
}

/*===========================================================================*
 *				do_open					     *
 *===========================================================================*/
PRIVATE void do_open(tp, m_ptr)
register tty_t *tp;
message *m_ptr;			/* pointer to message sent to task */
{
/* A tty line has been opened.  Make it the callers controlling tty if
 * O_NOCTTY is *not* set and it is not the log device.  1 is returned if
 * the tty is made the controlling tty, otherwise OK or an error code.
 */
  int r = OK;

  if (m_ptr->TTY_LINE == LOG_MINOR) {
	/* The log device is a write-only diagnostics device. */
	if (m_ptr->COUNT & R_BIT) r = EACCES;
  } else {
	if (!(m_ptr->COUNT & O_NOCTTY)) {
		tp->tty_pgrp = m_ptr->IO_ENDPT;
		r = 1;
	}
	tp->tty_openct++;
  }
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, r);
}

/*===========================================================================*
 *				do_close				     *
 *===========================================================================*/
PRIVATE void do_close(tp, m_ptr)
register tty_t *tp;
message *m_ptr;			/* pointer to message sent to task */
{
/* A tty line has been closed.  Clean up the line if it is the last close. */

  if (m_ptr->TTY_LINE != LOG_MINOR && --tp->tty_openct == 0) {
	tp->tty_pgrp = 0;
	tty_icancel(tp);
	(*tp->tty_ocancel)(tp, 0);
	(*tp->tty_close)(tp, 0);
	tp->tty_termios = termios_defaults;
	tp->tty_winsize = winsize_defaults;
	setattr(tp);
  }
  tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, OK);
}

/*===========================================================================*
 *				do_cancel				     *
 *===========================================================================*/
PRIVATE void do_cancel(tp, m_ptr)
register tty_t *tp;
message *m_ptr;			/* pointer to message sent to task */
{
/* A signal has been sent to a process that is hanging trying to read or write.
 * The pending read or write must be finished off immediately.
 */

  int proc_nr;
  int mode;
  int r = EINTR;

  /* Check the parameters carefully, to avoid cancelling twice. */
  proc_nr = m_ptr->IO_ENDPT;
  mode = m_ptr->COUNT;
  if ((mode & R_BIT) && tp->tty_inleft != 0 && proc_nr == tp->tty_inproc &&
	(!tp->tty_in_safe || tp->tty_in_vir_g==(vir_bytes)m_ptr->IO_GRANT)) {
	/* Process was reading when killed.  Clean up input. */
	tty_icancel(tp); 
	r = tp->tty_incum > 0 ? tp->tty_incum : EAGAIN;
	tp->tty_inleft = tp->tty_incum = tp->tty_inrevived = 0;
  } 
  if ((mode & W_BIT) && tp->tty_outleft != 0 && proc_nr == tp->tty_outproc &&
	(!tp->tty_out_safe || tp->tty_out_vir_g==(vir_bytes)m_ptr->IO_GRANT)) {
	/* Process was writing when killed.  Clean up output. */
	r = tp->tty_outcum > 0 ? tp->tty_outcum : EAGAIN;
	tp->tty_outleft = tp->tty_outcum = tp->tty_outrevived = 0;
  } 
  if (tp->tty_ioreq != 0 && proc_nr == tp->tty_ioproc) {
	/* Process was waiting for output to drain. */
	tp->tty_ioreq = 0;
  }
  tp->tty_events = 1;
  tty_reply(TASK_REPLY, m_ptr->m_source, proc_nr, r);
}

PUBLIC int select_try(struct tty *tp, int ops)
{
	int ready_ops = 0;

	/* Special case. If line is hung up, no operations will block.
	 * (and it can be seen as an exceptional condition.)
	 */
	if (tp->tty_termios.c_ospeed == B0) {
		ready_ops |= ops;
	}

	if (ops & SEL_RD) {
		/* will i/o not block on read? */
		if (tp->tty_inleft > 0) {
			ready_ops |= SEL_RD;	/* EIO - no blocking */
		} else if (tp->tty_incount > 0) {
			/* Is a regular read possible? tty_incount
			 * says there is data. But a read will only succeed
			 * in canonical mode if a newline has been seen.
			 */
			if (!(tp->tty_termios.c_lflag & ICANON) ||
				tp->tty_eotct > 0) {
				ready_ops |= SEL_RD;
			}
		}
	}

	if (ops & SEL_WR)  {
  		if (tp->tty_outleft > 0)  ready_ops |= SEL_WR;
		else if ((*tp->tty_devwrite)(tp, 1)) ready_ops |= SEL_WR;
	}
	return ready_ops;
}

PUBLIC int select_retry(struct tty *tp)
{
  	if (tp->tty_select_ops && select_try(tp, tp->tty_select_ops))
		notify(tp->tty_select_proc);
	return OK;
}

/*===========================================================================*
 *				handle_events				     *
 *===========================================================================*/
PUBLIC void handle_events(tp)
tty_t *tp;			/* TTY to check for events. */
{
/* Handle any events pending on a TTY.  These events are usually device
 * interrupts.
 *
 * Two kinds of events are prominent:
 *	- a character has been received from the console or an RS232 line.
 *	- an RS232 line has completed a write request (on behalf of a user).
 * The interrupt handler may delay the interrupt message at its discretion
 * to avoid swamping the TTY task.  Messages may be overwritten when the
 * lines are fast or when there are races between different lines, input
 * and output, because MINIX only provides single buffering for interrupt
 * messages (in proc.c).  This is handled by explicitly checking each line
 * for fresh input and completed output on each interrupt.
 */

  do {
	tp->tty_events = 0;

	/* Read input and perform input processing. */
	(*tp->tty_devread)(tp, 0);

	/* Perform output processing and write output. */
	(*tp->tty_devwrite)(tp, 0);

	/* Ioctl waiting for some event? */
	if (tp->tty_ioreq != 0) dev_ioctl(tp);
  } while (tp->tty_events);

  /* Transfer characters from the input queue to a waiting process. */
  in_transfer(tp);

  /* Reply if enough bytes are available. */
  if (tp->tty_incum >= tp->tty_min && tp->tty_inleft > 0) {
	if (tp->tty_inrepcode == TTY_REVIVE) {
		notify(tp->tty_incaller);
		tp->tty_inrevived = 1;
	} else {
		tty_reply(tp->tty_inrepcode, tp->tty_incaller, 
			tp->tty_inproc, tp->tty_incum);
		tp->tty_inleft = tp->tty_incum = 0;
	}
  }
  if (tp->tty_select_ops)
  {
  	select_retry(tp);
  }
#if NR_PTYS > 0
  if (ispty(tp))
  	select_retry_pty(tp);
#endif
}

/*===========================================================================*
 *				in_transfer				     *
 *===========================================================================*/
PRIVATE void in_transfer(tp)
register tty_t *tp;		/* pointer to terminal to read from */
{
/* Transfer bytes from the input queue to a process reading from a terminal. */

  int ch;
  int count;
  char buf[64], *bp;

  /* Force read to succeed if the line is hung up, looks like EOF to reader. */
  if (tp->tty_termios.c_ospeed == B0) tp->tty_min = 0;

  /* Anything to do? */
  if (tp->tty_inleft == 0 || tp->tty_eotct < tp->tty_min) return;

  bp = buf;
  while (tp->tty_inleft > 0 && tp->tty_eotct > 0) {
	ch = *tp->tty_intail;

	if (!(ch & IN_EOF)) {
		/* One character to be delivered to the user. */
		*bp = ch & IN_CHAR;
		tp->tty_inleft--;
		if (++bp == bufend(buf)) {
			/* Temp buffer full, copy to user space. */
			if(tp->tty_in_safe) {
				sys_safecopyto(tp->tty_inproc,
					tp->tty_in_vir_g, tp->tty_in_vir_offset,
					(vir_bytes) buf,
					(vir_bytes) buflen(buf), D);
				tp->tty_in_vir_offset += buflen(buf);
			} else {
				sys_vircopy(SELF, D, (vir_bytes) buf, 
					tp->tty_inproc, D, tp->tty_in_vir_g,
					(vir_bytes) buflen(buf));
				tp->tty_in_vir_g += buflen(buf);
			}
			tp->tty_incum += buflen(buf);
			bp = buf;
		}
	}

	/* Remove the character from the input queue. */
	if (++tp->tty_intail == bufend(tp->tty_inbuf))
		tp->tty_intail = tp->tty_inbuf;
	tp->tty_incount--;
	if (ch & IN_EOT) {
		tp->tty_eotct--;
		/* Don't read past a line break in canonical mode. */
		if (tp->tty_termios.c_lflag & ICANON) tp->tty_inleft = 0;
	}
  }

  if (bp > buf) {
	/* Leftover characters in the buffer. */
	count = bp - buf;
	if(tp->tty_in_safe) {
		sys_safecopyto(tp->tty_inproc,
			tp->tty_in_vir_g, tp->tty_in_vir_offset,
			(vir_bytes) buf, (vir_bytes) count, D);
		tp->tty_in_vir_offset += count;
	} else {
		sys_vircopy(SELF, D, (vir_bytes) buf, 
			tp->tty_inproc, D, tp->tty_in_vir_g, (vir_bytes) count);
		tp->tty_in_vir_g += count;
	}
	tp->tty_incum += count;
  }

  /* Usually reply to the reader, possibly even if incum == 0 (EOF). */
  if (tp->tty_inleft == 0) {
	if (tp->tty_inrepcode == TTY_REVIVE) {
		notify(tp->tty_incaller);
		tp->tty_inrevived = 1;
	} else {
		tty_reply(tp->tty_inrepcode, tp->tty_incaller, 
			tp->tty_inproc, tp->tty_incum);
		tp->tty_inleft = tp->tty_incum = 0;
	}
  }
}

/*===========================================================================*
 *				in_process				     *
 *===========================================================================*/
PUBLIC int in_process(tp, buf, count)
register tty_t *tp;		/* terminal on which character has arrived */
char *buf;			/* buffer with input characters */
int count;			/* number of input characters */
{
/* Characters have just been typed in.  Process, save, and echo them.  Return
 * the number of characters processed.
 */

  int ch, sig, ct;
  int timeset = FALSE;

  for (ct = 0; ct < count; ct++) {
	/* Take one character. */
	ch = *buf++ & BYTE;

	/* Strip to seven bits? */
	if (tp->tty_termios.c_iflag & ISTRIP) ch &= 0x7F;

	/* Input extensions? */
	if (tp->tty_termios.c_lflag & IEXTEN) {

		/* Previous character was a character escape? */
		if (tp->tty_escaped) {
			tp->tty_escaped = NOT_ESCAPED;
			ch |= IN_ESC;	/* protect character */
		}

		/* LNEXT (^V) to escape the next character? */
		if (ch == tp->tty_termios.c_cc[VLNEXT]) {
			tp->tty_escaped = ESCAPED;
			rawecho(tp, '^');
			rawecho(tp, '\b');
			continue;	/* do not store the escape */
		}

		/* REPRINT (^R) to reprint echoed characters? */
		if (ch == tp->tty_termios.c_cc[VREPRINT]) {
			reprint(tp);
			continue;
		}
	}

	/* _POSIX_VDISABLE is a normal character value, so better escape it. */
	if (ch == _POSIX_VDISABLE) ch |= IN_ESC;

	/* Map CR to LF, ignore CR, or map LF to CR. */
	if (ch == '\r') {
		if (tp->tty_termios.c_iflag & IGNCR) continue;
		if (tp->tty_termios.c_iflag & ICRNL) ch = '\n';
	} else
	if (ch == '\n') {
		if (tp->tty_termios.c_iflag & INLCR) ch = '\r';
	}

	/* Canonical mode? */
	if (tp->tty_termios.c_lflag & ICANON) {

		/* Erase processing (rub out of last character). */
		if (ch == tp->tty_termios.c_cc[VERASE]) {
			(void) back_over(tp);
			if (!(tp->tty_termios.c_lflag & ECHOE)) {
				(void) tty_echo(tp, ch);
			}
			continue;
		}

        /* Erase processing (rub out of last word). */
        if (ch == tp->tty_termios.c_cc[VERASEWORD]) {
            (void) back_over_word(tp);
            if (!(tp->tty_termios.c_lflag & ECHOE)) {
                (void) tty_echo(tp, ch);
            }
            continue;
        }

		/* Kill processing (remove current line). */
		if (ch == tp->tty_termios.c_cc[VKILL]) {
			while (back_over(tp)) {}
			if (!(tp->tty_termios.c_lflag & ECHOE)) {
				(void) tty_echo(tp, ch);
				if (tp->tty_termios.c_lflag & ECHOK)
					rawecho(tp, '\n');
			}
			continue;
		}

		/* EOF (^D) means end-of-file, an invisible "line break". */
		if (ch == tp->tty_termios.c_cc[VEOF]) ch |= IN_EOT | IN_EOF;

		/* The line may be returned to the user after an LF. */
		if (ch == '\n') ch |= IN_EOT;

		/* Same thing with EOL, whatever it may be. */
		if (ch == tp->tty_termios.c_cc[VEOL]) ch |= IN_EOT;
	}

	/* Start/stop input control? */
	if (tp->tty_termios.c_iflag & IXON) {

		/* Output stops on STOP (^S). */
		if (ch == tp->tty_termios.c_cc[VSTOP]) {
			tp->tty_inhibited = STOPPED;
			tp->tty_events = 1;
			continue;
		}

		/* Output restarts on START (^Q) or any character if IXANY. */
		if (tp->tty_inhibited) {
			if (ch == tp->tty_termios.c_cc[VSTART]
					|| (tp->tty_termios.c_iflag & IXANY)) {
				tp->tty_inhibited = RUNNING;
				tp->tty_events = 1;
				if (ch == tp->tty_termios.c_cc[VSTART])
					continue;
			}
		}
	}

	if (tp->tty_termios.c_lflag & ISIG) {
		/* Check for INTR (^?) and QUIT (^\) characters. */
		if (ch == tp->tty_termios.c_cc[VINTR]
					|| ch == tp->tty_termios.c_cc[VQUIT]) {
			sig = SIGINT;
			if (ch == tp->tty_termios.c_cc[VQUIT]) sig = SIGQUIT;
			sigchar(tp, sig, 1);
			(void) tty_echo(tp, ch);
			continue;
		}
	}

	/* Is there space in the input buffer? */
	if (tp->tty_incount == buflen(tp->tty_inbuf)) {
		/* No space; discard in canonical mode, keep in raw mode. */
		if (tp->tty_termios.c_lflag & ICANON) continue;
		break;
	}

	if (!(tp->tty_termios.c_lflag & ICANON)) {
		/* In raw mode all characters are "line breaks". */
		ch |= IN_EOT;

		/* Start an inter-byte timer? */
		if (!timeset && tp->tty_termios.c_cc[VMIN] > 0
				&& tp->tty_termios.c_cc[VTIME] > 0) {
			settimer(tp, TRUE);
			timeset = TRUE;
		}
	}

	/* Perform the intricate function of echoing. */
	if (tp->tty_termios.c_lflag & (ECHO|ECHONL)) ch = tty_echo(tp, ch);

	/* Save the character in the input queue. */
	*tp->tty_inhead++ = ch;
	if (tp->tty_inhead == bufend(tp->tty_inbuf))
		tp->tty_inhead = tp->tty_inbuf;
	tp->tty_incount++;
	if (ch & IN_EOT) tp->tty_eotct++;

	/* Try to finish input if the queue threatens to overflow. */
	if (tp->tty_incount == buflen(tp->tty_inbuf)) in_transfer(tp);
  }
  return ct;
}

/*===========================================================================*
 *				echo					     *
 *===========================================================================*/
PRIVATE int tty_echo(tp, ch)
register tty_t *tp;		/* terminal on which to echo */
register int ch;		/* pointer to character to echo */
{
/* Echo the character if echoing is on.  Some control characters are echoed
 * with their normal effect, other control characters are echoed as "^X",
 * normal characters are echoed normally.  EOF (^D) is echoed, but immediately
 * backspaced over.  Return the character with the echoed length added to its
 * attributes.
 */
  int len, rp;

  ch &= ~IN_LEN;
  if (!(tp->tty_termios.c_lflag & ECHO)) {
	if (ch == ('\n' | IN_EOT) && (tp->tty_termios.c_lflag
					& (ICANON|ECHONL)) == (ICANON|ECHONL))
		(*tp->tty_echo)(tp, '\n');
	return(ch);
  }

  /* "Reprint" tells if the echo output has been messed up by other output. */
  rp = tp->tty_incount == 0 ? FALSE : tp->tty_reprint;

  if ((ch & IN_CHAR) < ' ') {
	switch (ch & (IN_ESC|IN_EOF|IN_EOT|IN_CHAR)) {
	    case '\t':
		len = 0;
		do {
			(*tp->tty_echo)(tp, ' ');
			len++;
		} while (len < TAB_SIZE && (tp->tty_position & TAB_MASK) != 0);
		break;
	    case '\r' | IN_EOT:
	    case '\n' | IN_EOT:
		(*tp->tty_echo)(tp, ch & IN_CHAR);
		len = 0;
		break;
	    default:
		(*tp->tty_echo)(tp, '^');
		(*tp->tty_echo)(tp, '@' + (ch & IN_CHAR));
		len = 2;
	}
  } else
  if ((ch & IN_CHAR) == '\177') {
	/* A DEL prints as "^?". */
	(*tp->tty_echo)(tp, '^');
	(*tp->tty_echo)(tp, '?');
	len = 2;
  } else {
	(*tp->tty_echo)(tp, ch & IN_CHAR);
	len = 1;
  }
  if (ch & IN_EOF) while (len > 0) { (*tp->tty_echo)(tp, '\b'); len--; }

  tp->tty_reprint = rp;
  return(ch | (len << IN_LSHIFT));
}

/*===========================================================================*
 *				rawecho					     *
 *===========================================================================*/
PRIVATE void rawecho(tp, ch)
register tty_t *tp;
int ch;
{
/* Echo without interpretation if ECHO is set. */
  int rp = tp->tty_reprint;
  if (tp->tty_termios.c_lflag & ECHO) (*tp->tty_echo)(tp, ch);
  tp->tty_reprint = rp;
}

/*===========================================================================*
 *				back_over				     *
 *===========================================================================*/
PRIVATE int back_over(tp)
register tty_t *tp;
{
/* Backspace to previous character on screen and erase it. */
  u16_t *head;
  int len;

  if (tp->tty_incount == 0) return(0);	/* queue empty */
  head = tp->tty_inhead;
  if (head == tp->tty_inbuf) head = bufend(tp->tty_inbuf);
  if (*--head & IN_EOT) return(0);		/* can't erase "line breaks" */
  if (tp->tty_reprint) reprint(tp);		/* reprint if messed up */
  tp->tty_inhead = head;
  tp->tty_incount--;
  if (tp->tty_termios.c_lflag & ECHOE) {
	len = (*head & IN_LEN) >> IN_LSHIFT;
	while (len > 0) {
		rawecho(tp, '\b');
		rawecho(tp, ' ');
		rawecho(tp, '\b');
		len--;
	}
  }
  return(1);				/* one character erased */
}

/*===========================================================================*
 *				back_over_word				     *
 *===========================================================================*/
PRIVATE int back_over_word(tp)
register tty_t *tp;
{
/* erase characters until whitespace or start of string */
  u16_t *head;
  u16_t last;
  u8_t last_char;
  int len;
  int erase_more = 1;

  while(erase_more == 1) {
  
      if (tp->tty_incount == 0) return(0);    /* queue empty */
      head = tp->tty_inhead;
      if (head == tp->tty_inbuf) head = bufend(tp->tty_inbuf);
      
      /* if the latest character is a-z or A-Z or 0-9 */
      last = *--head;
      last_char = (last & 0x00FF);
      if ((last_char >= '0' && last_char <= '9') ||
          (last_char >= 'a' && last_char <= 'z') ||
          (last_char >= 'A' && last_char <= 'Z')) {
          (void) back_over(tp);
      } else if (last_char == ' ') {
          (void) back_over(tp);
          erase_more = 0;
      } else {
          erase_more = 0;
      }
  }
      
return(1);                /* one word erased */
}

/*===========================================================================*
 *				reprint					     *
 *===========================================================================*/
PRIVATE void reprint(tp)
register tty_t *tp;		/* pointer to tty struct */
{
/* Restore what has been echoed to screen before if the user input has been
 * messed up by output, or if REPRINT (^R) is typed.
 */
  int count;
  u16_t *head;

  tp->tty_reprint = FALSE;

  /* Find the last line break in the input. */
  head = tp->tty_inhead;
  count = tp->tty_incount;
  while (count > 0) {
	if (head == tp->tty_inbuf) head = bufend(tp->tty_inbuf);
	if (head[-1] & IN_EOT) break;
	head--;
	count--;
  }
  if (count == tp->tty_incount) return;		/* no reason to reprint */

  /* Show REPRINT (^R) and move to a new line. */
  (void) tty_echo(tp, tp->tty_termios.c_cc[VREPRINT] | IN_ESC);
  rawecho(tp, '\r');
  rawecho(tp, '\n');

  /* Reprint from the last break onwards. */
  do {
	if (head == bufend(tp->tty_inbuf)) head = tp->tty_inbuf;
	*head = tty_echo(tp, *head);
	head++;
	count++;
  } while (count < tp->tty_incount);
}

/*===========================================================================*
 *				out_process				     *
 *===========================================================================*/
PUBLIC void out_process(tp, bstart, bpos, bend, icount, ocount)
tty_t *tp;
char *bstart, *bpos, *bend;	/* start/pos/end of circular buffer */
int *icount;			/* # input chars / input chars used */
int *ocount;			/* max output chars / output chars used */
{
/* Perform output processing on a circular buffer.  *icount is the number of
 * bytes to process, and the number of bytes actually processed on return.
 * *ocount is the space available on input and the space used on output.
 * (Naturally *icount < *ocount.)  The column position is updated modulo
 * the TAB size, because we really only need it for tabs.
 */

  int tablen;
  int ict = *icount;
  int oct = *ocount;
  int pos = tp->tty_position;

  while (ict > 0) {
	switch (*bpos) {
	case '\7':
		break;
	case '\b':
		pos--;
		break;
	case '\r':
		pos = 0;
		break;
	case '\n':
		if ((tp->tty_termios.c_oflag & (OPOST|ONLCR))
							== (OPOST|ONLCR)) {
			/* Map LF to CR+LF if there is space.  Note that the
			 * next character in the buffer is overwritten, so
			 * we stop at this point.
			 */
			if (oct >= 2) {
				*bpos = '\r';
				if (++bpos == bend) bpos = bstart;
				*bpos = '\n';
				pos = 0;
				ict--;
				oct -= 2;
			}
			goto out_done;	/* no space or buffer got changed */
		}
		break;
	case '\t':
		/* Best guess for the tab length. */
		tablen = TAB_SIZE - (pos & TAB_MASK);

		if ((tp->tty_termios.c_oflag & (OPOST|XTABS))
							== (OPOST|XTABS)) {
			/* Tabs must be expanded. */
			if (oct >= tablen) {
				pos += tablen;
				ict--;
				oct -= tablen;
				do {
					*bpos = ' ';
					if (++bpos == bend) bpos = bstart;
				} while (--tablen != 0);
			}
			goto out_done;
		}
		/* Tabs are output directly. */
		pos += tablen;
		break;
	default:
		/* Assume any other character prints as one character. */
		pos++;
	}
	if (++bpos == bend) bpos = bstart;
	ict--;
	oct--;
  }
out_done:
  tp->tty_position = pos & TAB_MASK;

  *icount -= ict;	/* [io]ct are the number of chars not used */
  *ocount -= oct;	/* *[io]count are the number of chars that are used */
}

/*===========================================================================*
 *				dev_ioctl				     *
 *===========================================================================*/
PRIVATE void dev_ioctl(tp)
tty_t *tp;
{
/* The ioctl's TCSETSW, TCSETSF and TCDRAIN wait for output to finish to make
 * sure that an attribute change doesn't affect the processing of current
 * output.  Once output finishes the ioctl is executed as in do_ioctl().
 */
  int result = EINVAL;

  if (tp->tty_outleft > 0) return;		/* output not finished */

  if (tp->tty_ioreq != TCDRAIN) {
	if (tp->tty_ioreq == TCSETSF) tty_icancel(tp);
	if(tp->tty_io_safe) {
	   result = sys_safecopyfrom(tp->tty_ioproc, tp->tty_iovir_g, 0,
		(vir_bytes) &tp->tty_termios,
		(vir_bytes) sizeof(tp->tty_termios), D);
	} else {
	    result = sys_vircopy(tp->tty_ioproc, D, tp->tty_iovir_g,
			SELF, D, (vir_bytes) &tp->tty_termios,
			(vir_bytes) sizeof(tp->tty_termios));
	}
	setattr(tp);
  }
  tp->tty_ioreq = 0;
  notify(tp->tty_iocaller);
  tp->tty_iorevived = 1;
  tp->tty_iostatus = result;
}

/*===========================================================================*
 *				setattr					     *
 *===========================================================================*/
PRIVATE void setattr(tp)
tty_t *tp;
{
/* Apply the new line attributes (raw/canonical, line speed, etc.) */
  u16_t *inp;
  int count;

  if (!(tp->tty_termios.c_lflag & ICANON)) {
	/* Raw mode; put a "line break" on all characters in the input queue.
	 * It is undefined what happens to the input queue when ICANON is
	 * switched off, a process should use TCSAFLUSH to flush the queue.
	 * Keeping the queue to preserve typeahead is the Right Thing, however
	 * when a process does use TCSANOW to switch to raw mode.
	 */
	count = tp->tty_eotct = tp->tty_incount;
	inp = tp->tty_intail;
	while (count > 0) {
		*inp |= IN_EOT;
		if (++inp == bufend(tp->tty_inbuf)) inp = tp->tty_inbuf;
		--count;
	}
  }

  /* Inspect MIN and TIME. */
  settimer(tp, FALSE);
  if (tp->tty_termios.c_lflag & ICANON) {
	/* No MIN & TIME in canonical mode. */
	tp->tty_min = 1;
  } else {
	/* In raw mode MIN is the number of chars wanted, and TIME how long
	 * to wait for them.  With interesting exceptions if either is zero.
	 */
	tp->tty_min = tp->tty_termios.c_cc[VMIN];
	if (tp->tty_min == 0 && tp->tty_termios.c_cc[VTIME] > 0)
		tp->tty_min = 1;
  }

  if (!(tp->tty_termios.c_iflag & IXON)) {
	/* No start/stop output control, so don't leave output inhibited. */
	tp->tty_inhibited = RUNNING;
	tp->tty_events = 1;
  }

  /* Setting the output speed to zero hangs up the phone. */
  if (tp->tty_termios.c_ospeed == B0) sigchar(tp, SIGHUP, 1);

  /* Set new line speed, character size, etc at the device level. */
  (*tp->tty_ioctl)(tp, 0);
}

/*===========================================================================*
 *				tty_reply				     *
 *===========================================================================*/
PUBLIC void 
tty_reply_f(
file, line, code, replyee, proc_nr, status)
char *file;
int line;
int code;			/* TASK_REPLY or REVIVE */
int replyee;			/* destination address for the reply */
int proc_nr;			/* to whom should the reply go? */
int status;			/* reply code */
{
/* Send a reply to a process that wanted to read or write data. */
  message tty_mess;

  tty_mess.m_type = code;
  tty_mess.REP_ENDPT = proc_nr;
  tty_mess.REP_STATUS = status;

  /* TTY is not supposed to send a TTY_REVIVE message. The
   * REVIVE message is gone, TTY_REVIVE is only used as an internal
   * placeholder for something that is not supposed to be a message.
   */
  if(code == TTY_REVIVE) {
	panicing = 1;
	printf("%s:%d: ", file, line);
	panic("TTY","tty_reply sending TTY_REVIVE", NO_NUM);
  }

  status = sendnb(replyee, &tty_mess);
  if (status != OK)
	printf("tty`tty_reply: send to %d failed: %d\n", replyee, status);
}

/*===========================================================================*
 *				sigchar					     *
 *===========================================================================*/
PUBLIC void sigchar(tp, sig, mayflush)
register tty_t *tp;
int sig;			/* SIGINT, SIGQUIT, SIGKILL or SIGHUP */
int mayflush;
{
/* Process a SIGINT, SIGQUIT or SIGKILL char from the keyboard or SIGHUP from
 * a tty close, "stty 0", or a real RS-232 hangup.  MM will send the signal to
 * the process group (INT, QUIT), all processes (KILL), or the session leader
 * (HUP).
 */
  int status;

  if (tp->tty_pgrp != 0)  {
      if (OK != (status = sys_kill(tp->tty_pgrp, sig))) {
        panic("TTY","Error, call to sys_kill failed", status);
      }
  }

  if (mayflush && !(tp->tty_termios.c_lflag & NOFLSH)) {
	tp->tty_incount = tp->tty_eotct = 0;	/* kill earlier input */
	tp->tty_intail = tp->tty_inhead;
	(*tp->tty_ocancel)(tp, 0);			/* kill all output */
	tp->tty_inhibited = RUNNING;
	tp->tty_events = 1;
  }
}

/*===========================================================================*
 *				tty_icancel				     *
 *===========================================================================*/
PRIVATE void tty_icancel(tp)
register tty_t *tp;
{
/* Discard all pending input, tty buffer or device. */

  tp->tty_incount = tp->tty_eotct = 0;
  tp->tty_intail = tp->tty_inhead;
  (*tp->tty_icancel)(tp, 0);
}

/*===========================================================================*
 *				tty_init				     *
 *===========================================================================*/
PRIVATE void tty_init()
{
/* Initialize tty structure and call device initialization routines. */

  register tty_t *tp;
  int s;

  system_hz = sys_hz();

  /* Initialize the terminal lines. */
  for (tp = FIRST_TTY,s=0; tp < END_TTY; tp++,s++) {

  	tp->tty_index = s;

  	tmr_inittimer(&tp->tty_tmr);

  	tp->tty_intail = tp->tty_inhead = tp->tty_inbuf;
  	tp->tty_min = 1;
  	tp->tty_termios = termios_defaults;
  	tp->tty_icancel = tp->tty_ocancel = tp->tty_ioctl = tp->tty_close =
								tty_devnop;
  	if (tp < tty_addr(NR_CONS)) {
		scr_init(tp);

		/* Initialize the keyboard driver. */
		kb_init(tp);

  		tp->tty_minor = CONS_MINOR + s;
  	} else
  	if (tp < tty_addr(NR_CONS+NR_RS_LINES)) {
		rs_init(tp);
  		tp->tty_minor = RS232_MINOR + s-NR_CONS;
  	} else {
		pty_init(tp);
		tp->tty_minor = s - (NR_CONS+NR_RS_LINES) + TTYPX_MINOR;
  	}
  }

}

/*===========================================================================*
 *				tty_timed_out				     *
 *===========================================================================*/
PRIVATE void tty_timed_out(timer_t *tp)
{
/* This timer has expired. Set the events flag, to force processing. */
  tty_t *tty_ptr;
  tty_ptr = &tty_table[tmr_arg(tp)->ta_int];
  tty_ptr->tty_min = 0;			/* force read to succeed */
  tty_ptr->tty_events = 1;		
}

/*===========================================================================*
 *				expire_timers			    	     *
 *===========================================================================*/
PRIVATE void expire_timers(void)
{
/* A synchronous alarm message was received. Check if there are any expired 
 * timers. Possibly set the event flag and reschedule another alarm.  
 */
  clock_t now;				/* current time */
  int s;

  /* Get the current time to compare the timers against. */
  if ((s=getuptime(&now)) != OK)
 	panic("TTY","Couldn't get uptime from clock.", s);

  /* Scan the queue of timers for expired timers. This dispatch the watchdog
   * functions of expired timers. Possibly a new alarm call must be scheduled.
   */
  tmrs_exptimers(&tty_timers, now, NULL);
  if (tty_timers == NULL) tty_next_timeout = TMR_NEVER;
  else {  					  /* set new sync alarm */
  	tty_next_timeout = tty_timers->tmr_exp_time;
  	if ((s=sys_setalarm(tty_next_timeout, 1)) != OK)
 		panic("TTY","Couldn't set synchronous alarm.", s);
  }
}

/*===========================================================================*
 *				settimer				     *
 *===========================================================================*/
PRIVATE void settimer(tty_ptr, enable)
tty_t *tty_ptr;			/* line to set or unset a timer on */
int enable;			/* set timer if true, otherwise unset */
{
  clock_t now;				/* current time */
  clock_t exp_time;
  int s;

  /* Get the current time to calculate the timeout time. */
  if ((s=getuptime(&now)) != OK)
 	panic("TTY","Couldn't get uptime from clock.", s);
  if (enable) {
  	exp_time = now + tty_ptr->tty_termios.c_cc[VTIME] * (system_hz/10);
 	/* Set a new timer for enabling the TTY events flags. */
 	tmrs_settimer(&tty_timers, &tty_ptr->tty_tmr, 
 		exp_time, tty_timed_out, NULL);  
  } else {
  	/* Remove the timer from the active and expired lists. */
  	tmrs_clrtimer(&tty_timers, &tty_ptr->tty_tmr, NULL);
  }
  
  /* Now check if a new alarm must be scheduled. This happens when the front
   * of the timers queue was disabled or reinserted at another position, or
   * when a new timer was added to the front.
   */
  if (tty_timers == NULL) tty_next_timeout = TMR_NEVER;
  else if (tty_timers->tmr_exp_time != tty_next_timeout) { 
  	tty_next_timeout = tty_timers->tmr_exp_time;
  	if ((s=sys_setalarm(tty_next_timeout, 1)) != OK)
 		panic("TTY","Couldn't set synchronous alarm.", s);
  }
}

/*===========================================================================*
 *				tty_devnop				     *
 *===========================================================================*/
PUBLIC int tty_devnop(tp, try)
tty_t *tp;
int try;
{
  /* Some functions need not be implemented at the device level. */
  return 0;
}

/*===========================================================================*
 *				do_select				     *
 *===========================================================================*/
PRIVATE void do_select(tp, m_ptr)
register tty_t *tp;		/* pointer to tty struct */
register message *m_ptr;	/* pointer to message sent to the task */
{
	int ops, ready_ops = 0, watch;

	ops = m_ptr->IO_ENDPT & (SEL_RD|SEL_WR|SEL_ERR);
	watch = (m_ptr->IO_ENDPT & SEL_NOTIFY) ? 1 : 0;

	ready_ops = select_try(tp, ops);

	if (!ready_ops && ops && watch) {
		tp->tty_select_ops |= ops;
		tp->tty_select_proc = m_ptr->m_source;
	}

        tty_reply(TASK_REPLY, m_ptr->m_source, m_ptr->IO_ENDPT, ready_ops);

        return;
}

