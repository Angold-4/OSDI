/* The kernel call implemented in this file:
 *   m_type:	SYS_IRQCTL
 *
 * The parameters for this kernel call are:
 *    m5_c1:	IRQ_REQUEST	(control operation to perform)	
 *    m5_c2:	IRQ_VECTOR	(irq line that must be controlled)
 *    m5_i1:	IRQ_POLICY	(irq policy allows reenabling interrupts)
 *    m5_l3:	IRQ_HOOK_ID	(provides index to be returned on interrupt)
 *      ,,          ,,          (returns index of irq hook assigned at kernel)
 */

#include "../system.h"

#include <minix/endpoint.h>

#if USE_IRQCTL

FORWARD _PROTOTYPE(int generic_handler, (irq_hook_t *hook));

/*===========================================================================*
 *				do_irqctl				     *
 *===========================================================================*/
PUBLIC int do_irqctl(m_ptr)
register message *m_ptr;	/* pointer to request message */
{
  /* Dismember the request message. */
  int irq_vec;
  int irq_hook_id;
  int notify_id;
  int r = OK;
  int i;
  irq_hook_t *hook_ptr;
  struct proc *rp;
  struct priv *privp;

  /* Hook identifiers start at 1 and end at NR_IRQ_HOOKS. */
  irq_hook_id = (unsigned) m_ptr->IRQ_HOOK_ID - 1;
  irq_vec = (unsigned) m_ptr->IRQ_VECTOR; 

  /* See what is requested and take needed actions. */
  switch(m_ptr->IRQ_REQUEST) {

  /* Enable or disable IRQs. This is straightforward. */
  case IRQ_ENABLE:           
  case IRQ_DISABLE: 
      if (irq_hook_id >= NR_IRQ_HOOKS || irq_hook_id < 0 ||
          irq_hooks[irq_hook_id].proc_nr_e == NONE) return(EINVAL);
      if (irq_hooks[irq_hook_id].proc_nr_e != m_ptr->m_source) return(EPERM);
      if (m_ptr->IRQ_REQUEST == IRQ_ENABLE) {
          enable_irq(&irq_hooks[irq_hook_id]);	
      }
      else 
          disable_irq(&irq_hooks[irq_hook_id]);	
      break;

  /* Control IRQ policies. Set a policy and needed details in the IRQ table.
   * This policy is used by a generic function to handle hardware interrupts. 
   */
  case IRQ_SETPOLICY:  

      /* Check if IRQ line is acceptable. */
      if (irq_vec < 0 || irq_vec >= NR_IRQ_VECTORS) return(EINVAL);

      rp= proc_addr(who_p);
      privp= priv(rp);
      if (!privp)
      {
	kprintf("do_irqctl: no priv structure!\n");
	return EPERM;
      }
      if (privp->s_flags & CHECK_IRQ)
      {
	for (i= 0; i<privp->s_nr_irq; i++)
	{
		if (irq_vec == privp->s_irq_tab[i])
			break;
	}
	if (i >= privp->s_nr_irq)
	{
		kprintf(
		"do_irqctl: IRQ check failed for proc %d, IRQ %d\n",
			m_ptr->m_source, irq_vec);
		return EPERM;
	}
    }

      /* Find a free IRQ hook for this mapping. */
      hook_ptr = NULL;
      for (irq_hook_id=0; irq_hook_id<NR_IRQ_HOOKS; irq_hook_id++) {
          if (irq_hooks[irq_hook_id].proc_nr_e == NONE) {	
              hook_ptr = &irq_hooks[irq_hook_id];	/* free hook */
              break;
          }
      }
      if (hook_ptr == NULL) return(ENOSPC);

      /* When setting a policy, the caller must provide an identifier that
       * is returned on the notification message if a interrupt occurs.
       */
      notify_id = (unsigned) m_ptr->IRQ_HOOK_ID;
      if (notify_id > CHAR_BIT * sizeof(irq_id_t) - 1) return(EINVAL);

      /* Install the handler. */
      hook_ptr->proc_nr_e = m_ptr->m_source;	/* process to notify */   	
      hook_ptr->notify_id = notify_id;		/* identifier to pass */   	
      hook_ptr->policy = m_ptr->IRQ_POLICY;	/* policy for interrupts */
      put_irq_handler(hook_ptr, irq_vec, generic_handler);

      /* Return index of the IRQ hook in use. */
      m_ptr->IRQ_HOOK_ID = irq_hook_id + 1;
      break;

  case IRQ_RMPOLICY:
      if (irq_hook_id < 0 || irq_hook_id >= NR_IRQ_HOOKS ||
               irq_hooks[irq_hook_id].proc_nr_e == NONE) {
           return(EINVAL);
      } else if (m_ptr->m_source != irq_hooks[irq_hook_id].proc_nr_e) {
           return(EPERM);
      }
      /* Remove the handler and return. */
      rm_irq_handler(&irq_hooks[irq_hook_id]);
      break;

  default:
      r = EINVAL;				/* invalid IRQ_REQUEST */
  }
  return(r);
}

/*===========================================================================*
 *			       generic_handler				     *
 *===========================================================================*/
PRIVATE int generic_handler(hook)
irq_hook_t *hook;	
{
/* This function handles hardware interrupt in a simple and generic way. All
 * interrupts are transformed into messages to a driver. The IRQ line will be
 * reenabled if the policy says so.
 */
  int proc_nr;

  vmassert(intr_disabled());

  /* As a side-effect, the interrupt handler gathers random information by 
   * timestamping the interrupt events. This is used for /dev/random.
   */
  get_randomness(&krandom, hook->irq);

  /* Check if the handler is still alive.
   * If it's dead, this should never happen, as processes that die 
   * automatically get their interrupt hooks unhooked.
   */
  if(!isokendpt(hook->proc_nr_e, &proc_nr))
     minix_panic("invalid interrupt handler", hook->proc_nr_e);

  /* Add a bit for this interrupt to the process' pending interrupts. When 
   * sending the notification message, this bit map will be magically set
   * as an argument. 
   */
  priv(proc_addr(proc_nr))->s_int_pending |= (1 << hook->notify_id);

  /* Build notification message and return. */
  vmassert(intr_disabled());
  mini_notify(proc_addr(HARDWARE), hook->proc_nr_e);
  return(hook->policy & IRQ_REENABLE);
}

#endif /* USE_IRQCTL */

