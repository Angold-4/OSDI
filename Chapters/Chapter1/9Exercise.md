### Operating Systerms Design and Implementation Notes

##### By Jiawei Wang

# 9. Exercises for Chapter 1

<br>

### Question1
> **What are the two main functions of an operating system?**<br>


1. **Provide the users with an extended (i.e., virtual) machine**
2. **Manage the I/O devices and other system resources.**



### Question2
> **What is the difference between kernel mode and user mode? <br>Why is the difference important to an operating system?**


**In kernel mode, every machine instruction is allowed, as is access to all the I/O devices. In user mode, many sensitive instructions are prohibited.**<br>
**Operating systems use these two modes to encapsulate user programs. Running user programs in user mode keeps them from doing I/O and prevents them from interfering with each other and with the kernel.**



### Question3
> **What is multiprogramming?**


**Multiprogramming is the rapid switching of the CPU between multiple processes in memory. It is commonly used to keep the CPU busy while one or more processes are doing I/O.**



### Question4
> **What is spooling? Do you think that advanced personal computers will have spooling as a standard feature in the future?**


**First Please check the [Spooling](https://en.wikipedia.org/wiki/Spooling).it is to use software to simulate offline technology**<br>
**the "Spool" stands for Simultaneous Peripheral Operations On-Line, which I've be metioned in the history of OS.**<br>

* **<u>Input spooling</u> is the technique of reading in jobs, for example, from cards, onto the disk, so that when the currently executing processes are finished, there will be work waiting for the CPU.**
* **<u>Output spooling</u> consists of first copying printable files to disk before printing them, rather than printing directly as the output is generated**
<br>

**Input spooling on a personal computer is not very likely, but output spooling is widely used in nowadays computers.**





























