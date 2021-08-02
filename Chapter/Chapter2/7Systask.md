## Operating Systems Design and Implementation Notes

# 7. System Task
##### By Jiawei Wang

Back to Chapter1, we use three notes to introduce 53 **system calls** of POSIX.<br>


In a **conventional operating system with a monolithic kernel**, the term **system call** is used to refer to all calls for services provided by the kernel.<br> In a sense, making a system call is like making a special kind of procedure call, **only system calls enter the kernel (space) or other privileged operating system components and procedure calls do not.**<br>

Unlike in a modern UNIX-like operating system the POSIX standard describes the system calls available to processes. In Minix3 components of the operating system run in user space -- **they are forbidden from doing actual I/O, manipulating kernel tables and doing other things operating system functions normally do.**

**To make the kernel as simple as possible.** Minx3 only compile 28 nessary **"System Call"** in the kernel binary program. Which is shown in layer in Fig. 2-29, we call them **System Task**.

![layer](Sources/layer.png)


## 1. Calls in Minx3
Generally speaking. There are three kinds of **"Calls"** in Minix3:

### System Call

### System Task (Kernel Call)
![syscall](Sources/syscall.png)

### IPC primitive (Trap)


## 2. Implementation of System Task
