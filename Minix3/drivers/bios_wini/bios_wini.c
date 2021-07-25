/* This file contains the "device dependent" part of a hard disk driver that
 * uses the ROM BIOS.  It makes a call and just waits for the transfer to
 * happen.  It is not interrupt driven and thus will (*) have poor performance.
 * The advantage is that it should work on virtually any PC, XT, 386, PS/2
 * or clone.  The demo disk uses this driver.  It is suggested that all
 * MINIX users try the other drivers, and use this one only as a last resort,
 * if all else fails.
 *
 * (*) The performance is within 10% of the AT driver for reads on any disk
 *     and writes on a 2:1 interleaved disk, it will be DMA_BUF_SIZE bytes
 *     per revolution for a minimum of 60 kb/s for writes to 1:1 disks.
 *
 * The file contains one entry point:
 *
 *	 bios_winchester_task:	main entry when system is brought up
 *
 *
 * Changes:
 *	30 Apr 1992 by Kees J. Bot: device dependent/independent split.
 *	14 May 2000 by Kees J. Bot: d-d/i rewrite.
 */

#include "../drivers.h"
#include "../libdriver/driver.h"
#include "../libdriver/drvlib.h"
#include <minix/sysutil.h>
#include <minix/safecopies.h>
#include <minix/keymap.h>
#include <sys/ioc_disk.h>
#include <ibm/int86.h>
#include <assert.h>

#define ME "BIOS_WINI"

/* Error codes */
#define ERR		 (-1)	/* general error */

/* Parameters for the disk drive. */
#define MAX_DRIVES         8	/* this driver supports 8 drives (d0 - d7)*/
#define MAX_SECS	 255	/* bios can transfer this many sectors */
#define NR_MINORS      (MAX_DRIVES * DEV_PER_DRIVE)
#define SUB_PER_DRIVE	(NR_PARTITIONS * NR_PARTITIONS)
#define NR_SUBDEVS	(MAX_DRIVES * SUB_PER_DRIVE)

PRIVATE int pc_at = 1;	/* What about PC XTs? */

/* Variables. */
PRIVATE struct wini {		/* main drive struct, one entry per drive */
  unsigned cylinders;		/* number of cylinders */
  unsigned heads;		/* number of heads */
  unsigned sectors;		/* number of sectors per track */
  unsigned open_ct;		/* in-use count */
  int drive_id;			/* Drive ID at BIOS level */
  int present;			/* Valid drive */
  int int13ext;			/* IBM/MS INT 13 extensions supported? */
  struct device part[DEV_PER_DRIVE];	/* disks and partitions */
  struct device subpart[SUB_PER_DRIVE]; /* subpartitions */
} wini[MAX_DRIVES], *w_wn;

PRIVATE int w_drive;			/* selected drive */
PRIVATE struct device *w_dv;		/* device's base and size */
PRIVATE char *bios_buf_v;
PRIVATE phys_bytes bios_buf_phys;
PRIVATE int remap_first = 0;		/* Remap drives for CD HD emulation */
#define BIOSBUF 16384
PRIVATE cp_grant_id_t my_bios_grant_id;

_PROTOTYPE(int main, (void) );
FORWARD _PROTOTYPE( struct device *w_prepare, (int device) );
FORWARD _PROTOTYPE( char *w_name, (void) );
FORWARD _PROTOTYPE( int w_transfer, (int proc_nr, int opcode, u64_t position,
				iovec_t *iov, unsigned nr_req, int safe) );
FORWARD _PROTOTYPE( int w_do_open, (struct driver *dp, message *m_ptr) );
FORWARD _PROTOTYPE( int w_do_close, (struct driver *dp, message *m_ptr) );
FORWARD _PROTOTYPE( void w_init, (void) );
FORWARD _PROTOTYPE( void w_geometry, (struct partition *entry));
FORWARD _PROTOTYPE( int w_other, (struct driver *dp, message *m_ptr, int)    );

