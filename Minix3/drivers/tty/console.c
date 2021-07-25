/* Code and data for the IBM console driver.
 *
 * The 6845 video controller used by the IBM PC shares its video memory with
 * the CPU somewhere in the 0xB0000 memory bank.  To the 6845 this memory
 * consists of 16-bit words.  Each word has a character code in the low byte
 * and a so-called attribute byte in the high byte.  The CPU directly modifies
 * video memory to display characters, and sets two registers on the 6845 that
 * specify the video origin and the cursor position.  The video origin is the
 * place in video memory where the first character (upper left corner) can
 * be found.  Moving the origin is a fast way to scroll the screen.  Some
 * video adapters wrap around the top of video memory, so the origin can
 * move without bounds.  For other adapters screen memory must sometimes be
 * moved to reset the origin.  All computations on video memory use character
 * (word) addresses for simplicity and assume there is no wrapping.  The
 * assembly support functions translate the word addresses to byte addresses
 * and the scrolling function worries about wrapping.
 */

#include "../drivers.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/vm.h>
#include <sys/video.h>
#include <sys/mman.h>
#include <minix/tty.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/sys_config.h>
#include <minix/vm.h>
#include "tty.h"

/* Set this to 1 if you want console output duplicated on the first
 * serial line.
  */
#define DUP_CONS_TO_SER	0

/* The clock task should provide an interface for this */
#define TIMER_FREQ  1193182L    /* clock frequency for timer in PC and AT */

/* Global variables used by the console driver and assembly support. */
PUBLIC phys_bytes vid_size;	/* 0x2000 for color or 0x0800 for mono */
PUBLIC phys_bytes vid_base;
PUBLIC unsigned vid_mask;	/* 0x1FFF for color or 0x07FF for mono */
PUBLIC unsigned blank_color = BLANK_COLOR; /* display code for blank */

/* Private variables used by the console driver. */
PRIVATE int vid_port;		/* I/O port for accessing 6845 */
PRIVATE int wrap;		/* hardware can wrap? */
PRIVATE int softscroll;		/* 1 = software scrolling, 0 = hardware */
PRIVATE int beeping;		/* speaker is beeping? */
PRIVATE unsigned font_lines;	/* font lines per character */
PRIVATE unsigned scr_width;	/* # characters on a line */
PRIVATE unsigned scr_lines;	/* # lines on the screen */
PRIVATE unsigned scr_size;	/* # characters on the screen */

/* tells mem_vid_copy() to blank the screen */
#define BLANK_MEM ((vir_bytes) 0) 

PRIVATE int disabled_vc = -1;	/* Virtual console that was active when 
				 * disable_console was called.
				 */
PRIVATE int disabled_sm;	/* Scroll mode to be restored when re-enabling
				 * console
				 */

char *console_memory = NULL;
char *font_memory = NULL;

/* Per console data. */
typedef struct console {
  tty_t *c_tty;			/* associated TTY struct */
  int c_column;			/* current column number (0-origin) */
  int c_row;			/* current row (0 at top of screen) */
  int c_rwords;			/* number of WORDS (not bytes) in outqueue */
  unsigned c_start;		/* start of video memory of this console */
  unsigned c_limit;		/* limit of this console's video memory */
  unsigned c_org;		/* location in RAM where 6845 base points */
  unsigned c_cur;		/* current position of cursor in video RAM */
  unsigned c_attr;		/* character attribute */
  unsigned c_blank;		/* blank attribute */
  char c_reverse;		/* reverse video */
  char c_esc_state;		/* 0=normal, 1=ESC, 2=ESC[ */
  char c_esc_intro;		/* Distinguishing character following ESC */
  int *c_esc_parmp;		/* pointer to current escape parameter */
  int c_esc_parmv[MAX_ESC_PARMS];	/* list of escape parameters */
  u16_t c_ramqueue[CONS_RAM_WORDS];	/* buffer for video RAM */
  int c_line;			/* line no */
} console_t;

#define UPDATE_CURSOR(ccons, cursor) {				\
	ccons->c_cur = cursor;					\
	if(curcons && ccons == curcons)				\
		set_6845(CURSOR, ccons->c_cur);			\
}

#define UPDATE_ORIGIN(ccons, origin) {				\
	ccons->c_org = origin;					\
  	if (curcons && ccons == curcons) 			\
		set_6845(VID_ORG, ccons->c_org);		\
}

PRIVATE int nr_cons= 1;		/* actual number of consoles */
PRIVATE console_t cons_table[NR_CONS];
PRIVATE console_t *curcons = NULL;	/* currently visible */

/* Color if using a color controller. */
#define color	(vid_port == C_6845)

/* Map from ANSI colors to the attributes used by the PC */
PRIVATE int ansi_colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Structure used for font management */
struct sequence {
	unsigned short index;
	unsigned char port;
	unsigned char value;
};

FORWARD _PROTOTYPE( int cons_write, (struct tty *tp, int try)		);
FORWARD _PROTOTYPE( void cons_echo, (tty_t *tp, int c)			);
FORWARD _PROTOTYPE( void out_char, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void cons_putk, (int c)				);
FORWARD _PROTOTYPE( void beep, (void)					);
FORWARD _PROTOTYPE( void do_escape, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void flush, (console_t *cons)			);
FORWARD _PROTOTYPE( void parse_escape, (console_t *cons, int c)		);
FORWARD _PROTOTYPE( void scroll_screen, (console_t *cons, int dir)	);
FORWARD _PROTOTYPE( void set_6845, (int reg, unsigned val)		);
FORWARD _PROTOTYPE( void stop_beep, (timer_t *tmrp)			);
FORWARD _PROTOTYPE( void cons_org0, (void)				);
FORWARD _PROTOTYPE( void disable_console, (void)			);
FORWARD _PROTOTYPE( void reenable_console, (void)			);
FORWARD _PROTOTYPE( int ga_program, (struct sequence *seq)		);
FORWARD _PROTOTYPE( int cons_ioctl, (tty_t *tp, int)			);
FORWARD _PROTOTYPE( void mem_vid_copy, (vir_bytes src, int dst, int count)	);
FORWARD _PROTOTYPE( void vid_vid_copy, (int src, int dst, int count)	);

