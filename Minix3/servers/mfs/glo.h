/* EXTERN should be extern except for the table file */
#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

EXTERN off_t rdahedpos;		/* position to read ahead */
EXTERN struct inode *rdahed_inode;	/* pointer to inode to read ahead */

/* The following variables are used for returning results to the caller. */
EXTERN int err_code;		/* temporary storage for error number */
EXTERN int rdwt_err;		/* status of last disk i/o request */

EXTERN int cch[NR_INODES];

extern char dot1[2];   /* dot1 (&dot1[0]) and dot2 (&dot2[0]) have a special */
extern char dot2[3];   /* meaning to search_dir: no access permission check. */

extern _PROTOTYPE (int (*fs_call_vec[]), (void) ); /* fs call table */

EXTERN message fs_m_in;
EXTERN message fs_m_out;
EXTERN int FS_STATE;

EXTERN uid_t caller_uid;
EXTERN gid_t caller_gid;

EXTERN time_t boottime;		/* time in seconds at system boot */
EXTERN int use_getuptime2;	/* Should be removed togetherwith boottime */

EXTERN int req_nr;

EXTERN int SELF_E;

EXTERN struct inode *chroot_dir;

EXTERN short path_processed;      /* number of characters processed */
EXTERN char user_path[PATH_MAX+1];  /* pathname to be processed */
EXTERN char *vfs_slink_storage;
EXTERN int Xsymloop;

EXTERN dev_t fs_dev;    	/* The device that is handled by this FS proc.
				 */
EXTERN char fs_dev_label[16];	/* Name of the device driver that is handled
				 * by this FS proc.
				 */
EXTERN int unmountdone;
EXTERN int exitsignaled;

/* our block size. */
EXTERN int fs_block_size;

/* Buffer cache. */
EXTERN struct buf buf[NR_BUFS];
EXTERN struct buf *buf_hash[NR_BUFS];   /* the buffer hash table */