/* Entry points to this driver. */
PRIVATE struct driver w_dtab = {
  w_name,	/* current device's name */
  w_do_open,	/* open or mount request, initialize device */
  w_do_close,	/* release device */
  do_diocntl,	/* get or set a partition's geometry */
  w_prepare,	/* prepare for I/O on a given minor device */
  w_transfer,	/* do the I/O */
  nop_cleanup,	/* no cleanup needed */
  w_geometry,	/* tell the geometry of the disk */
  nop_signal,		/* no cleanup needed on shutdown */
  nop_alarm,		/* ignore leftover alarms */
  nop_cancel,		/* ignore CANCELs */
  nop_select,		/* ignore selects */
  w_other,		/* catch-all for unrecognized commands and ioctls */
  NULL			/* leftover hardware interrupts */
};

/*===========================================================================*
 *				bios_winchester_task			     *
 *===========================================================================*/
PUBLIC int main()
{
  long v;

  v= 0;
  env_parse("bios_remap_first", "d", 0, &v, 0, 1);
  remap_first= v;

/* Set special disk parameters then call the generic main loop. */
  driver_task(&w_dtab);
  return(OK);
}

/*===========================================================================*
 *				w_prepare				     *
 *===========================================================================*/
PRIVATE struct device *w_prepare(device)
int device;
{
/* Prepare for I/O on a device. */

  if (device < NR_MINORS) {			/* d0, d0p[0-3], d1, ... */
	w_drive = device / DEV_PER_DRIVE;	/* save drive number */
	w_wn = &wini[w_drive];
	w_dv = &w_wn->part[device % DEV_PER_DRIVE];
  } else
  if ((unsigned) (device -= MINOR_d0p0s0) < NR_SUBDEVS) {/*d[0-7]p[0-3]s[0-3]*/
	w_drive = device / SUB_PER_DRIVE;
	w_wn = &wini[w_drive];
	w_dv = &w_wn->subpart[device % SUB_PER_DRIVE];
  } else {
	return(NIL_DEV);
  }
  if (w_drive >= MAX_DRIVES || !w_wn->present)
  	return NIL_DEV;
  return(w_dv);
}

/*===========================================================================*
 *				w_name					     *
 *===========================================================================*/
PRIVATE char *w_name()
{
/* Return a name for the current device. */
  static char name[] = "bios-d0";

  name[6] = '0' + w_drive;
  return name;
}

/*===========================================================================*
 *				w_transfer				     *
 *===========================================================================*/
