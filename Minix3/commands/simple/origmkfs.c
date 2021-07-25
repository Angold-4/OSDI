/* mkfs  -  make the MINIX filesystem	Authors: Tanenbaum et al. */

/*	Authors: Andy Tanenbaum, Paul Ogilvie, Frans Meulenbroeks, Bruce Evans
 *
 * This program can make both version 1 and version 2 file systems, as follows:
 *	mkfs /dev/fd0 1200	# Version 2 (default)
 *	mkfs -1 /dev/fd0 360	# Version 1
 *
 */

#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/minlib.h>
#include "../../servers/fs/const.h"
#if (MACHINE == IBM_PC)
#include <minix/partition.h>
#include <minix/u64.h>
#include <sys/ioctl.h>
#endif

#undef EXTERN
#define EXTERN			/* get rid of EXTERN by making it null */
#include "../../servers/fs/type.h"
#include "../../servers/fs/super.h"
#include <minix/fslib.h>

#ifndef DOS
#ifndef UNIX
#define UNIX
#endif
#endif

#undef BLOCK_SIZE
#define BLOCK_SIZE 1024

#define INODE_MAP            2
#define MAX_TOKENS          10
#define LINE_LEN           200
#define BIN                  2
#define BINGRP               2
#define BIT_MAP_SHIFT       13
#define N_BLOCKS         (1024L * 1024)
#define N_BLOCKS16	  (128L * 1024)
#define INODE_MAX       ((unsigned) 65535)

/* You can make a really large file system on a 16-bit system, but the array
 * of bits that get_block()/putblock() needs gets a bit big, so we can only
 * prefill MAX_INIT blocks.  (16-bit fsck can't check a file system larger
 * than N_BLOCKS16 anyway.)
 */
#define MAX_INIT	 (sizeof(char *) == 2 ? N_BLOCKS16 : N_BLOCKS)


#ifdef DOS
maybedefine O_RDONLY 4		/* O_RDONLY | BINARY_BIT */
 maybedefine BWRITE 5		/* O_WRONLY | BINARY_BIT */
#endif

#if (MACHINE == ATARI)
int isdev;
#endif

extern char *optarg;
extern int optind;

int next_zone, next_inode, zone_size, zone_shift = 0, zoff;
block_t nrblocks;
int inode_offset, lct = 0, disk, fd, print = 0, file = 0;
unsigned int nrinodes;
int override = 0, simple = 0, dflag;
int donttest;			/* skip test if it fits on medium */
char *progname;

long current_time, bin_time;
char zero[BLOCK_SIZE], *lastp;
char umap[MAX_INIT / 8];	/* bit map tells if block read yet */
block_t zone_map;		/* where is zone map? (depends on # inodes) */
int inodes_per_block;
int fs_version;
block_t max_nrblocks;

FILE *proto;

_PROTOTYPE(int main, (int argc, char **argv));
_PROTOTYPE(block_t sizeup, (char *device));
_PROTOTYPE(void super, (zone_t zones, Ino_t inodes));
_PROTOTYPE(void rootdir, (Ino_t inode));
_PROTOTYPE(void eat_dir, (Ino_t parent));
_PROTOTYPE(void eat_file, (Ino_t inode, int f));
_PROTOTYPE(void enter_dir, (Ino_t parent, char *name, Ino_t child));
_PROTOTYPE(void incr_size, (Ino_t n, long count));
_PROTOTYPE(PRIVATE ino_t alloc_inode, (int mode, int usrid, int grpid));
_PROTOTYPE(PRIVATE zone_t alloc_zone, (void));
_PROTOTYPE(void add_zone, (Ino_t n, zone_t z, long bytes, long cur_time));
_PROTOTYPE(void add_z_1, (Ino_t n, zone_t z, long bytes, long cur_time));
_PROTOTYPE(void add_z_2, (Ino_t n, zone_t z, long bytes, long cur_time));
_PROTOTYPE(void incr_link, (Ino_t n));
_PROTOTYPE(void insert_bit, (block_t block, int bit));
_PROTOTYPE(int mode_con, (char *p));
_PROTOTYPE(void getline, (char line[LINE_LEN], char *parse[MAX_TOKENS]));
_PROTOTYPE(void check_mtab, (char *devname));
_PROTOTYPE(long file_time, (int f));
_PROTOTYPE(void pexit, (char *s));
_PROTOTYPE(void copy, (char *from, char *to, int count));
_PROTOTYPE(void print_fs, (void));
_PROTOTYPE(int read_and_set, (block_t n));
_PROTOTYPE(void special, (char *string));
_PROTOTYPE(void get_block, (block_t n, char buf[BLOCK_SIZE]));
_PROTOTYPE(void put_block, (block_t n, char buf[BLOCK_SIZE]));
_PROTOTYPE(void cache_init, (void));
_PROTOTYPE(void flush, (void));
_PROTOTYPE(void mx_read, (int blocknr, char buf[BLOCK_SIZE]));
_PROTOTYPE(void mx_write, (int blocknr, char buf[BLOCK_SIZE]));
_PROTOTYPE(void dexit, (char *s, int sectnum, int err));
_PROTOTYPE(void usage, (void));

/*================================================================
 *                    mkfs  -  make filesystem
 *===============================================================*/
