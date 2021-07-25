## Operating Systerms Design and Implementation Notes

# 4. Implementation of Processes in Minix3
##### By Jiawei Wang

We are now moving closer to looking at the actual **code of Minix3**.<br>
Unlike the introduction sequence in the original book. I divide the implementation into two parts:<br>
* **Process Implementation** ([Note1](https://github.com/Angold-4/OSDI/blob/master/Chapter/Chapter2/1Introprogress.md))
* **Interprocess Communication** ([Note2](https://github.com/Angold-4/OSDI/blob/master/Chapter/Chapter2/2Communication.md) and [Note3](https://github.com/Angold-4/OSDI/blob/master/Chapter/Chapter2/3Semaphore.md))

One useful website for learning minix3: **[elixir.ortiz.sh](https://elixir.ortiz.sh/minix/v3.1.8/C/ident/)**<br>
Minix3 Source code used in this note: **[https://github.com/Angold-4/OSDI/tree/master/Minix3](https://github.com/Angold-4/OSDI/tree/master/Minix3)** (folked from [jncraton](https://github.com/jncraton/minix3))<br>
<br>
Before the formal introduction with code, Let us begin our study of MINIX 3 by taking a bird’s-eye view of the system. <br>
MINIX 3 is structured in **four layers**, with each layer performing a well-defined function. <br>
![layer](Sources/layer.png)



## 1. Clock Task
**Clocks (also called timers) are essential to the operation of any timesharing system.**<br>
In my opinion, **The Clocks is the heart of a computer**, so that it can make all components run regularly. Also, there are several places one could point to and say, **"This is where Minix3 starts running"**.
<br>


### Clock Hardware
The clock is built out of three components: a **crystal oscillator**, a **counter**, and a **holding register**.<br>

![clock](Sources/clock.png)


* **Crystal Oscillator:**<br>When a piece of quartz crystal is properly cut and mounted under tension, it can be made to generate a periodic signal of very high accuracy. 
* **Counter:**<br> The signal that **crystal oscillator** made is fed into the counter to make it count down to zero. When the counter gets to zero, it causes a CPU interrupt.
* **Holding Register:**<br> After getting to zero and causing the interrupt, the holding register is automatically copied into the counter, and the whole process is repeated again indefinitely. These periodic interrupts are called **clock ticks**.<br>

The advantage of the **programmable clock** is that its interrupt frequency can be controlled by software. If a 1-MHz crystal is used, then the counter is pulsed every microsecond. With 16-bit registers, interrupts can be programmed to occur at intervals from 1 microsecond to 65.536 milliseconds, **which means the clock ticks can be changed by adjust the value of registers.** The clock interrupts repeat 60 times a second as long as MINIX 3 runs.<br>


### Clock Software
All the clock hardware does is generate interrupts at known intervals. Everything else involving time must be done by the software, the **clock driver**.<br>
The exact duties of the clock driver vary among operating systems, but must include the following:<br>

* **Maintaining the time of day.**<br>
In Minix3, because the known-intervals-interrupts, we can maintain the time of the day by counting the ticks.<br> but to do that relative to the time the system was booted, rather than relative to a fixed external moment.<br>When the time of day is requested, the stored time of day is added to the counter to get the current time of day. <br>

* **Preventing processes from running longer than they are allowed to.**<br>
Whenever a process is started, the scheduler should initialize a counter to the value of that process’ quantum in clock ticks. At every clock interrupt, the clock driver decrements the quantum counter by 1. When it gets to zero, the clock driver calls the scheduler to set up another process.<br>
<br>

## 2. Start of Minix3
The [kernel/proc.h](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/proc.h) defines the process table of kernel.<br>

Generally speaking, Minx3 runs from [kernel/mpx386.s](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/arch/i386/mpx386.S).<br>
After execute `cstart()` function in [kernel/start.c](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/start.c), which set some global variable and other preparations.<br>
At line 252. after the assembly code: **`jmp main`**, it jumps to the main program of Minix3:

### [kernel/main.c](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/main.c)
* **`line 76 to 182`: Loop the `image[]`, initialize the boot process table.**<br>
The **`image[]`** is defined in the line 114 of **[kernel/table.c](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/table.c):**
```c
PUBLIC struct boot_image image[] = {
    /* process nr, pc,flags, qs,  queue, stack, traps, ipcto, call,  name */ 
    {IDLE,  idle_task,IDL_F,  8, IDLE_Q, IDL_S,     0,     0, no_c,"idle"  },
    {CLOCK,clock_task,TSK_F,  8, TASK_Q, TSK_S, TSK_T,     0, no_c,"clock" },
    {SYSTEM, sys_task,TSK_F,  8, TASK_Q, TSK_S, TSK_T,     0, no_c,"system"},
    {HARDWARE,      0,TSK_F,  8, TASK_Q, HRD_S,     0,     0, no_c,"kernel"},
    {PM_PROC_NR,    0,SRV_F, 32,      4, 0,     SRV_T, SRV_M, c(pm_c),"pm"    },
    {FS_PROC_NR,    0,SRV_F, 32,      5, 0,     SRV_T, SRV_M, c(fs_c),"vfs"   },
    {RS_PROC_NR,    0,SVM_F,  4,      4, 0,     SRV_T, SYS_M, c(rs_c),"rs"    },
    {MEM_PROC_NR,   0,SVM_F,  4,      3, 0,     SRV_T, SYS_M,c(mem_c),"memory"},
    {LOG_PROC_NR,   0,SRV_F,  4,      2, 0,     SRV_T, SYS_M,c(drv_c),"log"   },
    {TTY_PROC_NR,   0,SVM_F,  4,      1, 0,     SRV_T, SYS_M,c(tty_c),"tty"   },
    {DS_PROC_NR,    0,SVM_F,  4,      4, 0,     SRV_T, SYS_M, c(ds_c),"ds"    },
    {MFS_PROC_NR,   0,SVM_F, 32,      5, 0,     SRV_T, SRV_M, c(fs_c),"mfs"   },
    {VM_PROC_NR,    0,VM_F, 32,      2, 0,     SRV_T, SRV_M, c(vm_c),"vm"    },
    {INIT_PROC_NR,  0,USR_F,  8, USER_Q, 0,     USR_T, USR_M, c(usr_c),"init"  },
};
```
* **`line 193 to 211`: Return to the assembly code to start running the current process by calling `restart()`**<br>
The **`restart()`** is defined from 436 to 469 of **[kernel/mpx386.s](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/arch/i386/mpx386.S):**

```assembly
/*===========================================================================*/
/*				restart					     */
/*===========================================================================*/
restart:

/* Restart the current process or the next process if it is set.  */

	cli
	call	schedcheck
	movl	proc_ptr, %esp	/* will assume P_STACKBASE == 0 */
	lldt	P_LDT_SEL(%esp)	/* enable process' segment descriptors  */
	cmpl	$0, P_CR3(%esp)
	jz	0f
	mov	P_CR3(%esp), %eax
	cmpl	loadedcr3, %eax
	jz	0f
	mov	%eax, %cr3
	mov	%eax, loadedcr3
	mov	proc_ptr, %eax
	mov	%eax, ptproc
	movl	$0, dirtypde
0:
	lea	P_STACKTOP(%esp), %eax	/* arrange for next interrupt */
	movl	%eax, tss+TSS3_S_SP0	/* to save state in process table */
restart1:
	decb	k_reenter
	popw	%gs
	popw	%fs
	popw	%es
	popw	%ds
	popal
	add	$4, %esp	/* skip return adr */
	iret	/* continue process */
```
**Then the CPU will start to execute all boot processes that we initialized before. (from 0)**



## 3. Implementation of Clock Driver in Minix3
The Minix 3 clock driver is contained in the file [kernel/clock.c](https://github.com/Angold-4/OSDI/blob/master/Minix3/kernel/clock.c).





 