#if 0
FORWARD _PROTOTYPE( void get_6845, (int reg, unsigned *val)		);
#endif

/*===========================================================================*
 *				cons_write				     *
 *===========================================================================*/
PRIVATE int cons_write(tp, try)
register struct tty *tp;	/* tells which terminal is to be used */
int try;
{
/* Copy as much data as possible to the output queue, then start I/O.  On
 * memory-mapped terminals, such as the IBM console, the I/O will also be
 * finished, and the counts updated.  Keep repeating until all I/O done.
 */

  int count;
  int result;
  register char *tbuf;
  char buf[64];
  console_t *cons = tp->tty_priv;

  if (try) return 1;	/* we can always write to console */

  /* Check quickly for nothing to do, so this can be called often without
   * unmodular tests elsewhere.
   */
  if ((count = tp->tty_outleft) == 0 || tp->tty_inhibited) return 0;

  /* Copy the user bytes to buf[] for decent addressing. Loop over the
   * copies, since the user buffer may be much larger than buf[].
   */
  do {
	if (count > sizeof(buf)) count = sizeof(buf);
	if(tp->tty_out_safe) {
	   if ((result = sys_safecopyfrom(tp->tty_outproc, tp->tty_out_vir_g,
		tp->tty_out_vir_offset, (vir_bytes) buf, count, D)) != OK)
		break;
	    tp->tty_out_vir_offset += count;
	} else {
	   if ((result = sys_vircopy(tp->tty_outproc, D, tp->tty_out_vir_g, 
			SELF, D, (vir_bytes) buf, (vir_bytes) count)) != OK)
		break;
	    tp->tty_out_vir_g += count;
	}
	tbuf = buf;

	/* Update terminal data structure. */
	tp->tty_outcum += count;
	tp->tty_outleft -= count;

	/* Output each byte of the copy to the screen.  Avoid calling
	 * out_char() for the "easy" characters, put them into the buffer
	 * directly.
	 */
	do {
		if ((unsigned) *tbuf < ' ' || cons->c_esc_state > 0
			|| cons->c_column >= scr_width
			|| cons->c_rwords >= buflen(cons->c_ramqueue))
		{
			out_char(cons, *tbuf++);
		} else {
#if DUP_CONS_TO_SER
			if (cons == &cons_table[0]) ser_putc(*tbuf);
#endif
			cons->c_ramqueue[cons->c_rwords++] =
					cons->c_attr | (*tbuf++ & BYTE);
			cons->c_column++;
		}
	} while (--count != 0);
  } while ((count = tp->tty_outleft) != 0 && !tp->tty_inhibited);

  flush(cons);			/* transfer anything buffered to the screen */

  /* Reply to the writer if all output is finished or if an error occured. */
  if (tp->tty_outleft == 0 || result != OK) {
	/* REVIVE is not possible. I/O on memory mapped consoles finishes. */
	tty_reply(tp->tty_outrepcode, tp->tty_outcaller, tp->tty_outproc,
							tp->tty_outcum);
	tp->tty_outcum = 0;
  }

  return 0;
}

/*===========================================================================*
 *				cons_echo				     *
 *===========================================================================*/
PRIVATE void cons_echo(tp, c)
register tty_t *tp;		/* pointer to tty struct */
int c;				/* character to be echoed */
{
/* Echo keyboard input (print & flush). */
  console_t *cons = tp->tty_priv;

  out_char(cons, c);
  flush(cons);
}

/*===========================================================================*
 *				out_char				     *
 *===========================================================================*/
PRIVATE void out_char(cons, c)
register console_t *cons;	/* pointer to console struct */
int c;				/* character to be output */
{
/* Output a character on the console.  Check for escape sequences first. */
  if (cons->c_esc_state > 0) {
	parse_escape(cons, c);
	return;
  }

#if DUP_CONS_TO_SER
  if (cons == &cons_table[0] && c != '\0')
  {
	if (c == '\n')
		ser_putc('\r');
	ser_putc(c);
  }
#endif

  switch(c) {
	case 000:		/* null is typically used for padding */
		return;		/* better not do anything */

	case 007:		/* ring the bell */
		flush(cons);	/* print any chars queued for output */
		beep();
		return;

	case '\b':		/* backspace */
		if (--cons->c_column < 0) {
			if (--cons->c_row >= 0) cons->c_column += scr_width;
		}
		flush(cons);
		return;

	case '\n':		/* line feed */
		if ((cons->c_tty->tty_termios.c_oflag & (OPOST|ONLCR))
						== (OPOST|ONLCR)) {
			cons->c_column = 0;
		}
		/*FALL THROUGH*/
	case 013:		/* CTRL-K */
	case 014:		/* CTRL-L */
		if (cons->c_row == scr_lines-1) {
			scroll_screen(cons, SCROLL_UP);
		} else {
			cons->c_row++;
		}
		flush(cons);
		return;

	case '\r':		/* carriage return */
		cons->c_column = 0;
		flush(cons);
		return;

	case '\t':		/* tab */
		cons->c_column = (cons->c_column + TAB_SIZE) & ~TAB_MASK;
		if (cons->c_column > scr_width) {
			cons->c_column -= scr_width;
			if (cons->c_row == scr_lines-1) {
				scroll_screen(cons, SCROLL_UP);
			} else {
				cons->c_row++;
			}
		}
		flush(cons);
		return;

	case 033:		/* ESC - start of an escape sequence */
		flush(cons);	/* print any chars queued for output */
		cons->c_esc_state = 1;	/* mark ESC as seen */
		return;

	default:		/* printable chars are stored in ramqueue */
		if (cons->c_column >= scr_width) {
			if (!LINEWRAP) return;
			if (cons->c_row == scr_lines-1) {
				scroll_screen(cons, SCROLL_UP);
			} else {
				cons->c_row++;
			}
			cons->c_column = 0;
			flush(cons);
		}
		if (cons->c_rwords == buflen(cons->c_ramqueue)) flush(cons);
		cons->c_ramqueue[cons->c_rwords++] = cons->c_attr | (c & BYTE);
		cons->c_column++;			/* next column */
		return;
  }
}

/*===========================================================================*
 *				scroll_screen				     *
 *===========================================================================*/
