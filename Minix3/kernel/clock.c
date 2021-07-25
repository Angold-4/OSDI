/* This file contains the clock task, which handles time related functions.
 * Important events that are handled by the CLOCK include setting and 
 * monitoring alarm timers and deciding when to (re)schedule processes. 
 * The CLOCK offers a direct interface to kernel processes. System services 
 * can access its services through system calls, such as sys_setalarm(). The
 * CLOCK task thus is hidden from the outside world.  
 *
 * Changes:
 *   Aug 18, 2006   removed direct hardware access etc, MinixPPC (Ingmar Alting)
 *   Oct 08, 2005   reordering and comment editing (A. S. Woodhull)
 *   Mar 18, 2004   clock interface moved to SYSTEM task (Jorrit N. Herder) 
 *   Sep 30, 2004   source code documentation updated  (Jorrit N. Herder)
 *   Sep 24, 2004   redesigned alarm timers  (Jorrit N. Herder)
 *
 * The function do_clocktick() is triggered by the clock's interrupt 
 * handler when a watchdog timer has expired or a process must be scheduled. 
 *
 * In addition to the main clock_task() entry point, which starts the main 
 * loop, there are several other minor entry points:
 *   clock_stop:	called just before MINIX shutdown
 *   get_uptime:	get realtime since boot in clock ticks
 *   set_timer:		set a watchdog timer (+)
 *   reset_timer:	reset a watchdog timer (+)
 *   read_clock:	read the counter of channel 0 of the 8253A timer
 *
 * (+) The CLOCK task keeps tracks of watchdog timers for the entire kernel.
 * The watchdog functions of expired timers are executed in do_clocktick(). 
 * It is crucial that watchdog functions not block, or the CLOCK task may
 * be blocked. Do not send() a message when the receiver is not expecting it.
 * Instead, notify(), which always returns, should be used. 
 */

#include "kernel.h"
#include "proc.h"
#include <signal.h>
#include <minix/com.h>
#include <minix/endpoint.h>
#include <minix/portio.h>

/* Function prototype for PRIVATE functions.
 */ 
FORWARD _PROTOTYPE( void init_clock, (void) );
FORWARD _PROTOTYPE( int clock_handler, (irq_hook_t *hook) );
FORWARD _PROTOTYPE( void do_clocktick, (message *m_ptr) );
FORWARD _PROTOTYPE( void load_update, (void));

/* The CLOCK's timers queue. The functions in <timers.h> operate on this. 
 * Each system process possesses a single synchronous alarm timer. If other 
 * kernel parts want to use additional timers, they must declare their own 
 * persistent (static) timer structure, which can be passed to the clock
 * via (re)set_timer().
 * When a timer expires its watchdog function is run by the CLOCK task. 
 */
PRIVATE timer_t *clock_timers;	/* queue of CLOCK timers */
PRIVATE clock_t next_timeout;	/* realtime that next timer expires */

/* The time is incremented by the interrupt handler on each clock tick.
 */
PRIVATE clock_t realtime = 0;		      /* real time clock */
PRIVATE irq_hook_t clock_hook;		/* interrupt handler hook */

/*===========================================================================*
 *				clock_task				     *
 *===========================================================================*/
PUBLIC void clock_task()
{
/* Main program of clock task. If the call is not HARD_INT it is an error.
 */
  message m;       /* message buffer for both input and output */
  int result;      /* result returned by the handler */

  init_clock();    /* initialize clock task */
    
  /* Main loop of the clock task.  Get work, process it. Never reply. */
  while(TRUE) {
	/* Go get a message. */
	result = receive(ANY, &m);

	if(result != OK)
		minix_panic("receive() failed", result);

	/* Handle the request. Only clock ticks are expected. */
	if (is_notify(m.m_type)) {
		switch (_ENDPOINT_P(m.m_source)) {
			case HARDWARE:
				do_clocktick(&m); /* handle clock tick */
				break;
			default:	/* illegal request type */
				kprintf("CLOCK: illegal notify %d from %d.\n",
					m.m_type, m.m_source);
		}
	}
	else {
		/* illegal request type */
		kprintf("CLOCK: illegal request %d from %d.\n",
			m.m_type, m.m_source);
	}
  }
}

/*===========================================================================*
 *				do_clocktick				     *
 *===========================================================================*/
