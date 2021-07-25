/* In this file are declared all the constant used by the server. */

#define WRITE_LOG(TEXT) printf("iso9660fs: " TEXT "\n");

#define ISO9660_STANDARD_ID "CD001" /* Standard code for ISO9660 filesystems */

#define NR_DIR_RECORDS 256	/* Number of dir records to use at the same
				 * time. */
#define NR_ATTR_RECS 256	/* Number of extended attributes that is 
				 * possible to use at the same time */
/* #define NR_ID_INODES 1024 */	/* The ISO9660 doesn't save the inode numbers.
				 * There is a table that assign to every inode
				 * a particular id. This number defines the 
				 * maximum number of ids the finesystem can 
				 * handle */

#define NO_ADDRESS -1		/* Error constants */
#define NO_FREE_INODES -1

#define PATH_PENULTIMATE 001   /* parse_path stops at last but one name */
#define PATH_NONSYMBOLIC 004   /* parse_path scans final name if symbolic */

#define DIR_ENTRY_SIZE       sizeof (struct direct)
#define NR_DIR_ENTRIES(b)   ((b)/DIR_ENTRY_SIZE)

/* Below there are constant of the ISO9660 fs */

#define ISO9660_SUPER_BLOCK_POSITION        (32768)
#define ISO9660_MIN_BLOCK_SIZE		 2048

/* SIZES FIELDS ISO9660 STRUCTURES */
#define ISO9660_SIZE_STANDARD_ID 5
#define ISO9660_SIZE_BOOT_SYS_ID 32
#define ISO9660_SIZE_BOOT_ID 32

#define ISO9660_SIZE_SYS_ID 32
#define ISO9660_SIZE_VOLUME_ID 32
#define ISO9660_SIZE_VOLUME_SET_ID 128
#define ISO9660_SIZE_PUBLISHER_ID 128
#define ISO9660_SIZE_DATA_PREP_ID 128
#define ISO9660_SIZE_APPL_ID 128
#define ISO9660_SIZE_COPYRIGHT_FILE_ID 37
#define ISO9660_SIZE_ABSTRACT_FILE_ID 37
#define ISO9660_SIZE_BIBL_FILE_ID 37

#define ISO9660_SIZE_VOL_CRE_DATE 17
#define ISO9660_SIZE_VOL_MOD_DATE 17
#define ISO9660_SIZE_VOL_EXP_DATE 17
#define ISO9660_SIZE_VOL_EFF_DATE 17

#define ISO9660_SIZE_ESCAPE_SQC 32
#define ISO9660_SIZE_PART_ID 32

#define ISO9660_SIZE_SYSTEM_USE 64

/* maximum size of length of name file used in dir records */
#define ISO9660_MAX_FILE_ID_LEN 32

#define MFS_DEV_READ    10001
#define MFS_DEV_WRITE   10002
#define MFS_DEV_SCATTER 10003
#define MFS_DEV_GATHER  10004

#define END_OF_FILE   (-104)	/* eof detected */

#define	offsetof(type, field)	((size_t)(&((type *)0)->field))
