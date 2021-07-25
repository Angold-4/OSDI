
#include "syslib.h"

#include <minix/vm.h>

/*===========================================================================*
 *                                vm_brk				     *
 *===========================================================================*/
PUBLIC int vm_brk(endpoint_t ep, char *addr)
{
    message m;
    int result;

    m.VMB_ENDPOINT = ep;
    m.VMB_ADDR = (void *) addr;

    return _taskcall(VM_PROC_NR, VM_BRK, &m);
}

