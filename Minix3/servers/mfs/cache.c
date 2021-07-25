/* The file system maintains a buffer cache to reduce the number of disk
 * accesses needed.  Whenever a read or write to the disk is done, a check is
 * first made to see if the block is in the cache.  This file manages the
 * cache.
 *
 * The entry points into this file are:
 *   get_block:	  request to fetch a block for reading or writing from cache
 *   put_block:	  return a block previously requested with get_block
 *   alloc_zone:  allocate a new zone (to increase the length of a file)
 *   free_zone:	  release a zone (when a file is removed)
 *   invalidate:  remove all the cache blocks on some device
 *
 * Private functions:
 *   rw_block:    read or write a block from the disk itself
 */

#include "fs.h"
#include <minix/com.h>
#include <minix/u64.h>
#include <string.h>
#include "buf.h"
#include "super.h"
#include "inode.h"

FORWARD _PROTOTYPE( void rm_lru, (struct buf *bp) );
FORWARD _PROTOTYPE( int rw_block, (struct buf *, int) );

/*===========================================================================*
 *				get_block				     *
 *===========================================================================*/
PUBLIC struct buf *get_block(dev, block, only_search)
register dev_t dev;		/* on which device is the block? */
register block_t block;		/* which block is wanted? */
int only_search;		/* if NO_READ, don't read, else act normal */
{
/* Check to see if the requested block is in the block cache.  If so, return
 * a pointer to it.  If not, evict some other block and fetch it (unless
 * 'only_search' is 1).  All the blocks in the cache that are not in use
 * are linked together in a chain, with 'front' pointing to the least recently
 * used block and 'rear' to the most recently used block.  If 'only_search' is
 * 1, the block being requested will be overwritten in its entirety, so it is
 * only necessary to see if it is in the cache; if it is not, any free buffer
 * will do.  It is not necessary to actually read the block in from disk.
 * If 'only_search' is PREFETCH, the block need not be read from the disk,
 * and the device is not to be marked on the block, so callers can tell if
 * the block returned is valid.
 * In addition to the LRU chain, there is also a hash chain to link together
 * blocks whose block numbers end with the same bit strings, for fast lookup.
 */

  int b;
  static struct buf *bp, *prev_ptr;

  ASSERT(fs_block_size > 0);

  /* Search the hash chain for (dev, block). Do_read() can use 
   * get_block(NO_DEV ...) to get an unnamed block to fill with zeros when
   * someone wants to read from a hole in a file, in which case this search
   * is skipped
   */
  if (dev != NO_DEV) {
	b = BUFHASH(block);
	bp = buf_hash[b];
	while (bp != NIL_BUF) {
		if (bp->b_blocknr == block && bp->b_dev == dev) {
			/* Block needed has been found. */
			if (bp->b_count == 0) rm_lru(bp);
			bp->b_count++;	/* record that block is in use */
			ASSERT(bp->b_bytes == fs_block_size);
			ASSERT(bp->b_dev == dev);
			ASSERT(bp->b_dev != NO_DEV);
			ASSERT(bp->bp);
			return(bp);
		} else {
			/* This block is not the one sought. */
			bp = bp->b_hash; /* move to next block on hash chain */
		}
	}
  }

  /* Desired block is not on available chain.  Take oldest block ('front'). */
  if ((bp = front) == NIL_BUF) panic(__FILE__,"all buffers in use", NR_BUFS);

  if(bp->b_bytes < fs_block_size) {
	phys_bytes ph;
	ASSERT(!bp->bp);
	ASSERT(bp->b_bytes == 0);
	if(!(bp->bp = alloc_contig(fs_block_size, 0, &ph))) {
		printf("MFS: couldn't allocate a new block.\n");
		for(bp = front;
			bp && bp->b_bytes < fs_block_size; bp = bp->b_next)
			;
		if(!bp) {
			panic("MFS", "no buffer available", NO_NUM);
		}
	} else {
  		bp->b_bytes = fs_block_size;
	}
  }

  ASSERT(bp);
  ASSERT(bp->bp);
  ASSERT(bp->b_bytes == fs_block_size);
  ASSERT(bp->b_count == 0);

  rm_lru(bp);

  /* Remove the block that was just taken from its hash chain. */
  b = BUFHASH(bp->b_blocknr);
  prev_ptr = buf_hash[b];
  if (prev_ptr == bp) {
	buf_hash[b] = bp->b_hash;
  } else {
	/* The block just taken is not on the front of its hash chain. */
	while (prev_ptr->b_hash != NIL_BUF)
		if (prev_ptr->b_hash == bp) {
			prev_ptr->b_hash = bp->b_hash;	/* found it */
			break;
		} else {
			prev_ptr = prev_ptr->b_hash;	/* keep looking */
		}
  }

  /* If the block taken is dirty, make it clean by writing it to the disk.
   * Avoid hysteresis by flushing all other dirty blocks for the same device.
   */
  if (bp->b_dev != NO_DEV) {
	if (bp->b_dirt == DIRTY) flushall(bp->b_dev);
  }

  /* Fill in block's parameters and add it to the hash chain where it goes. */
  bp->b_dev = dev;		/* fill in device number */
  bp->b_blocknr = block;	/* fill in block number */
  bp->b_count++;		/* record that block is being used */
  b = BUFHASH(bp->b_blocknr);
  bp->b_hash = buf_hash[b];

  buf_hash[b] = bp;		/* add to hash list */

  /* Go get the requested block unless searching or prefetching. */
  if (dev != NO_DEV) {
	if (only_search == PREFETCH) bp->b_dev = NO_DEV;
	else
	if (only_search == NORMAL) {
		rw_block(bp, READING);
	}
  }

  ASSERT(bp->bp);

  return(bp);			/* return the newly acquired block */
}