PRIVATE void scroll_screen(cons, dir)
register console_t *cons;	/* pointer to console struct */
int dir;			/* SCROLL_UP or SCROLL_DOWN */
{
  unsigned new_line, new_org, chars;

  flush(cons);
  chars = scr_size - scr_width;		/* one screen minus one line */

  /* Scrolling the screen is a real nuisance due to the various incompatible
   * video cards.  This driver supports software scrolling (Hercules?),
   * hardware scrolling (mono and CGA cards) and hardware scrolling without
   * wrapping (EGA cards).  In the latter case we must make sure that
   *		c_start <= c_org && c_org + scr_size <= c_limit
   * holds, because EGA doesn't wrap around the end of video memory.
   */
  if (dir == SCROLL_UP) {
	/* Scroll one line up in 3 ways: soft, avoid wrap, use origin. */
	if (softscroll) {
		vid_vid_copy(cons->c_start + scr_width, cons->c_start, chars);
	} else
	if (!wrap && cons->c_org + scr_size + scr_width >= cons->c_limit) {
		vid_vid_copy(cons->c_org + scr_width, cons->c_start, chars);
		UPDATE_ORIGIN(cons, cons->c_start);
	} else {
		UPDATE_ORIGIN(cons, (cons->c_org + scr_width) & vid_mask);
	}
	new_line = (cons->c_org + chars) & vid_mask;
  } else {
	/* Scroll one line down in 3 ways: soft, avoid wrap, use origin. */
	if (softscroll) {
		vid_vid_copy(cons->c_start, cons->c_start + scr_width, chars);
	} else
	if (!wrap && cons->c_org < cons->c_start + scr_width) {
		new_org = cons->c_limit - scr_size;
		vid_vid_copy(cons->c_org, new_org + scr_width, chars);
		UPDATE_ORIGIN(cons, new_org);
	} else {
		UPDATE_ORIGIN(cons, (cons->c_org - scr_width) & vid_mask);
	}
	new_line = cons->c_org;
  }
  /* Blank the new line at top or bottom. */
  blank_color = cons->c_blank;
  mem_vid_copy(BLANK_MEM, new_line, scr_width);

  flush(cons);
}

/*===========================================================================*
 *				flush					     *
 *===========================================================================*/
PRIVATE void flush(cons)
register console_t *cons;	/* pointer to console struct */
{
/* Send characters buffered in 'ramqueue' to screen memory, check the new
 * cursor position, compute the new hardware cursor position and set it.
 */
  unsigned cur;
  tty_t *tp = cons->c_tty;

  /* Have the characters in 'ramqueue' transferred to the screen. */
  if (cons->c_rwords > 0) {
	mem_vid_copy((vir_bytes) cons->c_ramqueue, cons->c_cur, cons->c_rwords);
	cons->c_rwords = 0;

	/* TTY likes to know the current column and if echoing messed up. */
	tp->tty_position = cons->c_column;
	tp->tty_reprint = TRUE;
  }

  /* Check and update the cursor position. */
  if (cons->c_column < 0) cons->c_column = 0;
  if (cons->c_column > scr_width) cons->c_column = scr_width;
  if (cons->c_row < 0) cons->c_row = 0;
  if (cons->c_row >= scr_lines) cons->c_row = scr_lines - 1;
  cur = cons->c_org + cons->c_row * scr_width + cons->c_column;
  if (cur != cons->c_cur)
	UPDATE_CURSOR(cons, cur);
}

/*===========================================================================*
 *				parse_escape				     *
 *===========================================================================*/
PRIVATE void parse_escape(cons, c)
register console_t *cons;	/* pointer to console struct */
char c;				/* next character in escape sequence */
{
/* The following ANSI escape sequences are currently supported.
 * If n and/or m are omitted, they default to 1.
 *   ESC [nA moves up n lines
 *   ESC [nB moves down n lines
 *   ESC [nC moves right n spaces
 *   ESC [nD moves left n spaces
 *   ESC [m;nH" moves cursor to (m,n)
 *   ESC [J clears screen from cursor
 *   ESC [K clears line from cursor
 *   ESC [nL inserts n lines ar cursor
 *   ESC [nM deletes n lines at cursor
 *   ESC [nP deletes n chars at cursor
 *   ESC [n@ inserts n chars at cursor
 *   ESC [nm enables rendition n (0=normal, 4=bold, 5=blinking, 7=reverse)
 *   ESC M scrolls the screen backwards if the cursor is on the top line
 */

  switch (cons->c_esc_state) {
    case 1:			/* ESC seen */
	cons->c_esc_intro = '\0';
	cons->c_esc_parmp = bufend(cons->c_esc_parmv);
	do {
		*--cons->c_esc_parmp = 0;
	} while (cons->c_esc_parmp > cons->c_esc_parmv);
	switch (c) {
	    case '[':	/* Control Sequence Introducer */
		cons->c_esc_intro = c;
		cons->c_esc_state = 2;
		break;
	    case 'M':	/* Reverse Index */
		do_escape(cons, c);
		break;
	    default:
		cons->c_esc_state = 0;
	}
	break;

    case 2:			/* ESC [ seen */
	if (c >= '0' && c <= '9') {
		if (cons->c_esc_parmp < bufend(cons->c_esc_parmv))
			*cons->c_esc_parmp = *cons->c_esc_parmp * 10 + (c-'0');
	} else
	if (c == ';') {
		if (cons->c_esc_parmp < bufend(cons->c_esc_parmv))
			cons->c_esc_parmp++;
	} else {
		do_escape(cons, c);
	}
	break;
  }
}

/*===========================================================================*
 *				do_escape				     *
 *===========================================================================*/
