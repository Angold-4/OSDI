### Operating Systerms Design and Implementation Notes

##### By Jiawei Wang

# 5. Systerm Calls (2)
<br>

![Systermcalls](Sources/Systermcalls.png)<br>

<!-- vim-markdown-toc GFM -->

* [File System](#file-system)
    * [Standard Stream](#standard-stream)
    * [Three Data Structures inside The Kernel](#three-data-structures-inside-the-kernel)
        * [The per-process file description table](#the-per-process-file-description-table)
        * [The system-wide table of open file descriptions](#the-system-wide-table-of-open-file-descriptions)
        * [The file system i-node table](#the-file-system-i-node-table)

<!-- vim-markdown-toc -->


## File System
**Before we try to understand the specific file system calls<br>**
**we need to have a general understanding of the operating principle of the entire file system:**

### Standard Stream
**In computer programming, [standard streams](https://en.wikipedia.org/wiki/Standard_streams) are interconnected input and output communication channels between a computer program and its environment when it begins execution.**<br>

**Once a process is created, it will automatically creates a standard stream by opening 3 files (stdin stdout stderr)<br>**
**Once the process opens an existing file or creates a new file, the kernel returns a file descriptor to the process.<br>**
**Once the process involves IO operations, there must be a call file descriptor.**
<br>

**To understand what is Standard Stream. let us see an example in shell process:**<br>
**In the [last Note](https://github.com/Angold-4/OSDI/blob/master/Chapters/Chapter1/5Syscall-1.md) we metioned that shell is a process which accept the user input and creat child processes<br>**
**Let's briefly review the process of shell reading instructions to create child processes:**<br>

* **Call getchar ( ) to read the input string from the terminal, separated by spaces, the first parameter is the file name, and the following parameters are the input parameters.**
* **Execute the system call fork() to generate a child process**
* **The child process executes the system call execve(), passes in the file name and input parameters to update the memory space of the child process, and points the program counter of the process to the first instruction. So far, the new process has been generated.**
* **If the parameter ends with & (in shell), the main process continues to wait for input. On the contrary, the main process executes the system call waitpid() until the child process is executed, and then continues to wait for input.**
<br>

**In these steps above: the last step is very interesting. Sometimes people will say that adding "&" after the command means creating a background process**<br>
**But I don't think it is the true [background process](https://en.wikipedia.org/wiki/Background_process#:~:text=A%20background%20process%20is%20a,%2C%20scheduling%2C%20and%20user%20notification.):**<br>
> **from wikipedia:<br>A background process is a computer process that runs behind the scenes (i.e., in the background) and without user intervention. Typical tasks for these processes include logging, system monitoring, scheduling, and user notification. The background process usually is a child process created by a control process for processing a computing task. After creation, the child process will run on its own, performing the task independent of the control process, freeing the control process of performing that task.**

**In this briefly description: "independent" is the key word. If the background process cannot run with foreground process independently. It will not be truly defined as this name.**<br>

![background](Sources/background.png)<br>

**Let us see this command above -- cat**<br>
**The independent ```cat``` command read the char from shell and writing them to standard output.**<br>
**In this figure. we can find that: if we create a background ```cat``` process. Then in shell we type ```ps``` command to let the shell write the process status to standard output.**<br>
**We will find that the background process ```cat``` was stopped unexpectly.**
<br>

**To understand why this error occurs. we need to figure out These three Stand Stream -- ```stdin``` ```stdout``` ```stderr```**<br>
**As we metioned above : Once a process is created, it will automatically creates a standard stream by opening 3 files (stdin stdout stderr), which stand for standard input, output, and error.**<br>
**Samely, when shell open (we create the shell process). it will also creates 3 standard stream. we use [lsof](https://en.wikipedia.org/wiki/Lsof) command to view the file opened by a process.**<br>
> **from wikipedia:<br>```lsof``` is a command meaning "list open files", which is used in many Unix-like systems to report a list of all open files and the processes that opened them.**

<br>
**Check out this code:**

```
ubuntu@ubuntu:~$ ps
    PID TTY          TIME CMD
   1799 pts/0    00:00:00 bash
   2367 pts/0    00:00:00 cat
  12849 pts/0    00:00:00 ps
```

```
ubuntu@ubuntu:~$ lsof -p 1799
COMMAND  PID   USER   FD   TYPE DEVICE SIZE/OFF   NODE NAME
bash    1799 ubuntu  cwd    DIR  179,2     4096 130305 /home/ubuntu
bash    1799 ubuntu  rtd    DIR  179,2     4096      2 /
bash    1799 ubuntu  txt    REG  179,2  1215072   1537 /usr/bin/bash
bash    1799 ubuntu  mem    REG  179,2   201272   8107 /usr/lib/locale/C.UTF-8/LC_CTYPE
bash    1799 ubuntu  mem    REG  179,2    51616   3407 /usr/lib/aarch64-linux-gnu/libnss_files-2.31.so
bash    1799 ubuntu  mem    REG  179,2  1518110   8106 /usr/lib/locale/C.UTF-8/LC_COLLATE
bash    1799 ubuntu  mem    REG  179,2  3035952   8118 /usr/lib/locale/locale-archive
bash    1799 ubuntu  mem    REG  179,2  1441800   3130 /usr/lib/aarch64-linux-gnu/libc-2.31.so
bash    1799 ubuntu  mem    REG  179,2    14528   3173 /usr/lib/aarch64-linux-gnu/libdl-2.31.so
bash    1799 ubuntu  mem    REG  179,2   187688   3534 /usr/lib/aarch64-linux-gnu/libtinfo.so.6.2
bash    1799 ubuntu  mem    REG  179,2       50   8114 /usr/lib/locale/C.UTF-8/LC_NUMERIC
bash    1799 ubuntu  mem    REG  179,2     3360   8117 /usr/lib/locale/C.UTF-8/LC_TIME
bash    1799 ubuntu  mem    REG  179,2      270   8112 /usr/lib/locale/C.UTF-8/LC_MONETARY
bash    1799 ubuntu  mem    REG  179,2       48   8111 /usr/lib/locale/C.UTF-8/LC_MESSAGES/SYS_LC_MESSAGES
bash    1799 ubuntu  mem    REG  179,2       34   8115 /usr/lib/locale/C.UTF-8/LC_PAPER
bash    1799 ubuntu  mem    REG  179,2       62   8113 /usr/lib/locale/C.UTF-8/LC_NAME
bash    1799 ubuntu  mem    REG  179,2      131   8105 /usr/lib/locale/C.UTF-8/LC_ADDRESS
bash    1799 ubuntu  mem    REG  179,2   146320   2803 /usr/lib/aarch64-linux-gnu/ld-2.31.so
bash    1799 ubuntu  mem    REG  179,2       47   8116 /usr/lib/locale/C.UTF-8/LC_TELEPHONE
bash    1799 ubuntu  mem    REG  179,2       23   8109 /usr/lib/locale/C.UTF-8/LC_MEASUREMENT
bash    1799 ubuntu  mem    REG  179,2    27004   2767 /usr/lib/aarch64-linux-gnu/gconv/gconv-modules.cache
bash    1799 ubuntu  mem    REG  179,2      252   8108 /usr/lib/locale/C.UTF-8/LC_IDENTIFICATION
bash    1799 ubuntu    0u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    1u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    2u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu  255u   CHR  136,0      0t0      3 /dev/pts/0
```

**The 4-th column FD and the very next column TYPE correspond to the File Descriptor and the File Descriptor type.**<br>

**Some of the values for the FD can be:**
```
cwd – Current Working Directory
txt – Text file
mem – Memory mapped file
mmap – Memory mapped device
```
**But the real file descriptor is under:**
```
NUMBER – Represent the actual file descriptor.
```
**The character after the number i.e "1u", represents the mode in which the file is opened. r for read, w for write, u for read and write.**<br>
**In this figure: we can find that the last few lines were 3 different file descripter 0 1 and 2. And that is the standard stream**<br>
```
bash    1799 ubuntu    0u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    1u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    2u   CHR  136,0      0t0      3 /dev/pts/0
```
**You may ask yourself where are these file descriptors physically and what is stored in /dev/pts/0 for instance.**<br>
**The ```dev/``` directory stands for device. It is important that all things in linux-like system were files. so in this place store all [device file](https://en.wikipedia.org/wiki/Device_file) (keyboard monitor etc.)**<br>
> **from [stackexchange](https://unix.stackexchange.com/questions/21280/difference-between-pts-and-tty) :<br>A tty is a regular terminal device (the console on your server, for example).<br>A pts is a psuedo terminal slave (an xterm or an ssh connection).**<br>

* **Standard input -- stand for 0 file descripter, is a stream from which a program reads its input data. For example, in shell process. The keyboard is the standard input, which means the keyboard directly write chars into that stdin file.**
* **Standard output -- stand for 1 file descripter, is a stream to which a program writes its output data, Usually be the monitor.**
* **Standard error -- is another output stream typically used by programs to output error messages or diagnostics. It is a stream independent of standard output and can be redirected separately.**<br>
![stdstream](Sources/stdstream.png)
<br>

**Let us back to that question: Why the background ```cat``` program will be stopped?**<br>
**Well, if we check the inode of two Standard streams of two different process.**<br>

```
COMMAND  PID   USER   FD   TYPE DEVICE SIZE/OFF   NODE NAME
bash    1799 ubuntu    0u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    1u   CHR  136,0      0t0      3 /dev/pts/0
bash    1799 ubuntu    2u   CHR  136,0      0t0      3 /dev/pts/0
```

```
cat     2367 ubuntu    0u   CHR  136,0      0t0      3 /dev/pts/0
cat     2367 ubuntu    1u   CHR  136,0      0t0      3 /dev/pts/0
cat     2367 ubuntu    2u   CHR  136,0      0t0      3 /dev/pts/0
```

**As we can see that: because of two processes' standard stream have the same Node number 3(we will talk about node number later), they direct to the same file.<br>**
**Because of this. When background process ```cat``` is waiting for standard input from keyboard (like the shell), if we type a command or other chars at shell. the OS cannot distingushes which one from the standard input will be executed. So the shell stopped the ```cat``` child process to avoid error.**<br>


**So what is Node number? and what is the File Descripter (fd) ?**<br>
<br>
**The file descriptor (fd) is an index created by the kernel to efficiently manage the opened file. It is a non-negative integer used to refer to the opened file. All system calls that perform I/O operations are Via file descriptor.**<br>

**Each file descriptor will correspond to an open file, and at the same time, different file descriptors will also point to the same file. The same file can be opened by different processes or opened multiple times in the same process.**<br>

**The system maintains a file descriptor table for each process. The value of the table starts from 0, so you will see the same file descriptor in different processes. In this case, the same file descriptor is possible Point to the same file, or it may point to different files. The specific situation needs to be analyzed in detail. To understand the specific situation, you need to look at the three data structures maintained by the kernel.**
<br>

* **File Descripters**
* **File Table**
* **Inode Table**
![3table](Sources/3table.png)

### Three Data Structures inside The Kernel
#### The per-process file description table
**For each process, the kernel maintains a table of open file descriptors. Each entry in this table records information about a single file descriptor(the one returned by the ```open()``` system call), including:**

* **a set of [flags](https://www.gnu.org/software/libc/manual/html_node/File-Status-Flags.html) controlling the operation of the file descriptor (actually there is just one such flag, the [close-on-exec flag](https://stackoverflow.com/questions/9583845/why-isnt-close-on-exec-the-default-configuration)**
* **a reference to the open file description**
<br>

#### The system-wide table of open file descriptions
**An open file description stores all information relating an open file. It's also called open file table or open file handles. Information includes:**
* **the current file offset (as updated by ```read()``` and ```write()```, or explicitly modified using ```lseek()```)**
* **status flags specified when opening the file (i.e, the flags argument to ```open()```)**
* **the file access mode (read-only, write-only, or read-write, as specified in ```open()```)**
* **setting relating to signal-driven I/O**
* **a reference to the i-node object for this file**

**For more details. I will make a conclution when learning implementation of File system**
<br>


#### The file system [i-node](https://en.wikipedia.org/wiki/Inode) table

* **file type (e.g, regular file, socket or FIFO) and permission**
* **a pointer to a list of blocks held on this file**
* **various properties of this file, including its size and time stamps, etc.**

<br>


**Here is a picture taken from the book The [Linux Programming Interface](https://man7.org/tlpi/), which clearly depicts the relationship between file descriptors, open file descriptions and i-nodes. In this situation, two processes have a number of open file descriptors.**<br>

![3tables](Sources/3tables.png)

**Let us do a little analysis on this diagram:<br>**
* **In this diagram, descriptors 1 and 20 of process A both refer to the same open file description (labeled 23). This situation may arise as a result of a call the ```dup()``` , ```dup2()``` or ```fcntl()``` (which I will metioned later)**
* **Descriptor 2 of process A and descriptor 2 of process B refer to a single open file description(73). This scenario could occur after a call to ```fork()```(i.e, process A is the parent of process B, or vice versa), or if one process passes an open file descriptor to another process using a UNIX domain socket.**
* **Finally, we see that descriptor 0 of process A and descriptor 3 of process B refer to different open file descriptions, but that these descriptions refer to the same i-node table entry (1976) -- in other words, to the same file. A similar situation could occur if a single process open the same file twice.** 

<br>

**After that conclution above: let's see another question:<br>**
**Why does unix need these 3 tables?**<br>

**See another example, then maybe we will get the key of the implementation of Unix file system:<br>**

![samefile](Sources/samefile.png)

**From that i-node table in that figure below: it is clear that these two processes opened the same file.**<br>
**But due to the difference file table -- these two processes don't read the file with same offset and other permissions (read write etc.)**<br>
**In my case. This arrangement has these advantages:<br>**

* **This arrangement achieved independence between different processes by making file table<br>**
* **In this arrangement, users can flexibly choose the mode of opening multiple files with different progress or the same progress.<br>**
* **Because of the file descripter. The processes just need to pass the fd to the kernel to open files among the massive files. That helps the process more efficient<br>**