PRIVATE void do_clocktick(m_ptr)
message *m_ptr;				/* pointer to request message */
{
  register struct proc *bill_copy = bill_ptr;

/* Despite its name, this routine is not called on every clock tick. It
 * is called on those clock ticks when a lot of work needs to be done.
 */
  
  /* A process used up a full quantum. The interrupt handler stored this
   * process in 'prev_ptr'.  First make sure that the process is not on the 
   * scheduling queues.  Then announce the process ready again. Since it has 
   * no more time left, it gets a new quantum and is inserted at the right 
   * place in the queues.  As a side-effect a new process will be scheduled.
   */ 
  if (prev_ptr->p_ticks_left <= 0 && priv(prev_ptr)->s_flags & PREEMPTIBLE) {
      if(prev_ptr->p_rts_flags == 0) {	/* if it was runnable .. */
      lock;
      {
	dequeue(prev_ptr);		/* take it off the queues */
      	enqueue(prev_ptr);		/* and reinsert it again */ 
      }
      unlock;
      } else {
	kprintf("CLOCK: %d not runnable; flags: %x\n",
		prev_ptr->p_endpoint, prev_ptr->p_rts_flags);
      }
  }

  /* Check if a process-virtual timer expired. Check prev_ptr, but also
   * bill_ptr - one process's user time is another's system time, and the
   * profile timer decreases for both! Do this before the queue operations
   * below, which may alter bill_ptr. Note the use a copy of bill_ptr, because
   * bill_ptr may have been changed above, and this code can't be put higher
   * up because otherwise cause_sig() may dequeue prev_ptr before we do.
   */
  vtimer_check(prev_ptr);

  if (prev_ptr != bill_copy)
	vtimer_check(bill_copy);

  /* Check if a clock timer expired and run its watchdog function. */
  if (next_timeout <= realtime) {
  	tmrs_exptimers(&clock_timers, realtime, NULL);  	
	next_timeout = (clock_timers == NULL) ?
		 TMR_NEVER : clock_timers->tmr_exp_time;	
  }

  return;
}

/*===========================================================================*
 *				init_clock				     *
 *===========================================================================*/
PRIVATE void init_clock()
{
  /* First of all init the clock system.
   *
   * Here the (a) clock is set to produce a interrupt at
   * every 1/60 second (ea. 60Hz).
   *
   * Running right away.
   */
  arch_init_clock();	/* architecture-dependent initialization. */
   
  /* Initialize the CLOCK's interrupt hook. */
  clock_hook.proc_nr_e = CLOCK;
 
  put_irq_handler(&clock_hook, CLOCK_IRQ, clock_handler);
  enable_irq(&clock_hook);		/* ready for clock interrupts */
    
  /* Set a watchdog timer to periodically balance the scheduling queues. */
  balance_queues(NULL);			/* side-effect sets new timer */
}

/*===========================================================================*
 *				clock_handler				     *
 *===========================================================================*/
