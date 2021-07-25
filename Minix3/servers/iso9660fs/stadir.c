#include "inc.h"
#include <sys/stat.h>
#include <sys/statfs.h>
#include <minix/com.h>
#include <string.h>
#include <time.h>

#include <minix/vfsif.h>


FORWARD _PROTOTYPE(int stat_dir_record, (struct dir_record *dir, int pipe_pos,
					 int who_e, cp_grant_id_t gid));

/* This function returns all the info about a particular inode. It's missing
 * the recording date because of a bug in the standard functions stdtime.
 * Once the bug is fixed the function can be called inside this function to
 * return the date. */
/*===========================================================================*
 *				stat_dir_record				     *
 *===========================================================================*/
PRIVATE int stat_dir_record(dir, pipe_pos, who_e, gid)
register struct dir_record *dir;	/* pointer to dir record to stat */
int pipe_pos;   		/* position in a pipe, supplied by fstat() */
int who_e;			/* Caller endpoint */
cp_grant_id_t gid;		/* grant for the stat buf */
{

/* Common code for stat and fstat system calls. */
  struct stat statbuf;
  mode_t mo;
  int r, s;
  struct tm ltime;
  time_t time1;

  statbuf.st_dev = fs_dev;	/* the device of the file */
  statbuf.st_ino = ID_DIR_RECORD(dir); /* the id of the dir record */
  statbuf.st_mode = dir->d_mode; /* flags of the file */
  statbuf.st_nlink = dir->d_count; /* times this file is used */
  statbuf.st_uid = 0;		/* user root */
  statbuf.st_gid = 0;		/* group operator */
  statbuf.st_rdev = NO_DEV;
  statbuf.st_size = dir->d_file_size;	/* size of the file */

  ltime.tm_year = dir->rec_date[0];
  ltime.tm_mon = dir->rec_date[1] - 1;
  ltime.tm_mday = dir->rec_date[2];
  ltime.tm_hour = dir->rec_date[3];
  ltime.tm_min = dir->rec_date[4];
  ltime.tm_sec = dir->rec_date[5];
  ltime.tm_isdst = 0;

  if (dir->rec_date[6] != 0)
    ltime.tm_hour += dir->rec_date[6] / 4;

  time1 = mktime(&ltime);

  statbuf.st_atime = time1;
  statbuf.st_mtime = time1;
  statbuf.st_ctime = time1;

  /* Copy the struct to user space. */
  r = sys_safecopyto(who_e, gid, 0, (vir_bytes) &statbuf,
  		(phys_bytes) sizeof(statbuf), D);
  
  return(r);
}

/* This function is a wrapper to the function above. It is called with the
 * request. */
/*===========================================================================*
 *                             fs_stat					     *
 *===========================================================================*/
PUBLIC int fs_stat()
{
  register int r;              /* return value */
  struct dir_record *dir;
  r = EINVAL;

  if ((dir = get_dir_record(fs_m_in.REQ_INODE_NR)) != NULL) {
    r = stat_dir_record(dir, 0, fs_m_in.m_source, fs_m_in.REQ_GRANT);
    release_dir_record(dir);
  } else
    printf("I9660FS(%d) fs_stat() failed\n", SELF_E);

  return r;
}

/*===========================================================================*
 *				fs_fstatfs				     *
 *===========================================================================*/
PUBLIC int fs_fstatfs()
{
  struct statfs st;
  int r;

  st.f_bsize = v_pri.logical_block_size_l;
  
  /* Copy the struct to user space. */
  r = sys_safecopyto(fs_m_in.m_source, fs_m_in.REQ_GRANT, 0,
	(vir_bytes) &st, (phys_bytes) sizeof(st), D);
  
  return(r);
}