int main(argc, argv)
int argc;
char *argv[];
{
  int nread, mode, usrid, grpid, ch;
  block_t blocks;
  block_t i;
  ino_t root_inum;
  ino_t inodes;
  zone_t zones;
  char *token[MAX_TOKENS], line[LINE_LEN];
  struct stat statbuf;

  /* Get two times, the current time and the mod time of the binary of
   * mkfs itself.  When the -d flag is used, the later time is put into
   * the i_mtimes of all the files.  This feature is useful when
   * producing a set of file systems, and one wants all the times to be
   * identical. First you set the time of the mkfs binary to what you
   * want, then go.
   */
  current_time = time((time_t *) 0);	/* time mkfs is being run */
  stat(argv[0], &statbuf);
  bin_time = statbuf.st_mtime;	/* time when mkfs binary was last modified */

  /* Process switches. */
  progname = argv[0];
  blocks = 0;
  i = 0;
  fs_version = 2;
  inodes_per_block = V2_INODES_PER_BLOCK(BLOCK_SIZE);
  max_nrblocks = N_BLOCKS;
  while ((ch = getopt(argc, argv, "1b:di:lot")) != EOF)
	switch (ch) {
	    case '1':
		fs_version = 1;
		inodes_per_block = V1_INODES_PER_BLOCK;
		max_nrblocks = 0xFFFF;
		break;
	    case 'b':
		blocks = strtoul(optarg, (char **) NULL, 0);
		break;
	    case 'd':
		dflag = 1;
		current_time = bin_time;
		break;
	    case 'i':
		i = strtoul(optarg, (char **) NULL, 0);
		break;
	    case 'l':	print = 1;	break;
	    case 'o':	override = 1;	break;
	    case 't':	donttest = 1;	break;
	    default:	usage();
	}

  /* Determine the size of the device if not specified as -b or proto. */
  if (argc - optind == 1 && blocks == 0) blocks = sizeup(argv[optind]);
  printf("%lu blocks\n", blocks);

  /* The remaining args must be 'special proto', or just 'special' if the
   * block size has already been specified.
   */
  if (argc - optind != 2 && (argc - optind != 1 || blocks == 0)) usage();

  /* Check special. */
  check_mtab(argv[optind]);

  /* Check and start processing proto. */
  optarg = argv[++optind];
  if (optind < argc && (proto = fopen(optarg, "r")) != NULL) {
	/* Prototype file is readable. */
	lct = 1;
	getline(line, token);	/* skip boot block info */

	/* Read the line with the block and inode counts. */
	getline(line, token);
	blocks = atol(token[0]);
	if (blocks > max_nrblocks) {
		printf("%d > %d\n",  blocks, max_nrblocks);
		pexit("Block count too large");
	}
	if (sizeof(char *) == 2 && blocks > N_BLOCKS16) {
		fprintf(stderr,
		"%s: warning: FS is larger than the %dM that fsck can check!\n",
			progname, (int) (N_BLOCKS16 / (1024L * 1024)));
	}
	inodes = atoi(token[1]);

	/* Process mode line for root directory. */
	getline(line, token);
	mode = mode_con(token[0]);
	usrid = atoi(token[1]);
	grpid = atoi(token[2]);
  } else {
	lct = 0;
	if (optind < argc) {
		/* Maybe the prototype file is just a size.  Check. */
		blocks = strtoul(optarg, (char **) NULL, 0);
		if (blocks == 0) pexit("Can't open prototype file");
	}
	if (i == 0) {
		/* The default for inodes is 3 blocks per inode, rounded up
		 * to fill an inode block.  Above 20M, the average files are
		 * sure to be larger because it is hard to fill up 20M with
		 * tiny files, so reduce the default number of inodes.  This
		 * default can always be overridden by using the -i option.
		 */
		i = blocks / 3;
		if (blocks >= 20000) i = blocks / 4;
		if (blocks >= 40000) i = blocks / 5;
		if (blocks >= 60000) i = blocks / 6;
		if (blocks >= 80000) i = blocks / 7;
		if (blocks >= 100000) i = blocks / 8;
		i += inodes_per_block - 1;
		i = i / inodes_per_block * inodes_per_block;
		if (i > INODE_MAX) i = INODE_MAX;
	}
	if (blocks < 5) pexit("Block count too small");
	if (blocks > max_nrblocks)  {
		printf("%d > %d\n",  blocks, max_nrblocks);
		pexit("Block count too large");
	}
	if (i < 1) pexit("Inode count too small");
	if (i > INODE_MAX) pexit("Inode count too large");
	inodes = (ino_t) i;

	/* Make simple file system of the given size, using defaults. */
	mode = 040777;
	usrid = BIN;
	grpid = BINGRP;
	simple = 1;
  }
  nrblocks = blocks;
  nrinodes = inodes;

  /* Open special. */
  special(argv[--optind]);

#ifdef UNIX
  if (!donttest) {
	static short testb[BLOCK_SIZE / sizeof(short)];

	/* Try writing the last block of partition or diskette. */
	lseek(fd, (off_t) (blocks - 1) * BLOCK_SIZE, SEEK_SET);
	testb[0] = 0x3245;
	testb[1] = 0x11FF;
	if (write(fd, (char *) testb, BLOCK_SIZE) != BLOCK_SIZE)
		pexit("File system is too big for minor device");
	sync();			/* flush write, so if error next read fails */
	lseek(fd, (off_t) (blocks - 1) * BLOCK_SIZE, SEEK_SET);
	testb[0] = 0;
	testb[1] = 0;
	nread = read(fd, (char *) testb, BLOCK_SIZE);
	if (nread != BLOCK_SIZE || testb[0] != 0x3245 || testb[1] != 0x11FF)
		pexit("File system is too big for minor device");
	lseek(fd, (off_t) (blocks - 1) * BLOCK_SIZE, SEEK_SET);
	testb[0] = 0;
	testb[1] = 0;
	if (write(fd, (char *) testb, BLOCK_SIZE) != BLOCK_SIZE)
		pexit("File system is too big for minor device");
	lseek(fd, 0L, SEEK_SET);
  }
#endif

  /* Make the file-system */

  cache_init();

#if (MACHINE == ATARI)
  if (isdev) {
	char block0[BLOCK_SIZE];
	get_block((block_t) 0, block0);
	/* Need to read twice; first time gets an empty block */
	get_block((block_t) 0, block0);
	/* Zero parts of the boot block so the disk won't be
	 * recognized as a tos disk any more. */
	block0[0] = block0[1] = 0;	/* branch code to boot code    */
	strncpy(&block0[2], "MINIX ", (size_t) 6);
	block0[16] = 0;		/* number of FATS              */
	block0[17] = block0[18] = 0;	/* number of dir entries       */
	block0[22] = block0[23] = 0;	/* sectors/FAT                 */
	bzero(&block0[30], 480);/* boot code                   */
	put_block((block_t) 0, block0);
  } else
#endif

	put_block((block_t) 0, zero);	/* Write a null boot block. */

  zone_shift = 0;		/* for future use */
  zones = nrblocks >> zone_shift;

  super(zones, inodes);

  root_inum = alloc_inode(mode, usrid, grpid);
  rootdir(root_inum);
  if (simple == 0) eat_dir(root_inum);

  if (print) print_fs();
  flush();
  return(0);

  /* NOTREACHED */
}				/* end main */