PRIVATE int clock_handler(hook)
irq_hook_t *hook;
{
/* This executes on each clock tick (i.e., every time the timer chip generates 
 * an interrupt). It does a little bit of work so the clock task does not have 
 * to be called on every tick.  The clock task is called when:
 *
 *	(1) the scheduling quantum of the running process has expired, or
 *	(2) a timer has expired and the watchdog function should be run.
 *
 * Many global global and static variables are accessed here.  The safety of
 * this must be justified. All scheduling and message passing code acquires a 
 * lock by temporarily disabling interrupts, so no conflicts with calls from 
 * the task level can occur. Furthermore, interrupts are not reentrant, the 
 * interrupt handler cannot be bothered by other interrupts.
 * 
 * Variables that are updated in the clock's interrupt handler:
 *	lost_ticks:
 *		Clock ticks counted outside the clock task. This for example
 *		is used when the boot monitor processes a real mode interrupt.
 * 	realtime:
 * 		The current uptime is incremented with all outstanding ticks.
 *	proc_ptr, bill_ptr:
 *		These are used for accounting and virtual timers. It does not
 *		matter if proc.c is changing them, provided they are always
 * 		valid pointers, since at worst the previous process would be
 *		billed.
 */
  register unsigned ticks;
  register int expired;

  if(minix_panicing) return;

  /* Get number of ticks and update realtime. */
  ticks = lost_ticks + 1;
  lost_ticks = 0;
  realtime += ticks;

  /* Update user and system accounting times. Charge the current process for
   * user time. If the current process is not billable, that is, if a non-user
   * process is running, charge the billable process for system time as well.
   * Thus the unbillable process' user time is the billable user's system time.
   */
  
  proc_ptr->p_user_time += ticks;
  if (priv(proc_ptr)->s_flags & PREEMPTIBLE) {
      proc_ptr->p_ticks_left -= ticks;
  }
  if (! (priv(proc_ptr)->s_flags & BILLABLE)) {
      bill_ptr->p_sys_time += ticks;
      bill_ptr->p_ticks_left -= ticks;
  }

  /* Decrement virtual timers, if applicable. We decrement both the virtual
   * and the profile timer of the current process, and if the current process
   * is not billable, the timer of the billed process as well.
   * If any of the timers expire, do_clocktick() will send out signals.
   */
  expired = 0;
  if ((proc_ptr->p_misc_flags & MF_VIRT_TIMER) &&
	(proc_ptr->p_virt_left -= ticks) <= 0) expired = 1;
  if ((proc_ptr->p_misc_flags & MF_PROF_TIMER) &&
	(proc_ptr->p_prof_left -= ticks) <= 0) expired = 1;
  if (! (priv(proc_ptr)->s_flags & BILLABLE) &&
  	(bill_ptr->p_misc_flags & MF_PROF_TIMER) &&
  	(bill_ptr->p_prof_left -= ticks) <= 0) expired = 1;

  /* Update load average. */
  load_update();
  
  /* Check if do_clocktick() must be called. Done for alarms and scheduling.
   * Some processes, such as the kernel tasks, cannot be preempted. 
   */ 
  if ((next_timeout <= realtime) || (proc_ptr->p_ticks_left <= 0) || expired) {
      prev_ptr = proc_ptr;			/* store running process */
      mini_notify(proc_addr(HARDWARE), CLOCK);		/* send notification */
  } 

  if (do_serial_debug)
	do_ser_debug();

  return(1);					/* reenable interrupts */
}

/*===========================================================================*
 *				get_uptime				     *
 *===========================================================================*/
PUBLIC clock_t get_uptime(void)
{
  /* Get and return the current clock uptime in ticks. */
  return(realtime);
}

/*===========================================================================*
 *				set_timer				     *
 *===========================================================================*/
PUBLIC void set_timer(tp, exp_time, watchdog)
struct timer *tp;		/* pointer to timer structure */
clock_t exp_time;		/* expiration realtime */
tmr_func_t watchdog;		/* watchdog to be called */
{
/* Insert the new timer in the active timers list. Always update the 
 * next timeout time by setting it to the front of the active list.
 */
  tmrs_settimer(&clock_timers, tp, exp_time, watchdog, NULL);
  next_timeout = clock_timers->tmr_exp_time;
}

/*===========================================================================*
 *				reset_timer				     *
 *===========================================================================*/
PUBLIC void reset_timer(tp)
struct timer *tp;		/* pointer to timer structure */
{
/* The timer pointed to by 'tp' is no longer needed. Remove it from both the
 * active and expired lists. Always update the next timeout time by setting
 * it to the front of the active list.
 */
  tmrs_clrtimer(&clock_timers, tp, NULL);
  next_timeout = (clock_timers == NULL) ? 
	TMR_NEVER : clock_timers->tmr_exp_time;
}

/*===========================================================================*
 *				load_update				     * 
 *===========================================================================*/
PRIVATE void load_update(void)
{
	u16_t slot;
	int enqueued = -1, q;	/* -1: special compensation for IDLE. */
	struct proc *p;

	/* Load average data is stored as a list of numbers in a circular
	 * buffer. Each slot accumulates _LOAD_UNIT_SECS of samples of
	 * the number of runnable processes. Computations can then
	 * be made of the load average over variable periods, in the
	 * user library (see getloadavg(3)).
	 */
	slot = (realtime / system_hz / _LOAD_UNIT_SECS) % _LOAD_HISTORY;
	if(slot != kloadinfo.proc_last_slot) {
		kloadinfo.proc_load_history[slot] = 0;
		kloadinfo.proc_last_slot = slot;
	}

	/* Cumulation. How many processes are ready now? */
	for(q = 0; q < NR_SCHED_QUEUES; q++)
		for(p = rdy_head[q]; p != NIL_PROC; p = p->p_nextready)
			enqueued++;

	kloadinfo.proc_load_history[slot] += enqueued;

	/* Up-to-dateness. */
	kloadinfo.last_clock = realtime;
}

