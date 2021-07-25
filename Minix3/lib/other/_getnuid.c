#include <lib.h>
#define getnuid	_getnuid
#include <unistd.h>

PUBLIC uid_t getnuid(endpoint_t proc_ep)
{
  message m;
  m.m1_i1 = proc_ep;		/* search uid for this process */
  if (_syscall(MM, GETEPINFO, &m) < 0) return ( (uid_t) -1);
  return( (uid_t) m.m2_i1);	/* return search result */
}
