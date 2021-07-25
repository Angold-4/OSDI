/* Ben Gras
 *
 * Based on sizeup() in mkfs.c.
 */

#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <ibm/partition.h>
#include <minix/partition.h>
#include <minix/u64.h>
#include <sys/ioc_disk.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

unsigned long sizeup(char *);

int main(int argc, char *argv[])
{
  int sec;

  if(argc != 2) {
	fprintf(stderr, "Usage: %s <device>\n", argv[0]);
	return 1;
  }

  printf("%lu\n", sizeup(argv[1]));
  return 0;
}	


unsigned long sizeup(device)
char *device;
{
  int fd;
  struct partition entry;
  unsigned long d;
  struct stat st;

  if ((fd = open(device, O_RDONLY)) == -1) {
  	perror("sizeup open");
  	exit(1);
  }
  if (ioctl(fd, DIOCGETP, &entry) == -1) {
  	perror("sizeup ioctl");
  	exit(1);
  }
  close(fd);
  d = div64u(entry.size, 512);
  return d;
}
