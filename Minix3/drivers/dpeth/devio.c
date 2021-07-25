/*
**  File:	devio.c		Jun. 11, 2005
**
**  Author:	Giovanni Falzoni <gfalzoni@inwind.it>
**
**  This file contains the routines for readind/writing
**  from/to the device registers.
**
**  $Id: devio.c 2361 2006-07-10 12:43:38Z philip $
*/

#include "drivers.h"
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include "dp.h"

#if (USE_IOPL == 0)

static void warning(const char *type, int err)
{

  printf("Warning: eth#0 sys_%s failed (%d)\n", type, err);
  return;
}

/*
**  Name:	unsigned int inb(unsigned short int port);
**  Function:	Reads a byte from specified i/o port.
*/
PUBLIC unsigned int inb(unsigned short port)
{
  unsigned long value;
  int rc;

  if ((rc = sys_inb(port, &value)) != OK) warning("inb", rc);
  return value;
}

/*
**  Name:	unsigned int inw(unsigned short int port);
**  Function:	Reads a word from specified i/o port.
*/
PUBLIC unsigned int inw(unsigned short port)
{
  unsigned long value;
  int rc;

  if ((rc = sys_inw(port, &value)) != OK) warning("inw", rc);
  return value;
}

/*
**  Name:	unsigned int insb(unsigned short int port, int proc_nr, void *buffer, int count);
**  Function:	Reads a sequence of bytes from specified i/o port to user space buffer.
*/
PUBLIC void insb(unsigned short int port, int proc_nr, void *buffer, int count)
{
  int rc;

  if ((rc = sys_insb(port, proc_nr, buffer, count)) != OK)
	warning("insb", rc);
  return;
}

/*
**  Name:	unsigned int insw(unsigned short int port, int proc_nr, void *buffer, int count);
**  Function:	Reads a sequence of words from specified i/o port to user space buffer.
*/
PUBLIC void insw(unsigned short int port, int proc_nr, void *buffer, int count)
{
  int rc;

  if ((rc = sys_insw(port, proc_nr, buffer, count)) != OK)
	warning("insw", rc);
  return;
}

/*
**  Name:	void outb(unsigned short int port, unsigned long value);
**  Function:	Writes a byte to specified i/o port.
*/
PUBLIC void outb(unsigned short port, unsigned long value)
{
  int rc;

  if ((rc = sys_outb(port, value)) != OK) warning("outb", rc);
  return;
}

/*
**  Name:	void outw(unsigned short int port, unsigned long value);
**  Function:	Writes a word to specified i/o port.
*/
PUBLIC void outw(unsigned short port, unsigned long value)
{
  int rc;

  if ((rc = sys_outw(port, value)) != OK) warning("outw", rc);
  return;
}

/*
**  Name:	void outsb(unsigned short int port, int proc_nr, void *buffer, int count);
**  Function:	Writes a sequence of bytes from user space to specified i/o port.
*/
PUBLIC void outsb(unsigned short port, int proc_nr, void *buffer, int count)
{
  int rc;

  if ((rc = sys_outsb(port, proc_nr, buffer, count)) != OK)
	warning("outsb", rc);
  return;
}

/*
**  Name:	void outsw(unsigned short int port, int proc_nr, void *buffer, int count);
**  Function:	Writes a sequence of bytes from user space to specified i/o port.
*/
PUBLIC void outsw(unsigned short port, int proc_nr, void *buffer, int count)
{
  int rc;

  if ((rc = sys_outsw(port, proc_nr, buffer, count)) != OK)
	warning("outsw", rc);
  return;
}

#else
#error To be implemented
#endif				/* USE_IOPL */
/**  devio.c  **/