/*===========================================================================*
 *				put_block				     *
 *===========================================================================*/
PUBLIC void put_block(bp, block_type)
register struct buf *bp;	/* pointer to the buffer to be released */
int block_type;			/* INODE_BLOCK, DIRECTORY_BLOCK, or whatever */
{
/* Return a block to the list of available blocks.   Depending on 'block_type'
 * it may be put on the front or rear of the LRU chain.  Blocks that are
 * expected to be needed again shortly (e.g., partially full data blocks)
 * go on the rear; blocks that are unlikely to be needed again shortly
 * (e.g., full data blocks) go on the front.  Blocks whose loss can hurt
 * the integrity of the file system (e.g., inode blocks) are written to
 * disk immediately if they are dirty.
 */
  if (bp == NIL_BUF) return;	/* it is easier to check here than in caller */

  bp->b_count--;		/* there is one use fewer now */
  if (bp->b_count != 0) return;	/* block is still in use */

  bufs_in_use--;		/* one fewer block buffers in use */

  /* Put this block back on the LRU chain.  If the ONE_SHOT bit is set in
   * 'block_type', the block is not likely to be needed again shortly, so put
   * it on the front of the LRU chain where it will be the first one to be
   * taken when a free buffer is needed later.
   */
  if (bp->b_dev == DEV_RAM || (block_type & ONE_SHOT)) {
	/* Block probably won't be needed quickly. Put it on front of chain.
  	 * It will be the next block to be evicted from the cache.
  	 */
	bp->b_prev = NIL_BUF;
	bp->b_next = front;
	if (front == NIL_BUF)
		rear = bp;	/* LRU chain was empty */
	else
		front->b_prev = bp;
	front = bp;
  } 
  else {
	/* Block probably will be needed quickly.  Put it on rear of chain.
  	 * It will not be evicted from the cache for a long time.
  	 */
	bp->b_prev = rear;
	bp->b_next = NIL_BUF;
	if (rear == NIL_BUF)
		front = bp;
	else
		rear->b_next = bp;
	rear = bp;
  }

  /* Some blocks are so important (e.g., inodes, indirect blocks) that they
   * should be written to the disk immediately to avoid messing up the file
   * system in the event of a crash.
   */
  if ((block_type & WRITE_IMMED) && bp->b_dirt==DIRTY && bp->b_dev != NO_DEV) {
		rw_block(bp, WRITING);
  } 
}

/*===========================================================================*
 *				alloc_zone				     *
 *===========================================================================*/
PUBLIC zone_t alloc_zone(dev, z)
dev_t dev;			/* device where zone wanted */
zone_t z;			/* try to allocate new zone near this one */
{
/* Allocate a new zone on the indicated device and return its number. */

  int major, minor;
  bit_t b, bit;
  struct super_block *sp;

  /* Note that the routine alloc_bit() returns 1 for the lowest possible
   * zone, which corresponds to sp->s_firstdatazone.  To convert a value
   * between the bit number, 'b', used by alloc_bit() and the zone number, 'z',
   * stored in the inode, use the formula:
   *     z = b + sp->s_firstdatazone - 1
   * Alloc_bit() never returns 0, since this is used for NO_BIT (failure).
   */
  sp = get_super(dev);

  /* If z is 0, skip initial part of the map known to be fully in use. */
  if (z == sp->s_firstdatazone) {
	bit = sp->s_zsearch;
  } else {
	bit = (bit_t) z - (sp->s_firstdatazone - 1);
  }
  b = alloc_bit(sp, ZMAP, bit);
  if (b == NO_BIT) {
	err_code = ENOSPC;
	major = (int) (sp->s_dev >> MAJOR) & BYTE;
	minor = (int) (sp->s_dev >> MINOR) & BYTE;
	printf("No space on device %d/%d\n", major, minor);
	return(NO_ZONE);
  }
  if (z == sp->s_firstdatazone) sp->s_zsearch = b;	/* for next time */
  return(sp->s_firstdatazone - 1 + (zone_t) b);
}

