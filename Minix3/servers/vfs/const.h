/* Tables sizes */
#define NR_FILPS         512	/* # slots in filp table */
#define NR_LOCKS           8	/* # slots in the file locking table */
#define NR_MNTS             8	/* # slots in mount table */
#define NR_VNODES         512	/* # slots in vnode table */

/* Miscellaneous constants */
#define SU_UID 	 ((uid_t) 0)	/* super_user's uid_t */
#define SERVERS_UID ((uid_t) 11) /* who may do FSSIGNON */
#define SYS_UID  ((uid_t) 0)	/* uid_t for processes MM and INIT */
#define SYS_GID  ((gid_t) 0)	/* gid_t for processes MM and INIT */

#define FP_BLOCKED_ON_NONE	0 /* not blocked */
#define FP_BLOCKED_ON_PIPE	1 /* susp'd on pipe */
#define FP_BLOCKED_ON_LOCK	2 /* susp'd on lock */
#define FP_BLOCKED_ON_POPEN	3 /* susp'd on pipe open */
#define FP_BLOCKED_ON_SELECT	4 /* susp'd on select */
#define FP_BLOCKED_ON_DOPEN	5 /* susp'd on device open */
#define FP_BLOCKED_ON_OTHER	6 /* blocked on other process, check 
				     fp_task to find out */

/* test if the process is blocked on something */
#define fp_is_blocked(fp)	((fp)->fp_blocked_on != FP_BLOCKED_ON_NONE)

#define DUP_MASK        0100	/* mask to distinguish dup2 from dup */

#define LOOK_UP            0 /* tells search_dir to lookup string */
#define ENTER              1 /* tells search_dir to make dir entry */
#define DELETE             2 /* tells search_dir to delete entry */
#define IS_EMPTY           3 /* tells search_dir to ret. OK or ENOTEMPTY */  

#define SYMLOOP		16

#define ROOT_INODE         1		/* inode number for root directory */

/* Args to dev_io */
#define VFS_DEV_READ	2001
#define	VFS_DEV_WRITE	2002
#define VFS_DEV_SCATTER	2003
#define VFS_DEV_GATHER	2004
#define VFS_DEV_IOCTL	2005
#define VFS_DEV_SELECT	2006