/*================================================================
 *                    sizeup  -  determine device size
 *===============================================================*/
block_t sizeup(device)
char *device;
{
  int fd;
  struct partition entry;

  if ((fd = open(device, O_RDONLY)) == -1) return 0;
  if (ioctl(fd, DIOCGETP, &entry) == -1) entry.size = cvu64(0);
  close(fd);
  return div64u(entry.size, BLOCK_SIZE);
}


/*================================================================
 *                 super  -  construct a superblock
 *===============================================================*/

void super(zones, inodes)
zone_t zones;
ino_t inodes;
{
  unsigned int i;
  int inodeblks;
  int initblks;

  zone_t initzones, nrzones, v1sq, v2sq;
  zone_t zo;
  struct super_block *sup;
  char buf[BLOCK_SIZE], *cp;

  for (cp = buf; cp < &buf[BLOCK_SIZE]; cp++) *cp = 0;
  sup = (struct super_block *) buf;	/* lint - might use a union */

  sup->s_ninodes = inodes;
  if (fs_version == 1) {
	sup->s_nzones = zones;
  } else {
	sup->s_nzones = 0;	/* not used in V2 - 0 forces errors early */
	sup->s_zones = zones;
  }
  sup->s_imap_blocks = bitmapsize((bit_t) (1 + inodes), BLOCK_SIZE);
  sup->s_zmap_blocks = bitmapsize((bit_t) zones, BLOCK_SIZE);
  inode_offset = sup->s_imap_blocks + sup->s_zmap_blocks + 2;
  inodeblks = (inodes + inodes_per_block - 1) / inodes_per_block;
  initblks = inode_offset + inodeblks;
  initzones = (initblks + (1 << zone_shift) - 1) >> zone_shift;
  nrzones = nrblocks >> zone_shift;
  sup->s_firstdatazone = (initblks + (1 << zone_shift) - 1) >> zone_shift;
  zoff = sup->s_firstdatazone - 1;
  sup->s_log_zone_size = zone_shift;
  if (fs_version == 1) {
	sup->s_magic = SUPER_MAGIC;	/* identify super blocks */
	v1sq = (zone_t) V1_INDIRECTS * V1_INDIRECTS;
	zo = V1_NR_DZONES + (long) V1_INDIRECTS + v1sq;
  } else {
	sup->s_magic = SUPER_V2;/* identify super blocks */
	v2sq = (zone_t) V2_INDIRECTS(BLOCK_SIZE) * V2_INDIRECTS(BLOCK_SIZE);
	zo = V2_NR_DZONES + (zone_t) V2_INDIRECTS(BLOCK_SIZE) + v2sq;
  }
  sup->s_max_size = zo * BLOCK_SIZE;
  zone_size = 1 << zone_shift;	/* nr of blocks per zone */

  put_block((block_t) 1, buf);

  /* Clear maps and inodes. */
  for (i = 2; i < initblks; i++) put_block((block_t) i, zero);

  next_zone = sup->s_firstdatazone;
  next_inode = 1;

  zone_map = INODE_MAP + sup->s_imap_blocks;

  insert_bit(zone_map, 0);	/* bit zero must always be allocated */
  insert_bit((block_t) INODE_MAP, 0);	/* inode zero not used but
					 * must be allocated */
}


/*================================================================
 *              rootdir  -  install the root directory
 *===============================================================*/
void rootdir(inode)
ino_t inode;
{
  zone_t z;

  z = alloc_zone();
  add_zone(inode, z, 32L, current_time);
  enter_dir(inode, ".", inode);
  enter_dir(inode, "..", inode);
  incr_link(inode);
  incr_link(inode);
}


/*================================================================
 *	    eat_dir  -  recursively install directory
 *===============================================================*/