PRIVATE void do_escape(cons, c)
register console_t *cons;	/* pointer to console struct */
char c;				/* next character in escape sequence */
{
  int value, n;
  unsigned src, dst, count;
  int *parmp;

  /* Some of these things hack on screen RAM, so it had better be up to date */
  flush(cons);

  if (cons->c_esc_intro == '\0') {
	/* Handle a sequence beginning with just ESC */
	switch (c) {
	    case 'M':		/* Reverse Index */
		if (cons->c_row == 0) {
			scroll_screen(cons, SCROLL_DOWN);
		} else {
			cons->c_row--;
		}
		flush(cons);
		break;

	    default: break;
	}
  } else
  if (cons->c_esc_intro == '[') {
	/* Handle a sequence beginning with ESC [ and parameters */
	value = cons->c_esc_parmv[0];
	switch (c) {
	    case 'A':		/* ESC [nA moves up n lines */
		n = (value == 0 ? 1 : value);
		cons->c_row -= n;
		flush(cons);
		break;

	    case 'B':		/* ESC [nB moves down n lines */
		n = (value == 0 ? 1 : value);
		cons->c_row += n;
		flush(cons);
		break;

	    case 'C':		/* ESC [nC moves right n spaces */
		n = (value == 0 ? 1 : value);
		cons->c_column += n;
		flush(cons);
		break;

	    case 'D':		/* ESC [nD moves left n spaces */
		n = (value == 0 ? 1 : value);
		cons->c_column -= n;
		flush(cons);
		break;

	    case 'H':		/* ESC [m;nH" moves cursor to (m,n) */
		cons->c_row = cons->c_esc_parmv[0] - 1;
		cons->c_column = cons->c_esc_parmv[1] - 1;
		flush(cons);
		break;

	    case 'J':		/* ESC [sJ clears in display */
		switch (value) {
		    case 0:	/* Clear from cursor to end of screen */
			count = scr_size - (cons->c_cur - cons->c_org);
			dst = cons->c_cur;
			break;
		    case 1:	/* Clear from start of screen to cursor */
			count = cons->c_cur - cons->c_org;
			dst = cons->c_org;
			break;
		    case 2:	/* Clear entire screen */
			count = scr_size;
			dst = cons->c_org;
			break;
		    default:	/* Do nothing */
			count = 0;
			dst = cons->c_org;
		}
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, dst, count);
		break;

	    case 'K':		/* ESC [sK clears line from cursor */
		switch (value) {
		    case 0:	/* Clear from cursor to end of line */
			count = scr_width - cons->c_column;
			dst = cons->c_cur;
			break;
		    case 1:	/* Clear from beginning of line to cursor */
			count = cons->c_column;
			dst = cons->c_cur - cons->c_column;
			break;
		    case 2:	/* Clear entire line */
			count = scr_width;
			dst = cons->c_cur - cons->c_column;
			break;
		    default:	/* Do nothing */
			count = 0;
			dst = cons->c_cur;
		}
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, dst, count);
		break;

	    case 'L':		/* ESC [nL inserts n lines at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_lines - cons->c_row))
			n = scr_lines - cons->c_row;

		src = cons->c_org + cons->c_row * scr_width;
		dst = src + n * scr_width;
		count = (scr_lines - cons->c_row - n) * scr_width;
		vid_vid_copy(src, dst, count);
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, src, n * scr_width);
		break;

	    case 'M':		/* ESC [nM deletes n lines at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_lines - cons->c_row))
			n = scr_lines - cons->c_row;

		dst = cons->c_org + cons->c_row * scr_width;
		src = dst + n * scr_width;
		count = (scr_lines - cons->c_row - n) * scr_width;
		vid_vid_copy(src, dst, count);
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, dst + count, n * scr_width);
		break;

	    case '@':		/* ESC [n@ inserts n chars at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_width - cons->c_column))
			n = scr_width - cons->c_column;

		src = cons->c_cur;
		dst = src + n;
		count = scr_width - cons->c_column - n;
		vid_vid_copy(src, dst, count);
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, src, n);
		break;

	    case 'P':		/* ESC [nP deletes n chars at cursor */
		n = value;
		if (n < 1) n = 1;
		if (n > (scr_width - cons->c_column))
			n = scr_width - cons->c_column;

		dst = cons->c_cur;
		src = dst + n;
		count = scr_width - cons->c_column - n;
		vid_vid_copy(src, dst, count);
		blank_color = cons->c_blank;
		mem_vid_copy(BLANK_MEM, dst + count, n);
		break;

	    case 'm':		/* ESC [nm enables rendition n */
		for (parmp = cons->c_esc_parmv; parmp <= cons->c_esc_parmp
				&& parmp < bufend(cons->c_esc_parmv); parmp++) {
			if (cons->c_reverse) {
				/* Unswap fg and bg colors */
				cons->c_attr =	((cons->c_attr & 0x7000) >> 4) |
						((cons->c_attr & 0x0700) << 4) |
						((cons->c_attr & 0x8800));
			}
			switch (n = *parmp) {
			    case 0:	/* NORMAL */
				cons->c_attr = cons->c_blank = BLANK_COLOR;
				cons->c_reverse = FALSE;
				break;

			    case 1:	/* BOLD  */
				/* Set intensity bit */
				cons->c_attr |= 0x0800;
				break;

			    case 4:	/* UNDERLINE */
				if (color) {
					/* Change white to cyan, i.e. lose red
					 */
					cons->c_attr = (cons->c_attr & 0xBBFF);
				} else {
					/* Set underline attribute */
					cons->c_attr = (cons->c_attr & 0x99FF);
				}
				break;

			    case 5:	/* BLINKING */
				/* Set the blink bit */
				cons->c_attr |= 0x8000;
				break;

			    case 7:	/* REVERSE */
				cons->c_reverse = TRUE;
				break;

			    default:	/* COLOR */
				if (n == 39) n = 37;	/* set default color */
				if (n == 49) n = 40;

				if (!color) {
					/* Don't mess up a monochrome screen */
				} else
				if (30 <= n && n <= 37) {
					/* Foreground color */
					cons->c_attr =
						(cons->c_attr & 0xF8FF) |
						(ansi_colors[(n - 30)] << 8);
					cons->c_blank =
						(cons->c_blank & 0xF8FF) |
						(ansi_colors[(n - 30)] << 8);
				} else
				if (40 <= n && n <= 47) {
					/* Background color */
					cons->c_attr =
						(cons->c_attr & 0x8FFF) |
						(ansi_colors[(n - 40)] << 12);
					cons->c_blank =
						(cons->c_blank & 0x8FFF) |
						(ansi_colors[(n - 40)] << 12);
				}
			}
			if (cons->c_reverse) {
				/* Swap fg and bg colors */
				cons->c_attr =	((cons->c_attr & 0x7000) >> 4) |
						((cons->c_attr & 0x0700) << 4) |
						((cons->c_attr & 0x8800));
			}
		}
		break;
	}
  }
  cons->c_esc_state = 0;
}

