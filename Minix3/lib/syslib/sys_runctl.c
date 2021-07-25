#include "syslib.h"

/*===========================================================================*
 *                                sys_runctl			     	     *
 *===========================================================================*/
PUBLIC int sys_runctl(endpoint_t proc_ep, int action, int flags)
{
  message m;

  m.RC_ENDPT = proc_ep;
  m.RC_ACTION = action;
  m.RC_FLAGS = flags;

  return(_taskcall(SYSTASK, SYS_RUNCTL, &m));
}
