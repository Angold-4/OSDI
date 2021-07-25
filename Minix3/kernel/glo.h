#ifndef GLO_H
#define GLO_H

#include <minix/sysutil.h>

/* Global variables used in the kernel. This file contains the declarations;
 * storage space for the variables is allocated in table.c, because EXTERN is
 * defined as extern unless the _TABLE definition is seen. We rely on the 
 * compiler's default initialization (0) for several global variables. 
 */
#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

#include <minix/config.h>
#include <archtypes.h>
#include "config.h"
#include "debug.h"

/* Variables relating to shutting down MINIX. */
EXTERN char kernel_exception;		/* TRUE after system exceptions */
EXTERN char shutdown_started;		/* TRUE after shutdowns / reboots */

/* Kernel information structures. This groups vital kernel information. */
EXTERN struct kinfo kinfo;		/* kernel information for users */
EXTERN struct machine machine;		/* machine information for users */
EXTERN struct kmessages kmess;  	/* diagnostic messages in kernel */
EXTERN struct k_randomness krandom;	/* gather kernel random information */
EXTERN struct loadinfo kloadinfo;	/* status of load average */

/* Process scheduling information and the kernel reentry count. */
EXTERN struct proc *proc_ptr;	/* pointer to currently running process */
EXTERN struct proc *next_ptr;	/* next process to run after restart() */
EXTERN struct proc *prev_ptr;	
EXTERN struct proc *bill_ptr;	/* process to bill for clock ticks */
EXTERN struct proc *vmrestart;  /* first process on vmrestart queue */
EXTERN struct proc *vmrequest;  /* first process on vmrequest queue */
EXTERN struct proc *pagefaults; /* first process on pagefault queue */
EXTERN char k_reenter;		/* kernel reentry count (entry count less 1) */
EXTERN unsigned lost_ticks;	/* clock ticks counted outside clock task */


/* Interrupt related variables. */
EXTERN irq_hook_t irq_hooks[NR_IRQ_HOOKS];	/* hooks for general use */
EXTERN int irq_actids[NR_IRQ_VECTORS];		/* IRQ ID bits active */
EXTERN int irq_use;				/* map of all in-use irq's */
EXTERN u32_t system_hz;				/* HZ value */

/* Miscellaneous. */
EXTERN reg_t mon_ss, mon_sp;		/* boot monitor stack */
EXTERN int mon_return;			/* true if we can return to monitor */
EXTERN int do_serial_debug;
EXTERN endpoint_t who_e;		/* message source endpoint */
EXTERN int who_p;			/* message source proc */
EXTERN int sys_call_code;		/* kernel call number in SYSTEM */
EXTERN time_t boottime;
EXTERN char params_buffer[512];		/* boot monitor parameters */
EXTERN int minix_panicing;
EXTERN int locklevel;
#define MAGICTEST 0xC0FFEE23
EXTERN u32_t magictest;			/* global magic number */

#if DEBUG_TRACE
EXTERN int verboseflags;
#endif

/* VM */
EXTERN int vm_running;
EXTERN int catch_pagefaults;
EXTERN struct proc *ptproc;

/* Timing */
EXTERN util_timingdata_t timingdata[TIMING_CATEGORIES];

/* Variables that are initialized elsewhere are just extern here. */
extern struct boot_image image[]; 	/* system image processes */
extern char *t_stack[];			/* task stack space */
extern struct segdesc_s gdt[];		/* global descriptor table */

#endif /* GLO_H */