PRIVATE int w_transfer(proc_nr, opcode, pos64, iov, nr_req, safe)
int proc_nr;			/* process doing the request */
int opcode;			/* DEV_GATHER or DEV_SCATTER */
u64_t pos64;			/* offset on device to read or write */
iovec_t *iov;			/* pointer to read or write request vector */
unsigned nr_req;		/* length of request vector */
int safe;			/* use safecopies? */
{
  struct wini *wn = w_wn;
  iovec_t *iop, *iov_end = iov + nr_req;
  int r, errors;
  unsigned nbytes, count, chunk;
  unsigned long block;
  vir_bytes i13e_rw_off, rem_buf_size;
  size_t vir_offset = 0;
  unsigned secspcyl = wn->heads * wn->sectors;
  struct int13ext_rw {
	u8_t	len;
	u8_t	res1;
	u16_t	count;
	u16_t	addr[2];
	u32_t	block[2];
  } *i13e_rw;
  struct reg86u reg86;
  u32_t lopos;

  lopos= ex64lo(pos64);

  /* Check disk address. */
  if ((lopos & SECTOR_MASK) != 0) return(EINVAL);

  errors = 0;

  i13e_rw_off= BIOSBUF-sizeof(*i13e_rw);
  rem_buf_size= (i13e_rw_off & ~SECTOR_MASK);
  i13e_rw = (struct int13ext_rw *) (bios_buf_v + i13e_rw_off);
  assert(rem_buf_size != 0);

  while (nr_req > 0) {
	/* How many bytes to transfer? */
	nbytes = 0;
	for (iop = iov; iop < iov_end; iop++) {
		if (nbytes + iop->iov_size > rem_buf_size) {
			/* Don't do half a segment if you can avoid it. */
			if (nbytes == 0) nbytes = rem_buf_size;
			break;
		}
		nbytes += iop->iov_size;
	}
	if ((nbytes & SECTOR_MASK) != 0) return(EINVAL);

	/* Which block on disk and how close to EOF? */
	if (cmp64(pos64, w_dv->dv_size) >= 0) return(OK);	/* At EOF */
	if (cmp64(add64u(pos64, nbytes), w_dv->dv_size) > 0) {
		u64_t n;
		n = sub64(w_dv->dv_size, pos64);
		assert(ex64hi(n) == 0);
		nbytes = ex64lo(n);
	}
	block = div64u(add64(w_dv->dv_base, pos64), SECTOR_SIZE);

	/* Degrade to per-sector mode if there were errors. */
	if (errors > 0) nbytes = SECTOR_SIZE;

	if (opcode == DEV_SCATTER_S) {
		/* Copy from user space to the DMA buffer. */
		count = 0;
		for (iop = iov; count < nbytes; iop++) {
			chunk = iop->iov_size;
			if (count + chunk > nbytes) chunk = nbytes - count;
			assert(chunk <= rem_buf_size);

			if(safe) {
			   	r=sys_safecopyfrom(proc_nr,
					(cp_grant_id_t) iop->iov_addr,
		       			0, (vir_bytes) (bios_buf_v+count),
					chunk, D);
				if (r != OK)
					panic(ME, "copy failed", r);
			} else {
				if(proc_nr != SELF) {
					panic(ME, "unsafe outside self", r);
				}
				memcpy(bios_buf_v+count,
					(char *) iop->iov_addr, chunk);
			}
			count += chunk;
		}
	}

	/* Do the transfer */
	if (wn->int13ext) {
		i13e_rw->len = 0x10;
		i13e_rw->res1 = 0;
		i13e_rw->count = nbytes >> SECTOR_SHIFT;
		i13e_rw->addr[0] = bios_buf_phys % HCLICK_SIZE;
		i13e_rw->addr[1] = bios_buf_phys / HCLICK_SIZE;
		i13e_rw->block[0] = block;
		i13e_rw->block[1] = 0;

		/* Set up an extended read or write BIOS call. */
		reg86.u.b.intno = 0x13;
		reg86.u.w.ax = opcode == DEV_SCATTER_S ? 0x4300 : 0x4200;
		reg86.u.b.dl = wn->drive_id;
		reg86.u.w.si = (bios_buf_phys + i13e_rw_off) % HCLICK_SIZE;
		reg86.u.w.ds = (bios_buf_phys + i13e_rw_off) / HCLICK_SIZE;
	} else {
		/* Set up an ordinary read or write BIOS call. */
		unsigned cylinder = block / secspcyl;
		unsigned sector = (block % wn->sectors) + 1;
		unsigned head = (block % secspcyl) / wn->sectors;

		reg86.u.b.intno = 0x13;
		reg86.u.b.ah = opcode == DEV_SCATTER_S ? 0x03 : 0x02;
		reg86.u.b.al = nbytes >> SECTOR_SHIFT;
		reg86.u.w.bx = bios_buf_phys % HCLICK_SIZE;
		reg86.u.w.es = bios_buf_phys / HCLICK_SIZE;
		reg86.u.b.ch = cylinder & 0xFF;
		reg86.u.b.cl = sector | ((cylinder & 0x300) >> 2);
		reg86.u.b.dh = head;
		reg86.u.b.dl = wn->drive_id;
	}

	r= sys_int86(&reg86);
	if (r != OK)
		panic(ME, "BIOS call failed", r);

	if (reg86.u.w.f & 0x0001) {
		/* An error occurred, try again sector by sector unless */
		if (++errors == 2) return(EIO);
		continue;
	}

	if (opcode == DEV_GATHER_S) {
		/* Copy from the DMA buffer to user space. */
		count = 0;
		for (iop = iov; count < nbytes; iop++) {
			chunk = iop->iov_size;
			if (count + chunk > nbytes) chunk = nbytes - count;
			assert(chunk <= rem_buf_size);

			if(safe) {
			   	r=sys_safecopyto(proc_nr, iop->iov_addr, 
				       	0, (vir_bytes) (bios_buf_v+count), chunk, D);

				if (r != OK)
					panic(ME, "sys_vircopy failed", r);
			} else {
				if (proc_nr != SELF)
					panic(ME, "unsafe without self", NO_NUM);
				memcpy((char *) iop->iov_addr,
					bios_buf_v+count, chunk);
			}
			count += chunk;
		}
	}

	/* Book the bytes successfully transferred. */
	pos64 = add64ul(pos64, nbytes);
	for (;;) {
		if (nbytes < iov->iov_size) {
			/* Not done with this one yet. */
			vir_offset += nbytes;
			iov->iov_size -= nbytes;
			break;
		}
		nbytes -= iov->iov_size;
		vir_offset = 0;
		iov->iov_size = 0;
		if (nbytes == 0) {
			/* The rest is optional, so we return to give FS a
			 * chance to think it over.
			 */
			return(OK);
		}
		iov++;
		nr_req--;
	}
  }
  return(OK);
}