/*===========================================================================*
 *				set_6845				     *
 *===========================================================================*/
PRIVATE void set_6845(reg, val)
int reg;			/* which register pair to set */
unsigned val;			/* 16-bit value to set it to */
{
/* Set a register pair inside the 6845.
 * Registers 12-13 tell the 6845 where in video ram to start
 * Registers 14-15 tell the 6845 where to put the cursor
 */
  pvb_pair_t char_out[4];
  pv_set(char_out[0], vid_port + INDEX, reg);	/* set index register */
  pv_set(char_out[1], vid_port + DATA, (val>>8) & BYTE);    /* high byte */
  pv_set(char_out[2], vid_port + INDEX, reg + 1);	    /* again */
  pv_set(char_out[3], vid_port + DATA, val&BYTE);	    /* low byte */
  sys_voutb(char_out, 4);			/* do actual output */
}

#if 0
/*===========================================================================*
 *				get_6845				     *
 *===========================================================================*/
PRIVATE void get_6845(reg, val)
int reg;			/* which register pair to set */
unsigned *val;			/* 16-bit value to set it to */
{
  char v1, v2;
  unsigned long v;
/* Get a register pair inside the 6845.  */
  sys_outb(vid_port + INDEX, reg); 
  sys_inb(vid_port + DATA, &v); 
  v1 = v;
  sys_outb(vid_port + INDEX, reg+1); 
  sys_inb(vid_port + DATA, &v); 
  v2 = v;
  *val = (v1 << 8) | v2;
}
#endif

/*===========================================================================*
 *				beep					     *
 *===========================================================================*/
PRIVATE void beep()
{
/* Making a beeping sound on the speaker (output for CRTL-G).
 * This routine works by turning on the bits 0 and 1 in port B of the 8255
 * chip that drive the speaker.
 */
  static timer_t tmr_stop_beep;
  pvb_pair_t char_out[3];
  clock_t now;
  unsigned long port_b_val;
  int s;
  
  /* Fetch current time in advance to prevent beeping delay. */
  if ((s=getuptime(&now)) != OK)
  	panic("TTY","Console couldn't get clock's uptime.", s);
  if (!beeping) {
	/* Set timer channel 2, square wave, with given frequency. */
        pv_set(char_out[0], TIMER_MODE, 0xB6);	
        pv_set(char_out[1], TIMER2, (BEEP_FREQ >> 0) & BYTE);
        pv_set(char_out[2], TIMER2, (BEEP_FREQ >> 8) & BYTE);
        if (sys_voutb(char_out, 3)==OK) {
        	if (sys_inb(PORT_B, &port_b_val)==OK &&
        	    sys_outb(PORT_B, (port_b_val|3))==OK)
        	    	beeping = TRUE;
        }
  }
  /* Add a timer to the timers list. Possibly reschedule the alarm. */
  tmrs_settimer(&tty_timers, &tmr_stop_beep, now+B_TIME, stop_beep, NULL);
  if (tty_timers->tmr_exp_time != tty_next_timeout) {
  	tty_next_timeout = tty_timers->tmr_exp_time;
  	if ((s=sys_setalarm(tty_next_timeout, 1)) != OK)
  		panic("TTY","Console couldn't set alarm.", s);
  }
}


/*===========================================================================*
 *				do_video				     *
 *===========================================================================*/
PUBLIC void do_video(message *m)
{
	int r, safe = 0;

	/* Execute the requested device driver function. */
	r= EINVAL;	/* just in case */
	switch (m->m_type) {
	    case DEV_OPEN:
		/* Should grant IOPL */
		disable_console();
		r= OK;
		break;
	    case DEV_CLOSE:
		reenable_console();
		r= OK;
		break;
	    case DEV_IOCTL_S:
		safe=1;
		switch(m->TTY_REQUEST) {
		  case TIOCMAPMEM:
		  case TIOCUNMAPMEM: {
			int r, do_map;
			struct mapreqvm mapreqvm;
			void *result;

			do_map= (m->REQUEST == TIOCMAPMEM);	/* else unmap */

			/* Get request structure */
			if(!safe) {
				printf("tty: safecopy only\n");
				return;
			}

	   		r = sys_safecopyfrom(m->IO_ENDPT,
			  (vir_bytes)m->ADDRESS, 0, (vir_bytes) &mapreqvm,
			  sizeof(mapreqvm), D);

			if (r != OK)
			{
				printf("tty: sys_safecopyfrom failed\n");
				tty_reply(TASK_REPLY, m->m_source, m->IO_ENDPT,
					r);
				return;
			}

			/* In safe ioctl mode, the POSITION field contains
			 * the endpt number of the original requestor.
			 * IO_ENDPT is always FS.
			 */

			if(do_map) {
				mapreqvm.vaddr_ret = vm_map_phys(m->POSITION,
				(void *) mapreqvm.phys_offset, mapreqvm.size);
	   			if((r = sys_safecopyto(m->IO_ENDPT,
				  (vir_bytes)m->ADDRESS, 0,
				  (vir_bytes) &mapreqvm,
				  sizeof(mapreqvm), D)) != OK) {
				  printf("tty: sys_safecopyto failed\n");
				}
			} else {
				r = vm_unmap_phys(m->POSITION, 
					mapreqvm.vaddr, mapreqvm.size);
			}
			tty_reply(TASK_REPLY, m->m_source, m->IO_ENDPT, r);
			return;
		   }
		}
		r= ENOTTY;
		break;

	    default:		
		printf(
		"Warning, TTY(video) got unexpected request %d from %d\n",
			m->m_type, m->m_source);
		r= EINVAL;
	}
	tty_reply(TASK_REPLY, m->m_source, m->IO_ENDPT, r);
}


/*===========================================================================*
 *				beep_x					     *
 *===========================================================================*/
