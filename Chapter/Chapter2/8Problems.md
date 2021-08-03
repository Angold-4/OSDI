## Operating Systems Design and Implementation Notes

# 8. Problems of Chapter 2
##### By Jiawei Wang


## Questions

> **On all current computers, at least part of the interrupt handlers are written in assembly language. Why?**

**Generally, high level languages do not allow one the kind of access to CPU hardware that is required. For instance, an interrupt handler may be required to enable and disable the interrupt servicing a particular device, or to manipulate data within a process’ stack area. Also, interrupt service routines must execute as rapidly as possible.**<br>


> **What is the fundamental difference between a process and a thread?**

* **A process is a grouping of resources: an address space, open files, signal handlers, and one or more threads.** 
* **A thread is just an execution unit.**<br>


> **In a system with threads, is there normally one stack per thread or one stack per process? Explain.**
**Each thread calls procedures on its own, so it must have its own stack for the local variables, return addresses, and so on.**<br>


> **What is a race condition?**
**A race condition is a situation in which two (or more) process are about to perform some action. Depending on the exact timing, one or other goes first. If one of the processes goes first, everything works, but if another one goes first, a fatal error occurs.**<br>


> **Round-robin schedulers normally maintain a list of all runnable processes, with each process occurring exactly once in the list. What would happen if a process occurred twice in the list? Can you think of any reason for allowing this?**
**If a process occurs multiple times in the list, it will get multiple quanta per cycle. This approach could be used to give more important processes a larger share of the CPU.**<br>


> **During execution, MINIX 3 maintains a variable proc ptr that points to the process table entry for the current process. Why?**
**This pointer makes it easy to find the place to save the registers when a process switch is needed, either due to a system call or an interrupt.**<br>


> **MINIX 3 does not buffer messages. Explain how this design decision causes problems with clock and keyboard interrupts.**
**When a clock or keyboard interrupt occurs, and the task that should get the message is not blocked, the system has to do something strange to avoid losing the interrupt. With buffered messages this problem would not occur. Notification bitmaps provide provide a simple alternative to buffering.**