/*===========================================================================*
 *				free_zone				     *
 *===========================================================================*/
PUBLIC void free_zone(dev, numb)
dev_t dev;				/* device where zone located */
zone_t numb;				/* zone to be returned */
{
/* Return a zone. */

  register struct super_block *sp;
  bit_t bit;

  /* Locate the appropriate super_block and return bit. */
  sp = get_super(dev);
  if (numb < sp->s_firstdatazone || numb >= sp->s_zones) return;
  bit = (bit_t) (numb - (sp->s_firstdatazone - 1));
  free_bit(sp, ZMAP, bit);
  if (bit < sp->s_zsearch) sp->s_zsearch = bit;
}

/*===========================================================================*
 *				rw_block				     *
 *===========================================================================*/
PRIVATE int rw_block(bp, rw_flag)
register struct buf *bp;	/* buffer pointer */
int rw_flag;			/* READING or WRITING */
{
/* Read or write a disk block. This is the only routine in which actual disk
 * I/O is invoked. If an error occurs, a message is printed here, but the error
 * is not reported to the caller.  If the error occurred while purging a block
 * from the cache, it is not clear what the caller could do about it anyway.
 */
  int r, op;
  u64_t pos;
  dev_t dev;

  if ( (dev = bp->b_dev) != NO_DEV) {
	  pos = mul64u(bp->b_blocknr, fs_block_size);
	  op = (rw_flag == READING ? MFS_DEV_READ : MFS_DEV_WRITE);
	  r = block_dev_io(op, dev, SELF_E, bp->b_data, pos, fs_block_size, 0);
	  if (r != fs_block_size) {
		  if (r >= 0) r = END_OF_FILE;
		  if (r != END_OF_FILE)
			printf("MFS(%d) I/O error on device %d/%d, block %ld\n",
			SELF_E, (dev>>MAJOR)&BYTE, (dev>>MINOR)&BYTE, 
			bp->b_blocknr);
		  
		  bp->b_dev = NO_DEV;	/* invalidate block */

		  /* Report read errors to interested parties. */
		  if (rw_flag == READING) rdwt_err = r;
	  }
	  if(op == MFS_DEV_READ) {
		int i;
#if 0
		printf("mfsread after contents: 0x%lx, ", bp->b_data);
		for(i = 0; i < 10; i++) printf("%02lx ", (unsigned char) bp->b_data[i]);
		printf("\n");
#endif
	  }
  }

  bp->b_dirt = CLEAN;

  return OK;
}

/*===========================================================================*
 *				invalidate				     *
 *===========================================================================*/
PUBLIC void invalidate(device)
dev_t device;			/* device whose blocks are to be purged */
{
/* Remove all the blocks belonging to some device from the cache. */

  register struct buf *bp;

  for (bp = &buf[0]; bp < &buf[NR_BUFS]; bp++)
	if (bp->b_dev == device) bp->b_dev = NO_DEV;
}

/*===========================================================================*
 *				flushall				     *
 *===========================================================================*/
PUBLIC void flushall(dev)
dev_t dev;			/* device to flush */
{
/* Flush all dirty blocks for one device. */

  register struct buf *bp;
  static struct buf **dirty;	/* static so it isn't on stack */
  int ndirty;

  STATICINIT(dirty, NR_BUFS);

  for (bp = &buf[0], ndirty = 0; bp < &buf[NR_BUFS]; bp++)
	if (bp->b_dirt == DIRTY && bp->b_dev == dev) dirty[ndirty++] = bp;
  rw_scattered(dev, dirty, ndirty, WRITING);
}

/*===========================================================================*
 *				rw_scattered				     *
 *===========================================================================*/
