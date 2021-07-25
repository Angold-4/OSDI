/* The kernel call implemented in this file:
 *   m_type:	SYS_RUNCTL
 *
 * The parameters for this kernel call are:
 *    m1_i1:	RC_ENDPT	process number to control
 *    m1_i2:	RC_ACTION	stop or resume the process
 *    m1_i3:	RC_FLAGS	request flags
 */

#include "../system.h"
#include <minix/type.h>

#if USE_RUNCTL

/*===========================================================================*
 *				  do_runctl				     *
 *===========================================================================*/
PUBLIC int do_runctl(message *m_ptr)
{
/* Control a process's PROC_STOP flag. Used for process management.
 * If the process is queued sending a message or stopped for system call
 * tracing, and the RC_DELAY request flag is given, set MF_SIG_DELAY instead
 * of PROC_STOP, and send a SIGNDELAY signal later when the process is done
 * sending (ending the delay). Used by PM for safe signal delivery.
 */
  int proc_nr, action, flags, delayed;
  register struct proc *rp;

  /* Extract the message parameters and do sanity checking. */
  if (!isokendpt(m_ptr->RC_ENDPT, &proc_nr)) return(EINVAL);
  if (iskerneln(proc_nr)) return(EPERM);
  rp = proc_addr(proc_nr);

  action = m_ptr->RC_ACTION;
  flags = m_ptr->RC_FLAGS;

  /* Is the target sending or syscall-traced? Then set MF_SIG_DELAY instead.
   * Do this only when the RC_DELAY flag is set in the request flags field.
   * The process will not become runnable before PM has called SYS_ENDKSIG.
   * Note that asynchronous messages are not covered: a process using SENDA
   * should not also install signal handlers *and* expect POSIX compliance.
   */
  if (action == RC_STOP && (flags & RC_DELAY)) {
	RTS_LOCK_SET(rp, SYS_LOCK);

	if (RTS_ISSET(rp, SENDING) || (rp->p_misc_flags & MF_SC_DEFER))
		rp->p_misc_flags |= MF_SIG_DELAY;

	delayed = (rp->p_misc_flags & MF_SIG_DELAY);

	RTS_LOCK_UNSET(rp, SYS_LOCK);

	if (delayed) return(EBUSY);
  }

  /* Either set or clear the stop flag. */
  switch (action) {
  case RC_STOP:
	RTS_LOCK_SET(rp, PROC_STOP);
	break;
  case RC_RESUME:
	RTS_LOCK_UNSET(rp, PROC_STOP);
	break;
  default:
	return(EINVAL);
  }

  return(OK);
}

#endif /* USE_RUNCTL */

