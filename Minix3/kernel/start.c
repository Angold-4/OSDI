
/* First C file used by the kernel. */

/* This file contains the C startup code for Minix on Intel processors.
 * It cooperates with mpx.s to set up a good environment for main()
 *
 * This code runs in real mode for a 16 bit kernel and may have to switch
 * to protected mode for a 286.
 * For a 32 bit kernel this already runs in protected mode, but the selectors
 * are still those given by the BIOS with interrupts disabled, so the 
 * descriptors need to be reloaded and interrupt descriptors made.
 */

#include "kernel.h"
#include "proc.h"
#include <stdlib.h>
#include <string.h>
#include <archconst.h>
#include <proto.h>

FORWARD _PROTOTYPE( char *get_value, (_CONST char *params, _CONST char *key));
/*===========================================================================*
 *				cstart					     *
 *===========================================================================*/
PUBLIC void cstart(cs, ds, mds, parmoff, parmsize)
U16_t cs, ds;			/* kernel code and data segment */
U16_t mds;			/* monitor data segment */
U16_t parmoff, parmsize;	/* boot parameters offset and length */
{
/* Perform system initializations prior to calling main(). Most settings are
 * determined with help of the environment strings passed by MINIX' loader.
 */
  register char *value;				/* value in key=value pair */
  extern int etext, end;
  int h;

  /* Record where the kernel and the monitor are. */
  kinfo.code_base = seg2phys(cs);
  kinfo.code_size = (phys_bytes) &etext;	/* size of code segment */
  kinfo.data_base = seg2phys(ds);
  kinfo.data_size = (phys_bytes) &end;		/* size of data segment */

  /* protection initialization */
  prot_init();

  /* Copy the boot parameters to the local buffer. */
  arch_get_params(params_buffer, sizeof(params_buffer));

  /* Record miscellaneous information for user-space servers. */
  kinfo.nr_procs = NR_PROCS;
  kinfo.nr_tasks = NR_TASKS;
  strncpy(kinfo.release, OS_RELEASE, sizeof(kinfo.release));
  kinfo.release[sizeof(kinfo.release)-1] = '\0';
  strncpy(kinfo.version, OS_VERSION, sizeof(kinfo.version));
  kinfo.version[sizeof(kinfo.version)-1] = '\0';
  kinfo.proc_addr = (vir_bytes) proc;

  /* Load average data initialization. */
  kloadinfo.proc_last_slot = 0;
  for(h = 0; h < _LOAD_HISTORY; h++)
	kloadinfo.proc_load_history[h] = 0;

  /* Processor? Decide if mode is protected for older machines. */
  machine.processor=atoi(get_value(params_buffer, "processor")); 

  /* XT, AT or MCA bus? */
  value = get_value(params_buffer, "bus");
  if (value == NIL_PTR || strcmp(value, "at") == 0) {
      machine.pc_at = TRUE;			/* PC-AT compatible hardware */
  } else if (strcmp(value, "mca") == 0) {
      machine.pc_at = machine.ps_mca = TRUE;	/* PS/2 with micro channel */
  }

  /* Type of VDU: */
  value = get_value(params_buffer, "video");	/* EGA or VGA video unit */
  if (strcmp(value, "ega") == 0) machine.vdu_ega = TRUE;
  if (strcmp(value, "vga") == 0) machine.vdu_vga = machine.vdu_ega = TRUE;

  /* Get clock tick frequency. */
  value = get_value(params_buffer, "hz");
  if(value)
	system_hz = atoi(value);
  if(!value || system_hz < 2 || system_hz > 50000)	/* sanity check */
	system_hz = DEFAULT_HZ;
  value = get_value(params_buffer, SERVARNAME);
  if(value && atoi(value) == 0)
	do_serial_debug=1;

  /* Return to assembler code to switch to protected mode (if 286), 
   * reload selectors and call main().
   */

  intr_init(INTS_MINIX);
}

/*===========================================================================*
 *				get_value				     *
 *===========================================================================*/

PRIVATE char *get_value(params, name)
_CONST char *params;				/* boot monitor parameters */
_CONST char *name;				/* key to look up */
{
/* Get environment value - kernel version of getenv to avoid setting up the
 * usual environment array.
 */
  register _CONST char *namep;
  register char *envp;

  for (envp = (char *) params; *envp != 0;) {
	for (namep = name; *namep != 0 && *namep == *envp; namep++, envp++)
		;
	if (*namep == '\0' && *envp == '=') return(envp + 1);
	while (*envp++ != 0)
		;
  }
  return(NIL_PTR);
}
