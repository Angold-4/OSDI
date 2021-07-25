#ifndef PRIV_H
#define PRIV_H

/* Declaration of the system privileges structure. It defines flags, system 
 * call masks, an synchronous alarm timer, I/O privileges, pending hardware 
 * interrupts and notifications, and so on.
 * System processes each get their own structure with properties, whereas all 
 * user processes share one structure. This setup provides a clear separation
 * between common and privileged process fields and is very space efficient. 
 *
 * Changes:
 *   Jul 01, 2005	Created.  (Jorrit N. Herder)	
 */
#include <minix/com.h>
#include "const.h"
#include "type.h"

/* Max. number of I/O ranges that can be assigned to a process */
#define NR_IO_RANGE	32

/* Max. number of device memory ranges that can be assigned to a process */
#define NR_MEM_RANGE	10

/* Max. number of IRQs that can be assigned to a process */
#define NR_IRQ	4
 
struct priv {
  proc_nr_t s_proc_nr;		/* number of associated process */
  sys_id_t s_id;		/* index of this system structure */
  short s_flags;		/* PREEMTIBLE, BILLABLE, etc. */

  /* Asynchronous sends */
  vir_bytes s_asyntab;		/* addr. of table in process' address space */
  size_t s_asynsize;		/* number of elements in table. 0 when not in
				 * use
				 */

  short s_trap_mask;		/* allowed system call traps */
  sys_map_t s_ipc_to;		/* allowed destination processes */

  /* allowed kernel calls */
#define CALL_MASK_SIZE BITMAP_CHUNKS(NR_SYS_CALLS)
  bitchunk_t s_k_call_mask[CALL_MASK_SIZE];  

  sys_map_t s_notify_pending;  	/* bit map with pending notifications */
  irq_id_t s_int_pending;	/* pending hardware interrupts */
  sigset_t s_sig_pending;	/* pending signals */

  timer_t s_alarm_timer;	/* synchronous alarm timer */ 
  struct far_mem s_farmem[NR_REMOTE_SEGS];  /* remote memory map */
  reg_t *s_stack_guard;		/* stack guard word for kernel tasks */

  int s_nr_io_range;		/* allowed I/O ports */
  struct io_range s_io_tab[NR_IO_RANGE];

  int s_nr_mem_range;		/* allowed memory ranges */
  struct mem_range s_mem_tab[NR_MEM_RANGE];

  int s_nr_irq;			/* allowed IRQ lines */
  int s_irq_tab[NR_IRQ];
  vir_bytes s_grant_table;	/* grant table address of process, or 0 */
  int s_grant_entries;		/* no. of entries, or 0 */
};

/* Guard word for task stacks. */
#define STACK_GUARD	((reg_t) (sizeof(reg_t) == 2 ? 0xBEEF : 0xDEADBEEF))

/* Magic system structure table addresses. */
#define BEG_PRIV_ADDR (&priv[0])
#define END_PRIV_ADDR (&priv[NR_SYS_PROCS])

#define priv_addr(i)      (ppriv_addr)[(i)]
#define priv_id(rp)	  ((rp)->p_priv->s_id)
#define priv(rp)	  ((rp)->p_priv)

#define id_to_nr(id)	priv_addr(id)->s_proc_nr
#define nr_to_id(nr)    priv(proc_addr(nr))->s_id

#define may_send_to(rp, nr) (get_sys_bit(priv(rp)->s_ipc_to, nr_to_id(nr)))

/* The system structures table and pointers to individual table slots. The 
 * pointers allow faster access because now a process entry can be found by 
 * indexing the psys_addr array, while accessing an element i requires a 
 * multiplication with sizeof(struct sys) to determine the address. 
 */
EXTERN struct priv priv[NR_SYS_PROCS];		/* system properties table */
EXTERN struct priv *ppriv_addr[NR_SYS_PROCS];	/* direct slot pointers */

/* Unprivileged user processes all share the same privilege structure.
 * This id must be fixed because it is used to check send mask entries.
 */
#define USER_PRIV_ID	0

/* Make sure the system can boot. The following sanity check verifies that
 * the system privileges table is large enough for the number of processes
 * in the boot image. 
 */
#if (NR_BOOT_PROCS > NR_SYS_PROCS)
#error NR_SYS_PROCS must be larger than NR_BOOT_PROCS
#endif

#endif /* PRIV_H */