void eat_dir(parent)
ino_t parent;
{
  /* Read prototype lines and set up directory. Recurse if need be. */
  char *token[MAX_TOKENS], *p;
  char line[LINE_LEN];
  int mode, usrid, grpid, maj, min, f;
  ino_t n;
  zone_t z;
  long size;

  while (1) {
	getline(line, token);
	p = token[0];
	if (*p == '$') return;
	p = token[1];
	mode = mode_con(p);
	usrid = atoi(token[2]);
	grpid = atoi(token[3]);
	if (grpid & 0200) fprintf(stderr, "A.S.Tanenbaum\n");
	n = alloc_inode(mode, usrid, grpid);

	/* Enter name in directory and update directory's size. */
	enter_dir(parent, token[0], n);
	incr_size(parent, 16L);

	/* Check to see if file is directory or special. */
	incr_link(n);
	if (*p == 'd') {
		/* This is a directory. */
		z = alloc_zone();	/* zone for new directory */
		add_zone(n, z, 32L, current_time);
		enter_dir(n, ".", n);
		enter_dir(n, "..", parent);
		incr_link(parent);
		incr_link(n);
		eat_dir(n);
	} else if (*p == 'b' || *p == 'c') {
		/* Special file. */
		maj = atoi(token[4]);
		min = atoi(token[5]);
		size = 0;
		if (token[6]) size = atoi(token[6]);
		size = BLOCK_SIZE * size;
		add_zone(n, (zone_t) ((maj << 8) | min), size, current_time);
	} else {
		/* Regular file. Go read it. */
		if ((f = open(token[4], O_RDONLY)) < 0) {
			fprintf(stderr, "%s: Can't open %s: %s\n",
				progname, token[4], strerror(errno));
		} else
			eat_file(n, f);
	}
  }

}

/*================================================================
 * 		eat_file  -  copy file to MINIX
 *===============================================================*/
/* Zonesize >= blocksize */
void eat_file(inode, f)
ino_t inode;
int f;
{
  int ct, i, j, k;
  zone_t z;
  char buf[BLOCK_SIZE];
  long timeval;

  do {
	for (i = 0, j = 0; i < zone_size; i++, j += ct) {
		for (k = 0; k < BLOCK_SIZE; k++) buf[k] = 0;
		if ((ct = read(f, buf, BLOCK_SIZE)) > 0) {
			if (i == 0) z = alloc_zone();
			put_block((z << zone_shift) + i, buf);
		}
	}
	timeval = (dflag ? current_time : file_time(f));
	if (ct) add_zone(inode, z, (long) j, timeval);
  } while (ct == BLOCK_SIZE);
  close(f);
}



/*================================================================
 *	    directory & inode management assist group
 *===============================================================*/
void enter_dir(parent, name, child)
ino_t parent, child;
char *name;
{
  /* Enter child in parent directory */
  /* Works for dir > 1 block and zone > block */
  int i, j, k, l, off;
  block_t b;
  zone_t z;
  char *p1, *p2;
  struct direct dir_entry[NR_DIR_ENTRIES(BLOCK_SIZE)];
  d1_inode ino1[V1_INODES_PER_BLOCK];
  d2_inode ino2[V2_INODES_PER_BLOCK(BLOCK_SIZE)];
  int nr_dzones;

  b = ((parent - 1) / inodes_per_block) + inode_offset;
  off = (parent - 1) % inodes_per_block;

  if (fs_version == 1) {
	get_block(b, (char *) ino1);
	nr_dzones = V1_NR_DZONES;
  } else {
	get_block(b, (char *) ino2);
	nr_dzones = V2_NR_DZONES;
  }
  for (k = 0; k < nr_dzones; k++) {
	if (fs_version == 1) {
		z = ino1[off].d1_zone[k];
		if (z == 0) {
			z = alloc_zone();
			ino1[off].d1_zone[k] = z;
		}
	} else {
		z = ino2[off].d2_zone[k];
		if (z == 0) {
			z = alloc_zone();
			ino2[off].d2_zone[k] = z;
		}
	}
	for (l = 0; l < zone_size; l++) {
		get_block((z << zone_shift) + l, (char *) dir_entry);
		for (i = 0; i < NR_DIR_ENTRIES(BLOCK_SIZE); i++) {
			if (dir_entry[i].d_ino == 0) {
				dir_entry[i].d_ino = child;
				p1 = name;
				p2 = dir_entry[i].d_name;
				j = 14;
				while (j--) {
					*p2++ = *p1;
					if (*p1 != 0) p1++;
				}
				put_block((z << zone_shift) + l, (char *) dir_entry);
				if (fs_version == 1) {
					put_block(b, (char *) ino1);
				} else {
					put_block(b, (char *) ino2);
				}
				return;
			}
		}
	}
  }

  printf("Directory-inode %d beyond direct blocks.  Could not enter %s\n",
         parent, name);
  pexit("Halt");
}


void add_zone(n, z, bytes, cur_time)
ino_t n;
zone_t z;
long bytes, cur_time;
{
  if (fs_version == 1) {
	add_z_1(n, z, bytes, cur_time);
  } else {
	add_z_2(n, z, bytes, cur_time);
  }
}