/*============================================================================*
 *				w_do_open				      *
 *============================================================================*/
PRIVATE int w_do_open(dp, m_ptr)
struct driver *dp;
message *m_ptr;
{
/* Device open: Initialize the controller and read the partition table. */

  static int init_done = FALSE;

  if (!init_done) { w_init(); init_done = TRUE; }

  if (w_prepare(m_ptr->DEVICE) == NIL_DEV) return(ENXIO);

  if (w_wn->open_ct++ == 0) {
	/* Partition the disk. */
	partition(&w_dtab, w_drive * DEV_PER_DRIVE, P_PRIMARY, 0);
  }
  return(OK);
}

/*============================================================================*
 *				w_do_close				      *
 *============================================================================*/
PRIVATE int w_do_close(dp, m_ptr)
struct driver *dp;
message *m_ptr;
{
/* Device close: Release a device. */

  if (w_prepare(m_ptr->DEVICE) == NIL_DEV) return(ENXIO);
  w_wn->open_ct--;
  return(OK);
}

/*===========================================================================*
 *				w_init					     *
 *===========================================================================*/
PRIVATE void w_init()
{
/* This routine is called at startup to initialize the drive parameters. */

  int r, drive, drive_id, nr_drives;
  struct wini *wn;
  unsigned long capacity;
  struct int13ext_params {
	u16_t	len;
	u16_t	flags;
	u32_t	cylinders;
	u32_t	heads;
	u32_t	sectors;
	u32_t	capacity[2];
	u16_t	bts_per_sec;
	u16_t	config[2];
  } *i13e_par;
  struct reg86u reg86;

  /* Ask the system task for a suitable buffer */
  if(!(bios_buf_v = alloc_contig(BIOSBUF, AC_LOWER1M, &bios_buf_phys))) {
  	panic(ME, "allocating bios buffer failed", r);
  }

  if (bios_buf_phys+BIOSBUF > 0x100000)
  	panic(ME, "bad BIOS buffer, phys", bios_buf_phys);
#if 0
  printf("bios_wini: got buffer size %d, virtual 0x%x, phys 0x%x\n",
  		BIOSBUF, bios_buf_v, bios_buf_phys);
#endif

  i13e_par = (struct int13ext_params *) bios_buf_v;

  /* Get the geometry of the drives */
  for (drive = 0; drive < MAX_DRIVES; drive++) {
  	if (remap_first)
  	{
  		if (drive == 7)
			drive_id= 0x80;
		else
			drive_id= 0x80 + drive + 1;
  	}
  	else
		drive_id= 0x80 + drive;

	(void) w_prepare(drive * DEV_PER_DRIVE);
	wn = w_wn;
	wn->drive_id= drive_id;

	reg86.u.b.intno = 0x13;
	reg86.u.b.ah = 0x08;	/* Get drive parameters. */
	reg86.u.b.dl = drive_id;
	r= sys_int86(&reg86);
	if (r != OK)
		panic(ME, "BIOS call failed", r);

	nr_drives = !(reg86.u.w.f & 0x0001) ? reg86.u.b.dl : drive;
	if (drive_id >= 0x80 + nr_drives) continue;
	wn->present= 1;

	wn->heads = reg86.u.b.dh + 1;
	wn->sectors = reg86.u.b.cl & 0x3F;
	wn->cylinders = (reg86.u.b.ch | ((reg86.u.b.cl & 0xC0) << 2)) + 1;

	capacity = (unsigned long) wn->cylinders * wn->heads * wn->sectors;

	reg86.u.b.intno = 0x13;
	reg86.u.b.ah = 0x41;	/* INT 13 Extensions - Installation check */
	reg86.u.w.bx = 0x55AA;
	reg86.u.b.dl = drive_id;

	if (pc_at) {
		r= sys_int86(&reg86);
		if (r != OK)
			panic(ME, "BIOS call failed", r);
	}

	if (!(reg86.u.w.f & 0x0001) && reg86.u.w.bx == 0xAA55
				&& (reg86.u.w.cx & 0x0001)) {
		/* INT 13 Extensions available. */
		i13e_par->len = 0x001E;	/* Input size of parameter packet */
		reg86.u.b.intno = 0x13;
		reg86.u.b.ah = 0x48;	/* Ext. Get drive parameters. */
		reg86.u.b.dl = drive_id;
		reg86.u.w.si = bios_buf_phys % HCLICK_SIZE;
		reg86.u.w.ds = bios_buf_phys / HCLICK_SIZE;

		r= sys_int86(&reg86);
		if (r != OK)
			panic(ME, "BIOS call failed", r);

		if (!(reg86.u.w.f & 0x0001)) {
			wn->int13ext = 1;	/* Extensions can be used. */
			capacity = i13e_par->capacity[0];
			if (i13e_par->capacity[1] != 0) capacity = 0xFFFFFFFF;
		}
	}

	if (wn->int13ext) {
		printf("%s: %lu sectors\n", w_name(), capacity);
	} else {
		printf("%s: %d cylinders, %d heads, %d sectors per track\n",
			w_name(), wn->cylinders, wn->heads, wn->sectors);
	}
	wn->part[0].dv_size = mul64u(capacity, SECTOR_SIZE);
  }
}

/*============================================================================*
 *				w_geometry				      *
 *============================================================================*/
PRIVATE void w_geometry(entry)
struct partition *entry;
{
  entry->cylinders = w_wn->cylinders;
  entry->heads = w_wn->heads;
  entry->sectors = w_wn->sectors;
}

/*============================================================================*
 *				w_other				      *
 *============================================================================*/
PRIVATE int w_other(dr, m, safe)
struct driver *dr;
message *m;
int safe;
{
        int r, timeout, prev;

        if (m->m_type != DEV_IOCTL_S )
                return EINVAL;

	if (m->REQUEST == DIOCOPENCT) {
                int count;
                if (w_prepare(m->DEVICE) == NIL_DEV) return ENXIO;
                count = w_wn->open_ct;
		if(safe) {
		   r=sys_safecopyto(m->IO_ENDPT, (vir_bytes)m->ADDRESS,
		       0, (vir_bytes)&count, sizeof(count), D);
		} else {
                   r=sys_datacopy(SELF, (vir_bytes)&count,
                        m->IO_ENDPT, (vir_bytes)m->ADDRESS, sizeof(count));
		}

		if(r != OK)
                        return r;
                return OK;
        }

        return EINVAL;
}