PUBLIC void beep_x(freq, dur)
unsigned freq;
clock_t dur;
{
/* Making a beeping sound on the speaker.
 * This routine works by turning on the bits 0 and 1 in port B of the 8255
 * chip that drive the speaker.
 */
  static timer_t tmr_stop_beep;
  pvb_pair_t char_out[3];
  clock_t now;
  unsigned long port_b_val;
  int s;
  
  unsigned long ival= TIMER_FREQ / freq;
  if (ival == 0 || ival > 0xffff)
	return;	/* Frequency out of range */

  /* Fetch current time in advance to prevent beeping delay. */
  if ((s=getuptime(&now)) != OK)
  	panic("TTY","Console couldn't get clock's uptime.", s);
  if (!beeping) {
	/* Set timer channel 2, square wave, with given frequency. */
        pv_set(char_out[0], TIMER_MODE, 0xB6);	
        pv_set(char_out[1], TIMER2, (ival >> 0) & BYTE);
        pv_set(char_out[2], TIMER2, (ival >> 8) & BYTE);
        if (sys_voutb(char_out, 3)==OK) {
        	if (sys_inb(PORT_B, &port_b_val)==OK &&
        	    sys_outb(PORT_B, (port_b_val|3))==OK)
        	    	beeping = TRUE;
        }
  }
  /* Add a timer to the timers list. Possibly reschedule the alarm. */
  tmrs_settimer(&tty_timers, &tmr_stop_beep, now+dur, stop_beep, NULL);
  if (tty_timers->tmr_exp_time != tty_next_timeout) {
  	tty_next_timeout = tty_timers->tmr_exp_time;
  	if ((s=sys_setalarm(tty_next_timeout, 1)) != OK)
  		panic("TTY","Console couldn't set alarm.", s);
  }
}

/*===========================================================================*
 *				stop_beep				     *
 *===========================================================================*/
PRIVATE void stop_beep(tmrp)
timer_t *tmrp;
{
/* Turn off the beeper by turning off bits 0 and 1 in PORT_B. */
  unsigned long port_b_val;
  if (sys_inb(PORT_B, &port_b_val)==OK && 
	sys_outb(PORT_B, (port_b_val & ~3))==OK)
		beeping = FALSE;
}

/*===========================================================================*
 *				scr_init				     *
 *===========================================================================*/
PUBLIC void scr_init(tp)
tty_t *tp;
{
/* Initialize the screen driver. */
  console_t *cons;
  u16_t bios_columns, bios_crtbase, bios_fontlines;
  u8_t bios_rows;
  int line;
  int s;
  static int vdu_initialized = 0;
  static unsigned page_size;

  /* Associate console and TTY. */
  line = tp - &tty_table[0];
  if (line >= nr_cons) return;
  cons = &cons_table[line];
  cons->c_tty = tp;
  cons->c_line = line;
  tp->tty_priv = cons;

  /* Fill in TTY function hooks. */
  tp->tty_devwrite = cons_write;
  tp->tty_echo = cons_echo;
  tp->tty_ioctl = cons_ioctl;

  /* Get the BIOS parameters that describe the VDU. */
  if (! vdu_initialized++) {

	/* How about error checking? What to do on failure??? */
  	s=sys_readbios(VDU_SCREEN_COLS_ADDR, &bios_columns,
		VDU_SCREEN_COLS_SIZE);
  	s=sys_readbios(VDU_CRT_BASE_ADDR, &bios_crtbase,
		VDU_CRT_BASE_SIZE);
  	s=sys_readbios( VDU_SCREEN_ROWS_ADDR, &bios_rows,
		VDU_SCREEN_ROWS_SIZE);
  	s=sys_readbios(VDU_FONTLINES_ADDR, &bios_fontlines,
		VDU_FONTLINES_SIZE);

  	vid_port = bios_crtbase;
  	scr_width = bios_columns;
  	font_lines = bios_fontlines;
  	scr_lines = machine.vdu_ega ? bios_rows+1 : 25;

  	if (color) {
		vid_base = COLOR_BASE;
		vid_size = COLOR_SIZE;
  	} else {
		vid_base = MONO_BASE;
		vid_size = MONO_SIZE;
  	}
  	if (machine.vdu_ega) vid_size = EGA_SIZE;
  	wrap = ! machine.vdu_ega;

	console_memory = vm_map_phys(SELF, (void *) vid_base, vid_size);

	if(console_memory == MAP_FAILED) 
  		panic("TTY","Console couldn't map video memory", NO_NUM);

	font_memory = vm_map_phys(SELF, (void *)GA_VIDEO_ADDRESS, GA_FONT_SIZE);

	if(font_memory == MAP_FAILED) 
  		panic("TTY","Console couldn't map font memory", NO_NUM);


  	vid_size >>= 1;		/* word count */
  	vid_mask = vid_size - 1;

  	/* Size of the screen (number of displayed characters.) */
  	scr_size = scr_lines * scr_width;

  	/* There can be as many consoles as video memory allows. */
  	nr_cons = vid_size / scr_size;

  	if (nr_cons > NR_CONS) nr_cons = NR_CONS;
  	if (nr_cons > 1) wrap = 0;
  	page_size = vid_size / nr_cons;
  }

  cons->c_start = line * page_size;
  cons->c_limit = cons->c_start + page_size;
  cons->c_cur = cons->c_org = cons->c_start;
  cons->c_attr = cons->c_blank = BLANK_COLOR;

  if (line != 0) {
        /* Clear the non-console vtys. */
  	blank_color = BLANK_COLOR;
	mem_vid_copy(BLANK_MEM, cons->c_start, scr_size);
  } else {
	/* Set the cursor of the console vty at the bottom. c_cur
	 * is updated automatically later.
	 */
	scroll_screen(cons, SCROLL_UP);
	cons->c_row = scr_lines - 1;
	cons->c_column = 0;
  }
  select_console(0);
  cons_ioctl(tp, 0);
}

/*===========================================================================*
 *				kputc					     *
 *===========================================================================*/
PUBLIC void kputc(c)
int c;
{
/* Accumulate a single character for a kernel message. Send a notification
 * the to output driver if an END_OF_KMESS is encountered. 
 */
#if 0
  ser_putc(c);
  return;
#endif

#if 0
  if (panicing)
#endif
	cons_putk(c);
  if (c != 0) {
      kmess.km_buf[kmess.km_next] = c;	/* put normal char in buffer */
      if (kmess.km_size < _KMESS_BUF_SIZE)
          kmess.km_size += 1;		
      kmess.km_next = (kmess.km_next + 1) % _KMESS_BUF_SIZE;
  } else {
      notify(LOG_PROC_NR);
  }
}