void add_z_1(n, z, bytes, cur_time)
ino_t n;
zone_t z;
long bytes, cur_time;
{
  /* Add zone z to inode n. The file has grown by 'bytes' bytes. */

  int off, i;
  block_t b;
  zone_t indir;
  zone1_t blk[V1_INDIRECTS];
  d1_inode *p;
  d1_inode inode[V1_INODES_PER_BLOCK];

  b = ((n - 1) / V1_INODES_PER_BLOCK) + inode_offset;
  off = (n - 1) % V1_INODES_PER_BLOCK;
  get_block(b, (char *) inode);
  p = &inode[off];
  p->d1_size += bytes;
  p->d1_mtime = cur_time;
  for (i = 0; i < V1_NR_DZONES; i++)
	if (p->d1_zone[i] == 0) {
		p->d1_zone[i] = (zone1_t) z;
		put_block(b, (char *) inode);
		return;
	}
  put_block(b, (char *) inode);

  /* File has grown beyond a small file. */
  if (p->d1_zone[V1_NR_DZONES] == 0)
	p->d1_zone[V1_NR_DZONES] = (zone1_t) alloc_zone();
  indir = p->d1_zone[V1_NR_DZONES];
  put_block(b, (char *) inode);
  b = indir << zone_shift;
  get_block(b, (char *) blk);
  for (i = 0; i < V1_INDIRECTS; i++)
	if (blk[i] == 0) {
		blk[i] = (zone1_t) z;
		put_block(b, (char *) blk);
		return;
	}
  pexit("File has grown beyond single indirect");
}

void add_z_2(n, z, bytes, cur_time)
ino_t n;
zone_t z;
long bytes, cur_time;
{
  /* Add zone z to inode n. The file has grown by 'bytes' bytes. */

  int off, i;
  block_t b;
  zone_t indir;
  zone_t blk[V2_INDIRECTS(BLOCK_SIZE)];
  d2_inode *p;
  d2_inode inode[V2_INODES_PER_BLOCK(BLOCK_SIZE)];

  b = ((n - 1) / V2_INODES_PER_BLOCK(BLOCK_SIZE)) + inode_offset;
  off = (n - 1) % V2_INODES_PER_BLOCK(BLOCK_SIZE);
  get_block(b, (char *) inode);
  p = &inode[off];
  p->d2_size += bytes;
  p->d2_mtime = cur_time;
  for (i = 0; i < V2_NR_DZONES; i++)
	if (p->d2_zone[i] == 0) {
		p->d2_zone[i] = z;
		put_block(b, (char *) inode);
		return;
	}
  put_block(b, (char *) inode);

  /* File has grown beyond a small file. */
  if (p->d2_zone[V2_NR_DZONES] == 0) p->d2_zone[V2_NR_DZONES] = alloc_zone();
  indir = p->d2_zone[V2_NR_DZONES];
  put_block(b, (char *) inode);
  b = indir << zone_shift;
  get_block(b, (char *) blk);
  for (i = 0; i < V2_INDIRECTS(BLOCK_SIZE); i++)
	if (blk[i] == 0) {
		blk[i] = z;
		put_block(b, (char *) blk);
		return;
	}
  pexit("File has grown beyond single indirect");
}


void incr_link(n)
ino_t n;
{
  /* Increment the link count to inode n */
  int off;
  block_t b;

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;
  if (fs_version == 1) {
	d1_inode inode1[V1_INODES_PER_BLOCK];

	get_block(b, (char *) inode1);
	inode1[off].d1_nlinks++;
	put_block(b, (char *) inode1);
  } else {
	d2_inode inode2[V2_INODES_PER_BLOCK(BLOCK_SIZE)];

	get_block(b, (char *) inode2);
	inode2[off].d2_nlinks++;
	put_block(b, (char *) inode2);
  }
}


void incr_size(n, count)
ino_t n;
long count;
{
  /* Increment the file-size in inode n */
  block_t b;
  int off;

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;
  if (fs_version == 1) {
	d1_inode inode1[V1_INODES_PER_BLOCK];

	get_block(b, (char *) inode1);
	inode1[off].d1_size += count;
	put_block(b, (char *) inode1);
  } else {
	d2_inode inode2[V2_INODES_PER_BLOCK(BLOCK_SIZE)];

	get_block(b, (char *) inode2);
	inode2[off].d2_size += count;
	put_block(b, (char *) inode2);
  }
}


/*================================================================
 * 	 	     allocation assist group
 *===============================================================*/
PRIVATE ino_t alloc_inode(mode, usrid, grpid)
int mode, usrid, grpid;
{
  ino_t num;
  int off;
  block_t b;

  num = next_inode++;
  if (num > nrinodes) pexit("File system does not have enough inodes");
  b = ((num - 1) / inodes_per_block) + inode_offset;
  off = (num - 1) % inodes_per_block;
  if (fs_version == 1) {
	d1_inode inode1[V1_INODES_PER_BLOCK];

	get_block(b, (char *) inode1);
	inode1[off].d1_mode = mode;
	inode1[off].d1_uid = usrid;
	inode1[off].d1_gid = grpid;
	put_block(b, (char *) inode1);
  } else {
	d2_inode inode2[V2_INODES_PER_BLOCK(BLOCK_SIZE)];

	get_block(b, (char *) inode2);
	inode2[off].d2_mode = mode;
	inode2[off].d2_uid = usrid;
	inode2[off].d2_gid = grpid;
	put_block(b, (char *) inode2);
  }

  /* Set the bit in the bit map. */
  /* DEBUG FIXME.  This assumes the bit is in the first inode map block. */
  insert_bit((block_t) INODE_MAP, (int) num);
  return(num);
}


