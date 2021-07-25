/* This table has one slot per process.  It contains all the process management
 * information for each process.  Among other things, it defines the text, data
 * and stack segments, uids and gids, and various flags.  The kernel and file
 * systems have tables that are also indexed by process, with the contents
 * of corresponding slots referring to the same process in all three.
 */
#include <timers.h>
#include <signal.h>

/* Needs to be included here, for 'ps' etc */
#include "const.h"

EXTERN struct mproc {
  char mp_exitstatus;		/* storage for status when process exits */
  char mp_sigstatus;		/* storage for signal # for killed procs */
  pid_t mp_pid;			/* process id */
  endpoint_t mp_endpoint;	/* kernel endpoint id */
  pid_t mp_procgrp;		/* pid of process group (used for signals) */
  pid_t mp_wpid;		/* pid this process is waiting for */
  int mp_parent;		/* index of parent process */
  int mp_tracer;		/* index of tracer process, or NO_TRACER */

  /* Child user and system times. Accounting done on child exit. */
  clock_t mp_child_utime;	/* cumulative user time of children */
  clock_t mp_child_stime;	/* cumulative sys time of children */

  /* Real and effective uids and gids. */
  uid_t mp_realuid;		/* process' real uid */
  uid_t mp_effuid;		/* process' effective uid */
  gid_t mp_realgid;		/* process' real gid */
  gid_t mp_effgid;		/* process' effective gid */

  /* Signal handling information. */
  sigset_t mp_ignore;		/* 1 means ignore the signal, 0 means don't */
  sigset_t mp_catch;		/* 1 means catch the signal, 0 means don't */
  sigset_t mp_sig2mess;		/* 1 means transform into notify message */
  sigset_t mp_sigmask;		/* signals to be blocked */
  sigset_t mp_sigmask2;		/* saved copy of mp_sigmask */
  sigset_t mp_sigpending;	/* pending signals to be handled */
  sigset_t mp_sigtrace;		/* signals to hand to tracer first */
  struct sigaction mp_sigact[_NSIG]; /* as in sigaction(2) */
  vir_bytes mp_sigreturn; 	/* address of C library __sigreturn function */
  struct timer mp_timer;	/* watchdog timer for alarm(2), setitimer(2) */
  clock_t mp_interval[NR_ITIMERS];	/* setitimer(2) repetition intervals */

  unsigned mp_flags;		/* flag bits */
  unsigned mp_trace_flags;	/* trace options */
  vir_bytes mp_procargs;        /* ptr to proc's initial stack arguments */
  message mp_reply;		/* reply message to be sent to one */

  /* Scheduling priority. */
  signed int mp_nice;		/* nice is PRIO_MIN..PRIO_MAX, standard 0. */

  char mp_name[PROC_NAME_LEN];	/* process name */
} mproc[NR_PROCS];

/* Flag values */
#define IN_USE		0x00001	/* set when 'mproc' slot in use */
#define WAITING		0x00002	/* set by WAIT system call */
#define ZOMBIE		0x00004	/* waiting for parent to issue WAIT call */
#define PAUSED		0x00008	/* set by PAUSE system call */
#define ALARM_ON	0x00010	/* set when SIGALRM timer started */
#define EXITING		0x00020	/* set by EXIT, process is now exiting */
#define TOLD_PARENT	0x00040	/* parent wait() completed, ZOMBIE off */
#define STOPPED		0x00080	/* set if process stopped for tracing */
#define SIGSUSPENDED	0x00100	/* set by SIGSUSPEND system call */
#define REPLY		0x00200	/* set if a reply message is pending */
#define FS_CALL		0x00400	/* set if waiting for FS (normal calls) */
#define PM_SIG_PENDING	0x00800	/* process got a signal while waiting for FS */
#define UNPAUSED	0x01000	/* process is not in a blocking call */
#define PRIV_PROC	0x02000	/* system process, special privileges */
#define PARTIAL_EXEC	0x04000	/* process got a new map but no content */
#define TRACE_EXIT	0x08000	/* tracer is forcing this process to exit */
#define TRACE_ZOMBIE	0x10000	/* waiting for tracer to issue WAIT call */
#define DELAY_CALL	0x20000	/* waiting for call before sending signal */

#define NIL_MPROC ((struct mproc *) 0)

