#include "syslib.h"

/*===========================================================================*
 *                                sys_cprof				     *
 *===========================================================================*/
PUBLIC int sys_cprof(action, size, endpt, ctl_ptr, mem_ptr)
int action; 				/* get/reset profiling tables */
int size;				/* size of allocated memory */
endpoint_t endpt;				/* caller endpoint */
void *ctl_ptr;				/* location of info struct */
void *mem_ptr;				/* location of allocated memory */
{
  message m;

  m.PROF_ACTION         = action;
  m.PROF_MEM_SIZE       = size;
  m.PROF_ENDPT		= endpt;
  m.PROF_CTL_PTR        = ctl_ptr;
  m.PROF_MEM_PTR        = mem_ptr;

  return(_taskcall(SYSTASK, SYS_CPROF, &m));
}