PRIVATE zone_t alloc_zone()
{
  /* Allocate a new zone */
  /* Works for zone > block */
  block_t b;
  int i;
  zone_t z;

  z = next_zone++;
  b = z << zone_shift;
  if ((b + zone_size) > nrblocks)
	pexit("File system not big enough for all the files");
  for (i = 0; i < zone_size; i++)
	put_block(b + i, zero);	/* give an empty zone */
  /* DEBUG FIXME.  This assumes the bit is in the first zone map block. */
  insert_bit(zone_map, (int) (z - zoff));	/* lint, NOT OK because
						 * z hasn't been broken
						 * up into block +
						 * offset yet. */
  return(z);
}


void insert_bit(block, bit)
block_t block;
int bit;
{
  /* Insert 'count' bits in the bitmap */
  int w, s;
  short buf[BLOCK_SIZE / sizeof(short)];

  if (block < 0) pexit("insert_bit called with negative argument");
  get_block(block, (char *) buf);
  w = bit / (8 * sizeof(short));
  s = bit % (8 * sizeof(short));
  buf[w] |= (1 << s);
  put_block(block, (char *) buf);
}


/*================================================================
 * 		proto-file processing assist group
 *===============================================================*/
int mode_con(p)
char *p;
{
  /* Convert string to mode */
  int o1, o2, o3, mode;
  char c1, c2, c3;

  c1 = *p++;
  c2 = *p++;
  c3 = *p++;
  o1 = *p++ - '0';
  o2 = *p++ - '0';
  o3 = *p++ - '0';
  mode = (o1 << 6) | (o2 << 3) | o3;
  if (c1 == 'd') mode += I_DIRECTORY;
  if (c1 == 'b') mode += I_BLOCK_SPECIAL;
  if (c1 == 'c') mode += I_CHAR_SPECIAL;
  if (c1 == '-') mode += I_REGULAR;
  if (c2 == 'u') mode += I_SET_UID_BIT;
  if (c3 == 'g') mode += I_SET_GID_BIT;
  return(mode);
}

void getline(line, parse)
char *parse[MAX_TOKENS];
char line[LINE_LEN];
{
  /* Read a line and break it up in tokens */
  int k;
  char c, *p;
  int d;

  for (k = 0; k < MAX_TOKENS; k++) parse[k] = 0;
  for (k = 0; k < LINE_LEN; k++) line[k] = 0;
  k = 0;
  parse[0] = 0;
  p = line;
  while (1) {
	if (++k > LINE_LEN) pexit("Line too long");
	d = fgetc(proto);
	if (d == EOF) pexit("Unexpected end-of-file");
	*p = d;
	if (*p == '\n') lct++;
	if (*p == ' ' || *p == '\t') *p = 0;
	if (*p == '\n') {
		*p++ = 0;
		*p = '\n';
		break;
	}
	p++;
  }

  k = 0;
  p = line;
  lastp = line;
  while (1) {
	c = *p++;
	if (c == '\n') return;
	if (c == 0) continue;
	parse[k++] = p - 1;
	do {
		c = *p++;
	} while (c != 0 && c != '\n');
  }
}


/*================================================================
 *			other stuff
 *===============================================================*/
void check_mtab(devname)
char *devname;			/* /dev/hd1 or whatever */
{
/* Check to see if the special file named in s is mounted. */

  int n;
  char special[PATH_MAX + 1], mounted_on[PATH_MAX + 1], version[10], rw_flag[10];

  if (load_mtab("mkfs") < 0) return;
  while (1) {
	n = get_mtab_entry(special, mounted_on, version, rw_flag);
	if (n < 0) return;
	if (strcmp(devname, special) == 0) {
		/* Can't mkfs on top of a mounted file system. */
		fprintf(stderr, "%s: %s is mounted on %s\n",
			progname, devname, mounted_on);
		exit(1);
	}
  }
}


long file_time(f)
int f;
{
#ifdef UNIX
  struct stat statbuf;
  fstat(f, &statbuf);
  return(statbuf.st_mtime);
#else				/* fstat not supported by DOS */
  return(0L);
#endif
}


void pexit(s)
char *s;
{
  fprintf(stderr, "%s: %s\n", progname, s);
  if (lct != 0)
	fprintf(stderr, "Line %d being processed when error detected.\n", lct);
  flush();
  exit(2);
}


void copy(from, to, count)
char *from, *to;
int count;
{
  while (count--) *to++ = *from++;
}


