### Operating Systerms Design and Implementation Notes

##### By Jiawei Wang

# 7. Systerm Calls (3)
<br>

![Systermcalls](Sources/Systermcalls.png)<br>


<!-- vim-markdown-toc GFM -->

* [System Calls for Directory Management](#system-calls-for-directory-management)
    * [mkdir, rmdir -- make, remove a dir](#mkdir-rmdir----make-remove-a-dir)
    * [link -- make a new name for a file](#link----make-a-new-name-for-a-file)

<!-- vim-markdown-toc -->


## System Calls for Directory Management

### mkdir, rmdir -- make, remove a dir

### link -- make a new name for a file
**int [link](https://man7.org/linux/man-pages/man2/link.2.html) (const char oldpath(pointer), const char newpath(pointer));**<br>
**link() creates a new hard link to an existing file.**