PUBLIC void rw_scattered(dev, bufq, bufqsize, rw_flag)
dev_t dev;			/* major-minor device number */
struct buf **bufq;		/* pointer to array of buffers */
int bufqsize;			/* number of buffers */
int rw_flag;			/* READING or WRITING */
{
/* Read or write scattered data from a device. */

  register struct buf *bp;
  int gap;
  register int i;
  register iovec_t *iop;
  static iovec_t *iovec = NULL;
  int j, r;

  STATICINIT(iovec, NR_IOREQS);

  /* (Shell) sort buffers on b_blocknr. */
  gap = 1;
  do
	gap = 3 * gap + 1;
  while (gap <= bufqsize);
  while (gap != 1) {
	gap /= 3;
	for (j = gap; j < bufqsize; j++) {
		for (i = j - gap;
		     i >= 0 && bufq[i]->b_blocknr > bufq[i + gap]->b_blocknr;
		     i -= gap) {
			bp = bufq[i];
			bufq[i] = bufq[i + gap];
			bufq[i + gap] = bp;
		}
	}
  }

  /* Set up I/O vector and do I/O.  The result of dev_io is OK if everything
   * went fine, otherwise the error code for the first failed transfer.
   */  
  while (bufqsize > 0) {
	for (j = 0, iop = iovec; j < NR_IOREQS && j < bufqsize; j++, iop++) {
		bp = bufq[j];
		if (bp->b_blocknr != bufq[0]->b_blocknr + j) break;
		iop->iov_addr = (vir_bytes) bp->b_data;
		iop->iov_size = fs_block_size;
	}
	r = block_dev_io(rw_flag == WRITING ? MFS_DEV_SCATTER : MFS_DEV_GATHER,
		dev, SELF_E, iovec,
		mul64u(bufq[0]->b_blocknr, fs_block_size), j, 0);

	/* Harvest the results.  Dev_io reports the first error it may have
	 * encountered, but we only care if it's the first block that failed.
	 */
	for (i = 0, iop = iovec; i < j; i++, iop++) {
		bp = bufq[i];
		if (iop->iov_size != 0) {
			/* Transfer failed. An error? Do we care? */
			if (r != OK && i == 0) {
				printf(
				"fs: I/O error on device %d/%d, block %lu\n",
					(dev>>MAJOR)&BYTE, (dev>>MINOR)&BYTE,
					bp->b_blocknr);
				bp->b_dev = NO_DEV;	/* invalidate block */
			}
			break;
		}
		if (rw_flag == READING) {
			bp->b_dev = dev;	/* validate block */
			put_block(bp, PARTIAL_DATA_BLOCK);
		} else {
			bp->b_dirt = CLEAN;
		}
	}
	bufq += i;
	bufqsize -= i;
	if (rw_flag == READING) {
		/* Don't bother reading more than the device is willing to
		 * give at this time.  Don't forget to release those extras.
		 */
		while (bufqsize > 0) {
			put_block(*bufq++, PARTIAL_DATA_BLOCK);
			bufqsize--;
		}
	}
	if (rw_flag == WRITING && i == 0) {
		/* We're not making progress, this means we might keep
		 * looping. Buffers remain dirty if un-written. Buffers are
		 * lost if invalidate()d or LRU-removed while dirty. This
		 * is better than keeping unwritable blocks around forever..
		 */
		break;
	}
  }
}

/*===========================================================================*
 *				rm_lru					     *
 *===========================================================================*/
PRIVATE void rm_lru(bp)
struct buf *bp;
{
/* Remove a block from its LRU chain. */
  struct buf *next_ptr, *prev_ptr;

  bufs_in_use++;
  next_ptr = bp->b_next;	/* successor on LRU chain */
  prev_ptr = bp->b_prev;	/* predecessor on LRU chain */
  if (prev_ptr != NIL_BUF)
	prev_ptr->b_next = next_ptr;
  else
	front = next_ptr;	/* this block was at front of chain */

  if (next_ptr != NIL_BUF)
	next_ptr->b_prev = prev_ptr;
  else
	rear = prev_ptr;	/* this block was at rear of chain */
}

/*===========================================================================*
 *				set_blocksize				     *
 *===========================================================================*/
PUBLIC void set_blocksize(int blocksize)
{
	struct buf *bp;
	struct inode *rip;

	ASSERT(blocksize > 0);

        for (bp = &buf[0]; bp < &buf[NR_BUFS]; bp++)
		if(bp->b_count != 0)
			panic("MFS", "change blocksize with buffer in use",
				NO_NUM);

	for (rip = &inode[0]; rip < &inode[NR_INODES]; rip++)
		if (rip->i_count > 0)
			panic("MFS", "change blocksize with inode in use",
				NO_NUM);

	fs_sync();

	buf_pool();
	fs_block_size = blocksize;
}

/*===========================================================================*
 *                              buf_pool                                     *
 *===========================================================================*/
PUBLIC void buf_pool(void)
{
/* Initialize the buffer pool. */
  register struct buf *bp;

  bufs_in_use = 0;
  front = &buf[0];
  rear = &buf[NR_BUFS - 1];

  for (bp = &buf[0]; bp < &buf[NR_BUFS]; bp++) {
        bp->b_blocknr = NO_BLOCK;
        bp->b_dev = NO_DEV;
        bp->b_next = bp + 1;
        bp->b_prev = bp - 1;
        bp->bp = NULL;
        bp->b_bytes = 0;
  }
  buf[0].b_prev = NIL_BUF;
  buf[NR_BUFS - 1].b_next = NIL_BUF;

  for (bp = &buf[0]; bp < &buf[NR_BUFS]; bp++) bp->b_hash = bp->b_next;
  buf_hash[0] = front;

}