void print_fs()
{
  int i, j;
  ino_t k;
  d1_inode inode1[V1_INODES_PER_BLOCK];
  d2_inode inode2[V2_INODES_PER_BLOCK(BLOCK_SIZE)];
  unsigned short usbuf[BLOCK_SIZE / sizeof(unsigned short)];
  block_t b, inode_limit;
  struct direct dir[NR_DIR_ENTRIES(BLOCK_SIZE)];

  get_block((block_t) 1, (char *) usbuf);
  printf("\nSuperblock: ");
  for (i = 0; i < 8; i++) printf("%06o ", usbuf[i]);
  get_block((block_t) 2, (char *) usbuf);
  printf("...\nInode map:  ");
  for (i = 0; i < 9; i++) printf("%06o ", usbuf[i]);
  get_block((block_t) 3, (char *) usbuf);
  printf("...\nZone  map:  ");
  for (i = 0; i < 9; i++) printf("%06o ", usbuf[i]);
  printf("...\n");

  k = 0;
  for (b = inode_offset; k < nrinodes; b++) {
	if (fs_version == 1) {
		get_block(b, (char *) inode1);
	} else {
		get_block(b, (char *) inode2);
	}
	for (i = 0; i < inodes_per_block; i++) {
		k = inodes_per_block * (int) (b - inode_offset) + i + 1;
		/* Lint but OK */
		if (k > nrinodes) break;
		if (fs_version == 1) {
			if (inode1[i].d1_mode != 0) {
				printf("Inode %2d:  mode=", k);
				printf("%06o", inode1[i].d1_mode);
				printf("  uid=%2d  gid=%2d  size=",
				inode1[i].d1_uid, inode1[i].d1_gid);
				printf("%6ld", inode1[i].d1_size);
				printf("  zone[0]=%d\n", inode1[i].d1_zone[0]);
			}
			if ((inode1[i].d1_mode & I_TYPE) == I_DIRECTORY) {
				/* This is a directory */
				get_block(inode1[i].d1_zone[0], (char *) dir);
				for (j = 0; j < NR_DIR_ENTRIES(BLOCK_SIZE); j++)
					if (dir[j].d_ino)
						printf("\tInode %2d: %s\n", dir[j].d_ino, dir[j].d_name);
			}
		} else {
			if (inode2[i].d2_mode != 0) {
				printf("Inode %2d:  mode=", k);
				printf("%06o", inode2[i].d2_mode);
				printf("  uid=%2d  gid=%2d  size=",
				inode2[i].d2_uid, inode2[i].d2_gid);
				printf("%6ld", inode2[i].d2_size);
				printf("  zone[0]=%ld\n", inode2[i].d2_zone[0]);
			}
			if ((inode2[i].d2_mode & I_TYPE) == I_DIRECTORY) {
				/* This is a directory */
				get_block(inode2[i].d2_zone[0], (char *) dir);
				for (j = 0; j < NR_DIR_ENTRIES(BLOCK_SIZE); j++)
					if (dir[j].d_ino)
						printf("\tInode %2d: %s\n", dir[j].d_ino, dir[j].d_name);
			}
		}
	}
  }

  printf("%d inodes used.     %d zones used.\n", next_inode - 1, next_zone);
}


int read_and_set(n)
block_t n;
{
/* The first time a block is read, it returns all 0s, unless there has
 * been a write.  This routine checks to see if a block has been accessed.
 */

  int w, s, mask, r;

  if (sizeof(char *) == 2 && n >= MAX_INIT) pexit("can't initialize past 128M");
  w = n / 8;
  s = n % 8;
  mask = 1 << s;
  r = (umap[w] & mask ? 1 : 0);
  umap[w] |= mask;
  return(r);
}

void usage()
{
  fprintf(stderr,
	  "Usage: %s [-1dlot] [-b blocks] [-i inodes] special [proto]\n",
	  progname);
  exit(1);
}

/*================================================================
 *		      get_block & put_block for MS-DOS
 *===============================================================*/
#ifdef DOS

/*
 *	These are the get_block and put_block routines
 *	when compiling & running mkfs.c under MS-DOS.
 *
 *	It requires the (asembler) routines absread & abswrite
 *	from the file diskio.asm. Since these routines just do
 *	as they are told (read & write the sector specified),
 *	a local cache is used to minimize the i/o-overhead for
 *	frequently used blocks.
 *
 *	The global variable "file" determines whether the output
 *	is to a disk-device or to a binary file.
 */


#define PH_SECTSIZE	   512	/* size of a physical disk-sector */


char *derrtab[14] = {
	     "no error",
	     "disk is read-only",
	     "unknown unit",
	     "device not ready",
	     "bad command",
	     "data error",
	     "internal error: bad request structure length",
	     "seek error",
	     "unknown media type",
	     "sector not found",
	     "printer out of paper (?)",
	     "write fault",
	     "read error",
	     "general error"
};

#define	CACHE_SIZE	20	/* 20 block-buffers */


struct cache {
  char blockbuf[BLOCK_SIZE];
  block_t blocknum;
  int dirty;
  int usecnt;
} cache[CACHE_SIZE];


void special(string)
char *string;
{

  if (string[1] == ':' && string[2] == 0) {
	/* Format: d: or d:fname */
	disk = (string[0] & ~32) - 'A';
	if (disk > 1 && !override)	/* safety precaution */
		pexit("Bad drive specifier for special");
  } else {
	file = 1;
	if ((fd = creat(string, BWRITE)) == 0)
		pexit("Can't open special file");
  }
}

void get_block(n, buf)
block_t n;
char buf[BLOCK_SIZE];
{
  /* Get a block to the user */
  struct cache *bp, *fp;

  /* First access returns a zero block */
  if (read_and_set(n) == 0) {
	copy(zero, buf, BLOCK_SIZE);
	return;
  }

  /* Look for block in cache */
  fp = 0;
  for (bp = cache; bp < &cache[CACHE_SIZE]; bp++) {
	if (bp->blocknum == n) {
		copy(bp, buf, BLOCK_SIZE);
		bp->usecnt++;
		return;
	}

	/* Remember clean block */
	if (bp->dirty == 0)
		if (fp) {
			if (fp->usecnt > bp->usecnt) fp = bp;
		} else
			fp = bp;
  }

  /* Block not in cache, get it */
  if (!fp) {
	/* No clean buf, flush one */
	for (bp = cache, fp = cache; bp < &cache[CACHE_SIZE]; bp++)
		if (fp->usecnt > bp->usecnt) fp = bp;
	mx_write(fp->blocknum, fp);
  }
  mx_read(n, fp);
  fp->dirty = 0;
  fp->usecnt = 0;
  fp->blocknum = n;
  copy(fp, buf, BLOCK_SIZE);
}