/*===========================================================================*
 *				do_new_kmess				     *
 *===========================================================================*/
PUBLIC void do_new_kmess(m)
message *m;
{
/* Notification for a new kernel message. */
  static struct kmessages kmess;		/* kmessages structure */
  static int prev_next = 0;			/* previous next seen */
  int bytes;
  int r;

  /* Try to get a fresh copy of the buffer with kernel messages. */
#if DEAD_CODE	
  /* During shutdown, the reply is garbled because new notifications arrive
   * while the system task makes a copy of the kernel messages buffer.
   * Hence, don't check the return value. 
   */
  if ((r=sys_getkmessages(&kmess)) != OK) {
  	printf("TTY: couldn't get copy of kmessages: %d, 0x%x\n", r,r);
  	return;
  }
#endif
  sys_getkmessages(&kmess);

  /* Print only the new part. Determine how many new bytes there are with 
   * help of the current and previous 'next' index. Note that the kernel
   * buffer is circular. This works fine if less then _KMESS_BUF_SIZE bytes
   * is new data; else we miss % _KMESS_BUF_SIZE here.  
   * Check for size being positive, the buffer might as well be emptied!
   */
  if (kmess.km_size > 0) {
      bytes = ((kmess.km_next + _KMESS_BUF_SIZE) - prev_next) % _KMESS_BUF_SIZE;
      r=prev_next;				/* start at previous old */ 
      while (bytes > 0) {			
          cons_putk( kmess.km_buf[(r%_KMESS_BUF_SIZE)] );
          bytes --;
          r ++;
      }
      cons_putk(0);			/* terminate to flush output */
  }

  /* Almost done, store 'next' so that we can determine what part of the
   * kernel messages buffer to print next time a notification arrives.
   */
  prev_next = kmess.km_next;
}

/*===========================================================================*
 *				do_diagnostics				     *
 *===========================================================================*/
PUBLIC void do_diagnostics(m_ptr, safe)
message *m_ptr;			/* pointer to request message */
int safe;
{
/* Print a string for a server. */
  char c;
  vir_bytes src;
  int count, offset = 0;
  int result = OK;
  int proc_nr = m_ptr->m_source;

  src = (vir_bytes) m_ptr->DIAG_PRINT_BUF_G;
  for (count = m_ptr->DIAG_BUF_COUNT; count > 0; count--) {
	int r;
	if(safe) {
	   r = sys_safecopyfrom(proc_nr, src, offset, (vir_bytes) &c, 1, D);
	   if(r != OK)
	  	   printf("<tty: proc %d, grant %ld>", proc_nr, src);
	} else {
	   r = sys_vircopy(proc_nr, D, src+offset, SELF, D, (vir_bytes) &c, 1);
	}
	offset++;
	if(r != OK) {
		result = EFAULT;
		break;
	}
	cons_putk(c);
  }
  cons_putk(0);			/* always terminate, even with EFAULT */

  if(m_ptr->m_type != ASYN_DIAGNOSTICS_OLD) {
	  m_ptr->m_type = DIAG_REPL_OLD;
	  m_ptr->REP_STATUS = result;
	  send(m_ptr->m_source, m_ptr);
  }
}

/*===========================================================================*
 *				do_get_kmess				     *
 *===========================================================================*/
PUBLIC void do_get_kmess(m_ptr)
message *m_ptr;			/* pointer to request message */
{
/* Provide the log device with debug output */
  vir_bytes dst;
  int r;

  dst = (vir_bytes) m_ptr->GETKM_PTR;
  r= OK;
  if (sys_vircopy(SELF, D, (vir_bytes)&kmess, m_ptr->m_source, D,
	dst, sizeof(kmess)) != OK) {
	r = EFAULT;
  }
  m_ptr->m_type = r;
  send(m_ptr->m_source, m_ptr);
}

/*===========================================================================*
 *				do_get_kmess_s				     *
 *===========================================================================*/
PUBLIC void do_get_kmess_s(m_ptr)
message *m_ptr;			/* pointer to request message */
{
/* Provide the log device with debug output */
  cp_grant_id_t gid;
  int r;

  gid = m_ptr->GETKM_GRANT;
  r= OK;
  if (sys_safecopyto(m_ptr->m_source, gid, 0, (vir_bytes)&kmess, sizeof(kmess),
	D) != OK) {
	r = EFAULT;
  }
  m_ptr->m_type = r;
  send(m_ptr->m_source, m_ptr);
}

/*===========================================================================*
 *				cons_putk				     *
 *===========================================================================*/
PRIVATE void cons_putk(c)
int c;				/* character to print */
{
/* This procedure is used to print a character on the console.
 */
  if (c != 0) {
	if (c == '\n') cons_putk('\r');
	out_char(&cons_table[0], (int) c);
#if 0
	ser_putc(c);
#endif
  } else {
	flush(&cons_table[0]);
  }
}

/*===========================================================================*
 *				toggle_scroll				     *
 *===========================================================================*/
PUBLIC void toggle_scroll()
{
/* Toggle between hardware and software scroll. */

  cons_org0();
  softscroll = !softscroll;
  printf("%sware scrolling enabled.\n", softscroll ? "Soft" : "Hard");
}

/*===========================================================================*
 *				cons_stop				     *
 *===========================================================================*/
PUBLIC void cons_stop()
{
/* Prepare for halt or reboot. */
  select_console(0);
#if 0
  cons_org0();
  softscroll = 1;
  cons_table[0].c_attr = cons_table[0].c_blank = BLANK_COLOR;
#endif
}

/*===========================================================================*
 *				cons_org0				     *
 *===========================================================================*/
PRIVATE void cons_org0()
{
/* Scroll video memory back to put the origin at 0. */
  int cons_line;
  console_t *cons;
  unsigned n;

  for (cons_line = 0; cons_line < nr_cons; cons_line++) {
	cons = &cons_table[cons_line];
	while (cons->c_org > cons->c_start) {
		n = vid_size - scr_size;	/* amount of unused memory */
		if (n > cons->c_org - cons->c_start)
			n = cons->c_org - cons->c_start;
		vid_vid_copy(cons->c_org, cons->c_org - n, scr_size);
		UPDATE_ORIGIN(cons, cons->c_org - n);
	}
	flush(cons);
  }
  select_console(ccurrent);
}

