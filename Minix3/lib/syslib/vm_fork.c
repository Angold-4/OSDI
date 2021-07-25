
#include "syslib.h"

#include <minix/vm.h>

/*===========================================================================*
 *                                vm_fork				     *
 *===========================================================================*/
PUBLIC int vm_fork(endpoint_t ep, int slot, endpoint_t *childep)
{
    message m;
    int result;

    m.VMF_ENDPOINT = ep;
    m.VMF_SLOTNO = slot;

    result = _taskcall(VM_PROC_NR, VM_FORK, &m);

    *childep = m.VMF_CHILD_ENDPOINT;

    return(result);
}

