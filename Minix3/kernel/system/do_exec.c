/* The kernel call implemented in this file:
 *   m_type:	SYS_EXEC
 *
 * The parameters for this kernel call are:
 *    m1_i1:	PR_ENDPT  		(process that did exec call)
 *    m1_p1:	PR_STACK_PTR		(new stack pointer)
 *    m1_p2:	PR_NAME_PTR		(pointer to program name)
 *    m1_p3:	PR_IP_PTR		(new instruction pointer)
 */
#include "../system.h"
#include <string.h>
#include <signal.h>
#include <minix/endpoint.h>

#if USE_EXEC

/*===========================================================================*
 *				do_exec					     *
 *===========================================================================*/
PUBLIC int do_exec(m_ptr)
register message *m_ptr;	/* pointer to request message */
{
/* Handle sys_exec().  A process has done a successful EXEC. Patch it up. */
  register struct proc *rp;
  phys_bytes phys_name;
  char *np;
  int proc_nr;

  if(!isokendpt(m_ptr->PR_ENDPT, &proc_nr))
	return EINVAL;

  rp = proc_addr(proc_nr);

  if(rp->p_misc_flags & MF_DELIVERMSG) {
	rp->p_misc_flags &= ~MF_DELIVERMSG;
	rp->p_delivermsg_lin = 0;
  }

  /* Save command name for debugging, ps(1) output, etc. */
  if(data_copy(who_e, (vir_bytes) m_ptr->PR_NAME_PTR,
	SYSTEM, (vir_bytes) rp->p_name, (phys_bytes) P_NAME_LEN - 1) != OK)
  	strncpy(rp->p_name, "<unset>", P_NAME_LEN);

  /* Do architecture-specific exec() stuff. */
  arch_pre_exec(rp, (u32_t) m_ptr->PR_IP_PTR, (u32_t) m_ptr->PR_STACK_PTR);

  /* No reply to EXEC call */
  RTS_LOCK_UNSET(rp, RECEIVING);

  return(OK);
}
#endif /* USE_EXEC */