/*===========================================================================*
 *				disable_console				     *
 *===========================================================================*/
PRIVATE void disable_console()
{
	if (disabled_vc != -1)
		return;
	
	disabled_vc = ccurrent;
	disabled_sm = softscroll;

	cons_org0();
	softscroll = 1;
	select_console(0);

	/* Should also disable further output to virtual consoles */
}

/*===========================================================================*
 *				reenable_console			     *
 *===========================================================================*/
PRIVATE void reenable_console()
{
	if (disabled_vc == -1)
		return;

	softscroll = disabled_sm;
	select_console(disabled_vc);
	disabled_vc = -1;
}

/*===========================================================================*
 *				select_console				     *
 *===========================================================================*/
PUBLIC void select_console(int cons_line)
{
/* Set the current console to console number 'cons_line'. */

  if (cons_line < 0 || cons_line >= nr_cons) return;

  ccurrent = cons_line;
  curcons = &cons_table[cons_line];

  UPDATE_CURSOR(curcons, curcons->c_cur);
  UPDATE_ORIGIN(curcons, curcons->c_org);
}

/*===========================================================================*
 *				con_loadfont				     *
 *===========================================================================*/
PUBLIC int con_loadfont(m)
message *m;
{
  
/* Load a font into the EGA or VGA adapter. */
  int result;
  static struct sequence seq1[7] = {
	{ GA_SEQUENCER_INDEX, 0x00, 0x01 },
	{ GA_SEQUENCER_INDEX, 0x02, 0x04 },
	{ GA_SEQUENCER_INDEX, 0x04, 0x07 },
	{ GA_SEQUENCER_INDEX, 0x00, 0x03 },
	{ GA_GRAPHICS_INDEX, 0x04, 0x02 },
	{ GA_GRAPHICS_INDEX, 0x05, 0x00 },
	{ GA_GRAPHICS_INDEX, 0x06, 0x00 },
  };
  static struct sequence seq2[7] = {
	{ GA_SEQUENCER_INDEX, 0x00, 0x01 },
	{ GA_SEQUENCER_INDEX, 0x02, 0x03 },
	{ GA_SEQUENCER_INDEX, 0x04, 0x03 },
	{ GA_SEQUENCER_INDEX, 0x00, 0x03 },
	{ GA_GRAPHICS_INDEX, 0x04, 0x00 },
	{ GA_GRAPHICS_INDEX, 0x05, 0x10 },
	{ GA_GRAPHICS_INDEX, 0x06,    0 },
  };

  seq2[6].value= color ? 0x0E : 0x0A;

  if (!machine.vdu_ega) return(ENOTTY);
  result = ga_program(seq1);	/* bring font memory into view */

  if(sys_safecopyfrom(m->IO_ENDPT, (cp_grant_id_t) m->ADDRESS, 0,
	(vir_bytes) font_memory, GA_FONT_SIZE, D) != OK) {
	printf("tty: copying from %d failed\n", m->IO_ENDPT);
	return EFAULT;
  }

  result = ga_program(seq2);	/* restore */

  return(result);
}

/*===========================================================================*
 *				ga_program				     *
 *===========================================================================*/
PRIVATE int ga_program(seq)
struct sequence *seq;
{
  pvb_pair_t char_out[14];
  int i;
  for (i=0; i<7; i++) {
      pv_set(char_out[2*i], seq->index, seq->port);
      pv_set(char_out[2*i+1], seq->index+1, seq->value);
      seq++;
  } 
  return sys_voutb(char_out, 14);
}

/*===========================================================================*
 *				cons_ioctl				     *
 *===========================================================================*/
PRIVATE int cons_ioctl(tp, try)
tty_t *tp;
int try;
{
/* Set the screen dimensions. */

  tp->tty_winsize.ws_row= scr_lines;
  tp->tty_winsize.ws_col= scr_width;
  tp->tty_winsize.ws_xpixel= scr_width * 8;
  tp->tty_winsize.ws_ypixel= scr_lines * font_lines;

  return 0;
}

#define LIMITINDEX(mask, start, size, ct) { 	\
	int countlimit = size - start;		\
	start &= mask;				\
	if(ct > countlimit) ct = countlimit;	\
}

/*===========================================================================*
 *				mem_vid_copy				     *
 *===========================================================================*/
PRIVATE void mem_vid_copy(vir_bytes src, int dst_index, int count)
{
	u16_t *src_mem = (u16_t *) src;
	while(count > 0) {
		int i, subcount = count;
		u16_t *dst_mem;
		LIMITINDEX(vid_mask, dst_index, vid_size, subcount);
		dst_mem = (u16_t *) console_memory + dst_index;
		if(!src)
			for(i = 0; i < subcount; i++)
				*dst_mem++ = blank_color;
		else
			for(i = 0; i < subcount; i++)
				*dst_mem++ = *src_mem++;
		count -= subcount;
		dst_index += subcount;
	}
}

/*===========================================================================*
 *				vid_vid_copy				     *
 *===========================================================================*/
PRIVATE void vid_vid_copy(int src_index, int dst_index, int count)
{
	int backwards = 0;
	if(src_index < dst_index)
		backwards = 1;
	while(count > 0) {
		int i, subcount = count;
		u16_t *dst_mem, *src_mem;
		LIMITINDEX(vid_mask, src_index, vid_size, subcount);
		LIMITINDEX(vid_mask, dst_index, vid_size, subcount);
		src_mem = (u16_t *) console_memory + src_index;
		dst_mem = (u16_t *) console_memory + dst_index;
		if(backwards) {
			src_mem += subcount - 1;
			dst_mem += subcount - 1;
			for(i = 0; i < subcount; i++)
				*dst_mem-- = *src_mem--;
		} else {
			for(i = 0; i < subcount; i++)
				*dst_mem++ = *src_mem++;
		}
		count -= subcount;
		dst_index += subcount;
		src_index += subcount;
	}
}
