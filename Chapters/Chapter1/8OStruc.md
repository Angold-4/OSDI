### Operating Systerms Design and Implementation Notes

##### By Jiawei Wang

# 8. Oprating System Structure

**Now that we have seen what operating systems look like on the outside (i.e, the programmer’s interface), it is time to take a look inside.**<br>
**In the Following sections, We will introduce Oprating System briefly in 5 point of views:**<br>


<!-- vim-markdown-toc GFM -->

* [1.Monolithic Systems](#1monolithic-systems)

<!-- vim-markdown-toc -->



## 1.Monolithic Systems
**In this point of view. The OS is written as a collection of procedures.**<br>
* **Each of procedures can call any of the other ones whenever it needs to.**
* **Each procedure in the system has a well-defined interface in terms of parameters and results**
* **Each one is free to call any other one, if the latter provides some useful computation that the former needs.**
<br>

**This is a good time to look at how system calls are performed in this point of view.**<br>
**Let's see this ```read``` system call:**<br>

```c
count = read(fd, &buffer, nbytes);
```

![monolithic](Sources/monolithic.png)

**In preparation for calling the read library procedure, which actually makes the ```read``` system call.**<br>
**(Please notice that the following point 1 to 11 are correspond to the 11 steps in figure 1.16 above)**

1. **The calling program pushes the parameters onto the stack(nbytes)**
2. **The calling program pushes the parameters onto the stack(buffer)**
3. **The calling program pushes the parameters onto the stack(fd)**
4. **Then comes the actual call to the library procedure ```read```.**
5. **Execute the library procedure ```read```, It possibly written in assembly language, typically puts the system call number in a place where the operating system expects it, such as a register.**
6. **This step is the continuous of step 5, Then it executes a TRAP instruction to switch from user mode to kernel mode and start execution at a fixed address within the kernel.**
7. **Within the kernel, the kernel code that starts examines the system call number and then dispatches to the correct system call handler, usually via a table of pointers to system call handlers indexed on system call number.**
8. **At that point the system call handler runs <u>(This is the formal handler)</u>**
9. **Once the system call handler has completed its work, control may be returned to the user-space library procedure at the instruction following the TRAP instruction.**
1. **This procedure then returns to the user program(The program called ```read``` system call) in the usual way procedure calls return**
1. **To finish the job, the user program has to clean up the stack, as it does after any procedure call.**
> **Assuming the stack grows downward, as it often does, the compiled code increments the stack pointer exactly enough to remove the parameters pushed before the call to read. The program is now free to do whatever it wants to do next.**

<br>
**This organization suggests a basic structure for the operating system:**
1. **A main program that invokes the requested service procedure.<br><u>i.e: The user program which called the system call</u>**
2. **A set of service procedures that carry out the system calls.<br><u>i.e: The system call handler inside the kernel (8)</u>**
3. **A set of utility procedures that help the service procedures.<br><u>i.e: Specific operation. Such as fetching data from user programs or clean up the stack</u>**
<br>

![monolithicmodel](Sources/monolithicmodel.png)



