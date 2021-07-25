#ifndef PROC_H
#define PROC_H

/* Here is the declaration of the process table.  It contains all process
 * data, including registers, flags, scheduling priority, memory map, 
 * accounting, message passing (IPC) information, and so on. 
 *
 * Many assembly code routines reference fields in it.  The offsets to these
 * fields are defined in the assembler include file sconst.h.  When changing
 * struct proc, be sure to change sconst.h to match.
 */
#include <minix/com.h>
#include <minix/portio.h>
#include "const.h"
#include "priv.h"

struct proc {
  struct stackframe_s p_reg;	/* process' registers saved in stack frame */
  struct segframe p_seg;	/* segment descriptors */
  proc_nr_t p_nr;		/* number of this process (for fast access) */
  struct priv *p_priv;		/* system privileges structure */
  short p_rts_flags;		/* process is runnable only if zero */
  short p_misc_flags;		/* flags that do not suspend the process */

  char p_priority;		/* current scheduling priority */
  char p_max_priority;		/* maximum scheduling priority */
  char p_ticks_left;		/* number of scheduling ticks left */
  char p_quantum_size;		/* quantum size in ticks */

  struct mem_map p_memmap[NR_LOCAL_SEGS];   /* memory map (T, D, S) */
  struct pagefault p_pagefault;	/* valid if PAGEFAULT in p_rts_flags set */
  struct proc *p_nextpagefault;	/* next on PAGEFAULT chain */

  clock_t p_user_time;		/* user time in ticks */
  clock_t p_sys_time;		/* sys time in ticks */

  clock_t p_virt_left;		/* number of ticks left on virtual timer */
  clock_t p_prof_left;		/* number of ticks left on profile timer */

  struct proc *p_nextready;	/* pointer to next ready process */
  struct proc *p_caller_q;	/* head of list of procs wishing to send */
  struct proc *p_q_link;	/* link to next proc wishing to send */
  int p_getfrom_e;		/* from whom does process want to receive? */
  int p_sendto_e;		/* to whom does process want to send? */

  sigset_t p_pending;		/* bit map for pending kernel signals */

  char p_name[P_NAME_LEN];	/* name of the process, including \0 */

  endpoint_t p_endpoint;	/* endpoint number, generation-aware */

  message p_sendmsg;		/* Message from this process if SENDING */
  message p_delivermsg;		/* Message for this process if MF_DELIVERMSG */
  vir_bytes p_delivermsg_vir;	/* Virtual addr this proc wants message at */
  vir_bytes p_delivermsg_lin;	/* Linear addr this proc wants message at */

  /* If handler functions detect a process wants to do something with
   * memory that isn't present, VM has to fix it. Until it has asked
   * what needs to be done and fixed it, save necessary state here.
   *
   * The requester gets a copy of its request message in reqmsg and gets
   * VMREQUEST set.
   */
  struct {
	struct proc	*nextrestart;	/* next in vmrestart chain */
	struct proc	*nextrequestor;	/* next in vmrequest chain */
#define VMSTYPE_SYS_NONE	0
#define VMSTYPE_KERNELCALL	1
#define VMSTYPE_DELIVERMSG	2
	int		type;		/* suspended operation */
	union {
		/* VMSTYPE_SYS_MESSAGE */
		message		reqmsg;	/* suspended request message */
	} saved;

	/* Parameters of request to VM */
	vir_bytes 	start, length;	/* memory range */
	u8_t		writeflag;	/* nonzero for write access */
	endpoint_t	who;

	/* VM result when available */
	int		vmresult;

#if DEBUG_VMASSERT
	char stacktrace[200];
#endif

	/* If the suspended operation is a sys_call, its details are
	 * stored here.
	 */
  } p_vmrequest;

  struct proc *next_soft_notify;
  int p_softnotified;

#if DEBUG_SCHED_CHECK
  int p_ready, p_found;
#define PMAGIC 0xC0FFEE1
  int p_magic;	/* check validity of proc pointers */
#endif

#if DEBUG_TRACE
  int p_schedules;
#endif
};

/* Bits for the runtime flags. A process is runnable iff p_rts_flags == 0. */
#define SLOT_FREE	 0x01	/* process slot is free */
#define PROC_STOP	 0x02	/* process has been stopped */
#define SENDING		 0x04	/* process blocked trying to send */
#define RECEIVING	 0x08	/* process blocked trying to receive */
#define SIGNALED	 0x10	/* set when new kernel signal arrives */
#define SIG_PENDING	 0x20	/* unready while signal being processed */
#define P_STOP		 0x40	/* set when process is being traced */
#define NO_PRIV		 0x80	/* keep forked system process from running */
#define NO_ENDPOINT     0x100	/* process cannot send or receive messages */
#define VMINHIBIT       0x200	/* not scheduled until pagetable set by VM */
#define PAGEFAULT       0x400	/* process has unhandled pagefault */
#define VMREQUEST       0x800	/* originator of vm memory request */
#define VMREQTARGET    0x1000	/* target of vm memory request */
#define SYS_LOCK       0x2000	/* temporary process lock flag for systask */

/* These runtime flags can be tested and manipulated by these macros. */

#define RTS_ISSET(rp, f) (((rp)->p_rts_flags & (f)) == (f))


