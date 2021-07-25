/* The kernel call implemented in this file:
 *   m_type:	SYS_NEWMAP
 *
 * The parameters for this kernel call are:
 *    m1_i1:	PR_ENDPT		(install new map for this process)
 *    m1_p1:	PR_MEM_PTR		(pointer to the new memory map)
 */
#include "../system.h"
#include <minix/endpoint.h>

#if USE_NEWMAP

/*===========================================================================*
 *				do_newmap				     *
 *===========================================================================*/
PUBLIC int do_newmap(m_ptr)
message *m_ptr;			/* pointer to request message */
{
/* Handle sys_newmap().  Fetch the memory map. */
  register struct proc *rp;	/* process whose map is to be loaded */
  struct mem_map *map_ptr;	/* virtual address of map inside caller */
  phys_bytes src_phys;		/* physical address of map at the */
  int proc_nr;

  map_ptr = (struct mem_map *) m_ptr->PR_MEM_PTR;
  if (! isokendpt(m_ptr->PR_ENDPT, &proc_nr)) return(EINVAL);
  if (iskerneln(proc_nr)) return(EPERM);
  rp = proc_addr(proc_nr);

  return newmap(rp, map_ptr);
}


/*===========================================================================*
 *				newmap					     *
 *===========================================================================*/
PUBLIC int newmap(rp, map_ptr)
struct proc *rp;		/* process whose map is to be loaded */
struct mem_map *map_ptr;	/* virtual address of map inside caller */
{
  int r;
/* Fetch the memory map. */
  if((r=data_copy(who_e, (vir_bytes) map_ptr,
	SYSTEM, (vir_bytes) rp->p_memmap, sizeof(rp->p_memmap))) != OK) {
	kprintf("newmap: data_copy failed! (%d)\n", r);
	return r;
  }

  alloc_segments(rp);

  return(OK);
}
#endif /* USE_NEWMAP */

