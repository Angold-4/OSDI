### Operating Systerms Design and Implementation Notes

##### By Jiawei Wang

# 7. System Calls (3)
<br>

![Systermcalls](Sources/Systermcalls.png)<br>


<!-- vim-markdown-toc GFM -->

* [System Calls for Directory Management](#system-calls-for-directory-management)
    * [mkdir, rmdir -- make, remove a dir](#mkdir-rmdir----make-remove-a-dir)
    * [link -- make a new name for a file](#link----make-a-new-name-for-a-file)
    * [mount -- mount filesystem](#mount----mount-filesystem)
    * [chdir -- change working directory](#chdir----change-working-directory)
    * [chroot - change root directory](#chroot---change-root-directory)

<!-- vim-markdown-toc -->


## System Calls for Directory Management

### mkdir, rmdir -- make, remove a dir
**int [mkdir](https://man7.org/linux/man-pages/man2/mkdir.2.html) (const char pathname(pointer), mode_t mode);**<br>
**```mkdir()``` attempts to create a directory named pathname.**
<br>

**int [rmdir](https://man7.org/linux/man-pages/man2/rmdir.2.html) (const char pathname(pointer));**<br>
**```rmdir()``` deletes a directory, which must be empty.**
<br>

### link -- make a new name for a file
**int [link](https://man7.org/linux/man-pages/man2/link.2.html) (const char oldpath(pointer), const char newpath(pointer));**<br>
**```link()``` creates a new hard link to an existing file. The newly created directory will be owned by the effective user ID of the process.**
<br>

**Understanding how link works will probably make it clearer what it does. Let's see an exmple:**<br>
![link1](sources/link1.png)<br>
**consider the situation of fig.(a). here are two users, ast and jim, each having their own directories with some files. if ast now executes a program containing the system call:**

```c
link("/usr/jim/memo", ""/usr/ast/note");
```
**the file memo in jim’s directory is now entered into ast’s directory under the name note. thereafter, ```/usr/jim/memo``` and ```/usr/ast/note``` refer to the same file.**
**in the [previous note](https://github.com/angold-4/osdi/blob/master/chapters/chapter1/6syscall-2.md). i mentioned that there are 3 tables in unix-like file system:<br>**
**the fd table, the file table and the inode table.**<br>

**every file in unix has a unique number, its i-number, that identifies it. this i-number is an index into a table of i-nodes, one per file, telling who owns the file, where its disk blocks are, and so on.**
> **a directory is simply a file containing a set of (i-number, ascii name) pairs. in the first versions of unix, each directory entry was 16 bytes—2 bytes for the i-number and 14 bytes for the name. a more complicated structure is needed to support long file names, but conceptually a directory is still a set of (i-number, ascii name) pairs.**

**in this example. after the ```link()``` system call, it will be like fig.(b):<br>**
![link2](sources/link2.png)<br>

**in fig.(b), two entries have the same i-number (70) and thus refer to the same file.<br>if either one is later removed, using the unlink system call, the other one remains. if both are removed, unix sees that no entries to the file exist (a field in the i-node keeps track of the number of directory entries pointing to the file), so the file is removed from the disk.**
<br>

### mount -- mount filesystem
```#include <sys/mount.h>```

**int [mount](https://man7.org/linux/man-pages/man2/mount.2.html)(const char source(pointer), const char target(pointer),<br>const char filesystemtype(pointer), unsigned long mountflags<br>const void data(pointer));**

**the mount system call allows two file systems to be merged into one. A common situation is to have the root file system con- taining the binary (executable) versions of the common commands and other heavily used files, on a hard disk. The user can then insert a CD-ROM with files to be read into the CD-ROM drive.**<br>

![mount](Sources/mount.png)

**By executing the mount system call, the CD-ROM file system can be attached to the root file system, as shown in Fig. 1-15. A typical statement in C to perform the mount is**<br>
```c
mount(′′/dev/cdrom0′′, ′′/mnt′′, 0);
```
**Where the first parameter is the name of a block special file for CD-ROM drive 0, the second parameter is the place in the tree where it is to be mounted, and the third one tells whether the file system is to be mounted read-write or read-only.**<br>

> **After the mount call, a file on CD-ROM drive 0 can be accessed by just using its path from the root directory or the working directory, without regard to which drive it is on. In fact, second, third, and fourth drives can also be mounted**<br>

<br>

### sync -- commit filesystem caches to disk

```#include <unistd.h>```<br>
**void [sync](https://man7.org/linux/man-pages/man2/sync.2.html) (void);**<br>
**sync() causes all pending modifications to filesystem metadata and cached file data to be written to the underlying filesystems.**


> **MINIX 3 maintains a block cache cache of recently used blocks in main memory to avoid having to read them from the disk if they are used again quickly. If a block in the cache is modified (by a write on a file) and the system crashes before the modified block is written out to disk, the file system will be damaged. To limit the potential damage, it is important to flush the cache periodically, so that the amount of data lost by a crash will be small.**
<br>

**The system call sync tells MINIX 3 to write out all the cache blocks that have been modified since being read in. When MINIX 3 is started up, a program called update is started as a back- ground process to do a sync every 30 seconds, to keep flushing the cache.**
<br>

### chdir -- change working directory

### chroot - change root directory