/* Set flag and dequeue if the process was runnable. */
#define RTS_SET(rp, f)							\
	do {								\
		vmassert(intr_disabled());				\
		if(!(rp)->p_rts_flags) { dequeue(rp); }			\
		(rp)->p_rts_flags |=  (f);				\
		vmassert(intr_disabled());				\
	} while(0)

/* Clear flag and enqueue if the process was not runnable but is now. */
#define RTS_UNSET(rp, f) 						\
	do {								\
		int rts;						\
		vmassert(intr_disabled());				\
		rts = (rp)->p_rts_flags;				\
		(rp)->p_rts_flags &= ~(f);				\
		if(rts && !(rp)->p_rts_flags) { enqueue(rp); }		\
		vmassert(intr_disabled());				\
	} while(0)

/* Set flag and dequeue if the process was runnable. */
#define RTS_LOCK_SET(rp, f)						\
	do {								\
		int u = 0;						\
		if(!intr_disabled()) { u = 1; lock; }			\
		if(!(rp)->p_rts_flags) { dequeue(rp); }			\
		(rp)->p_rts_flags |=  (f);				\
		if(u) { unlock;	}					\
	} while(0)

/* Clear flag and enqueue if the process was not runnable but is now. */
#define RTS_LOCK_UNSET(rp, f) 						\
	do {								\
		int rts;						\
		int u = 0;						\
		if(!intr_disabled()) { u = 1; lock; }			\
		rts = (rp)->p_rts_flags;				\
		(rp)->p_rts_flags &= ~(f);				\
		if(rts && !(rp)->p_rts_flags) { enqueue(rp); }		\
		if(u) { unlock;	}					\
	} while(0)

/* Set flags to this value. */
#define RTS_LOCK_SETFLAGS(rp, f)					\
	do {								\
		int u = 0;						\
		if(!intr_disabled()) { u = 1; lock; }			\
		if(!(rp)->p_rts_flags && (f)) { dequeue(rp); }		\
		(rp)->p_rts_flags = (f);				\
		if(u) { unlock;	}					\
	} while(0)

/* Misc flags */
#define MF_REPLY_PEND	0x001	/* reply to IPC_REQUEST is pending */
#define MF_VIRT_TIMER	0x002	/* process-virtual timer is running */
#define MF_PROF_TIMER	0x004	/* process-virtual profile timer is running */
#define MF_ASYNMSG	0x010	/* Asynchrous message pending */
#define MF_FULLVM	0x020
#define MF_DELIVERMSG	0x040	/* Copy message for him before running */
#define MF_SIG_DELAY	0x080	/* Send signal when no longer sending */
#define MF_SC_ACTIVE	0x100	/* Syscall tracing: in a system call now */
#define MF_SC_DEFER	0x200	/* Syscall tracing: deferred system call */
#define MF_SC_TRACE	0x400	/* Syscall tracing: trigger syscall events */

/* Scheduling priorities for p_priority. Values must start at zero (highest
 * priority) and increment.  Priorities of the processes in the boot image 
 * can be set in table.c. IDLE must have a queue for itself, to prevent low 
 * priority user processes to run round-robin with IDLE. 
 */
#define NR_SCHED_QUEUES   16	/* MUST equal minimum priority + 1 */
#define TASK_Q		   0	/* highest, used for kernel tasks */
#define MAX_USER_Q  	   0    /* highest priority for user processes */   
#define USER_Q  	   7    /* default (should correspond to nice 0) */   
#define MIN_USER_Q	  14	/* minimum priority for user processes */
#define IDLE_Q		  15    /* lowest, only IDLE process goes here */

/* Magic process table addresses. */
#define BEG_PROC_ADDR (&proc[0])
#define BEG_USER_ADDR (&proc[NR_TASKS])
#define END_PROC_ADDR (&proc[NR_TASKS + NR_PROCS])

#define NIL_PROC          ((struct proc *) 0)		
#define NIL_SYS_PROC      ((struct proc *) 1)		
#define cproc_addr(n)     (&(proc + NR_TASKS)[(n)])
#define proc_addr(n)      (&(proc[NR_TASKS + (n)]))
#define proc_nr(p) 	  ((p)->p_nr)

#define isokprocn(n)      ((unsigned) ((n) + NR_TASKS) < NR_PROCS + NR_TASKS)
#define isemptyn(n)       isemptyp(proc_addr(n)) 
#define isemptyp(p)       ((p)->p_rts_flags == SLOT_FREE)
#define iskernelp(p)	  ((p) < BEG_USER_ADDR)
#define iskerneln(n)	  ((n) < 0)
#define isuserp(p)        isusern((p) >= BEG_USER_ADDR)
#define isusern(n)        ((n) >= 0)

/* The process table and pointers to process table slots. The pointers allow
 * faster access because now a process entry can be found by indexing the
 * pproc_addr array, while accessing an element i requires a multiplication
 * with sizeof(struct proc) to determine the address. 
 */
EXTERN struct proc proc[NR_TASKS + NR_PROCS];	/* process table */
EXTERN struct proc *rdy_head[NR_SCHED_QUEUES]; /* ptrs to ready list headers */
EXTERN struct proc *rdy_tail[NR_SCHED_QUEUES]; /* ptrs to ready list tails */

#endif /* PROC_H */