void put_block(n, buf)
block_t n;
char buf[BLOCK_SIZE];
{
  /* Accept block from user */
  struct cache *fp, *bp;

  (void) read_and_set(n);

  /* Look for block in cache */
  fp = 0;
  for (bp = cache; bp < &cache[CACHE_SIZE]; bp++) {
	if (bp->blocknum == n) {
		copy(buf, bp, BLOCK_SIZE);
		bp->dirty = 1;
		return;
	}

	/* Remember clean block */
	if (bp->dirty == 0)
		if (fp) {
			if (fp->usecnt > bp->usecnt) fp = bp;
		} else
			fp = bp;
  }

  /* Block not in cache */
  if (!fp) {
	/* No clean buf, flush one */
	for (bp = cache, fp = cache; bp < &cache[CACHE_SIZE]; bp++)
		if (fp->usecnt > bp->usecnt) fp = bp;
	mx_write(fp->blocknum, fp);
  }
  fp->dirty = 1;
  fp->usecnt = 1;
  fp->blocknum = n;
  copy(buf, fp, BLOCK_SIZE);
}

void cache_init()
{
  struct cache *bp;
  for (bp = cache; bp < &cache[CACHE_SIZE]; bp++) bp->blocknum = -1;
}

void flush()
{
  /* Flush all dirty blocks to disk */
  struct cache *bp;

  for (bp = cache; bp < &cache[CACHE_SIZE]; bp++)
	if (bp->dirty) {
		mx_write(bp->blocknum, bp);
		bp->dirty = 0;
	}
}

/*==================================================================
 *			hard read & write etc.
 *=================================================================*/
#define MAX_RETRIES	5


void mx_read(blocknr, buf)
int blocknr;
char buf[BLOCK_SIZE];
{

  /* Read the requested MINIX-block in core */
  char (*bp)[PH_SECTSIZE];
  int sectnum, retries, err;

  if (file) {
	lseek(fd, (off_t) blocknr * BLOCK_SIZE, 0);
	if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
		pexit("mx_read: error reading file");
  } else {
	sectnum = blocknr * (BLOCK_SIZE / PH_SECTSIZE);
	for (bp = buf; bp < &buf[BLOCK_SIZE]; bp++) {
		retries = MAX_RETRIES;
		do
			err = absread(disk, sectnum, bp);
		while (err && --retries);

		if (retries) {
			sectnum++;
		} else {
			dexit("mx_read", sectnum, err);
		}
	}
  }
}

void mx_write(blocknr, buf)
int blocknr;
char buf[BLOCK_SIZE];
{
  /* Write the MINIX-block to disk */
  char (*bp)[PH_SECTSIZE];
  int retries, sectnum, err;

  if (file) {
	lseek(fd, blocknr * BLOCK_SIZE, 0);
	if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
		pexit("mx_write: error writing file");
	}
  } else {
	sectnum = blocknr * (BLOCK_SIZE / PH_SECTSIZE);
	for (bp = buf; bp < &buf[BLOCK_SIZE]; bp++) {
		retries = MAX_RETRIES;
		do {
			err = abswrite(disk, sectnum, bp);
		} while (err && --retries);

		if (retries) {
			sectnum++;
		} else {
			dexit("mx_write", sectnum, err);
		}
	}
  }
}


void dexit(s, sectnum, err)
int sectnum, err;
char *s;
{
  printf("Error: %s, sector: %d, code: %d, meaning: %s\n",
         s, sectnum, err, derrtab[err]);
  exit(2);
}

#endif

/*================================================================
 *		      get_block & put_block for UNIX
 *===============================================================*/
#ifdef UNIX

void special(string)
char *string;
{
  fd = creat(string, 0777);
  close(fd);
  fd = open(string, O_RDWR);
  if (fd < 0) pexit("Can't open special file");
#if (MACHINE == ATARI)
  {
	struct stat statbuf;

	if (fstat(fd, &statbuf) < 0) return;
	isdev = (statbuf.st_mode & S_IFMT) == S_IFCHR
		||
		(statbuf.st_mode & S_IFMT) == S_IFBLK
		;
  }
#endif
}



void get_block(n, buf)
block_t n;
char buf[BLOCK_SIZE];
{
/* Read a block. */

  int k;

  /* First access returns a zero block */
  if (read_and_set(n) == 0) {
	copy(zero, buf, BLOCK_SIZE);
	return;
  }
  lseek(fd, (off_t) n * BLOCK_SIZE, SEEK_SET);
  k = read(fd, buf, BLOCK_SIZE);
  if (k != BLOCK_SIZE) {
	pexit("get_block couldn't read");
  }
}

void put_block(n, buf)
block_t n;
char buf[BLOCK_SIZE];
{
/* Write a block. */

  (void) read_and_set(n);

  /* XXX - check other lseeks too. */
  if (lseek(fd, (off_t) n * BLOCK_SIZE, SEEK_SET) == (off_t) -1) {
	pexit("put_block couldn't seek");
  }
  if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
	pexit("put_block couldn't write");
  }
}


/* Dummy routines to keep source file clean from #ifdefs */

void flush()
{
  return;
}

void cache_init()
{
  return;
}

#endif
