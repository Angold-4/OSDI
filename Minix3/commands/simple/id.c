/* id - return uid and gid		Author: John J. Marco */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
/* 		----- id.c -----					*/
/* Id - get real and effective user id and group id			*/
/* Author: John J. Marco						*/
/*	   pa1343@sdcc15.ucsd.edu					*/
/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

int main(int argc, char *argv[])
{
  struct passwd *pwd;
  struct group *grp;
  uid_t ruid, euid, uid;
  gid_t rgid, egid, gid;
#if __minix_vmd
  uid_t suid;
  gid_t sgid;
#else
# define suid ruid
# define sgid rgid
#endif
#if NGROUPS_MAX > 0
  gid_t groups[NGROUPS_MAX];
  int ngroups;
#else
# define groups (&rgid)
# define ngroups 0
#endif
  int g;
  int isug;
  int c, uopt = 0, gopt = 0, nopt = 0, ropt = 0;

#if __minix_vmd
  get6id(&ruid, &euid, &suid, &rgid, &egid, &sgid);
  isug = issetugid();
#else
  ruid = getuid();
  euid = geteuid();
  rgid = getgid();
  egid = getegid();
  isug = 0;
#endif
#if NGROUPS_MAX > 0
  ngroups = getgroups(NGROUPS_MAX, groups);
#endif

  while((c = getopt(argc, argv, "ugnr")) != EOF) {
	switch(c) {
		case 'u':
			uopt = 1;
			break;
		case 'g':
			gopt = 1;
			break;
		case 'n':
			nopt = 1;
			break;
		case 'r':
			ropt = 1;
			break;
		default:
			fprintf(stderr, "%s: unrecognized option\n", argv[0]);
			return(1);
	}
  }

  if(uopt && gopt) {
	fprintf(stderr, "%s: cannot combine -u and -g\n", argv[0]);
	return 1;
  }

  if((nopt || ropt) && !uopt && !gopt) {
	fprintf(stderr, "%s: cannot use -n or -r without -u or -g\n", argv[0]);
	return 1;
  }

  if(uopt) {
	uid = ropt ? ruid : euid;
	if (!nopt || (pwd = getpwuid(uid)) == NULL)
		printf("%u\n", uid);
	else
		printf("%s\n", pwd->pw_name);
	return 0;
  }
  if(gopt) {	
	gid = ropt ? rgid : egid;
	if (!nopt || (grp = getgrgid(gid)) == NULL)
		printf("%u\n", gid);
	else
		printf("%s\n", grp->gr_name);
	return 0;
  }

  if ((pwd = getpwuid(ruid)) == NULL)
	printf("uid=%d", ruid);
  else
	printf("uid=%d(%s)", ruid, pwd->pw_name);

  if ((grp = getgrgid(rgid)) == NULL)
	printf(" gid=%d", rgid);
  else
	printf(" gid=%d(%s)", rgid, grp->gr_name);

  if (euid != ruid)
	if ((pwd = getpwuid(euid)) != NULL)
		printf(" euid=%d(%s)", euid, pwd->pw_name);
	else
		printf(" euid=%d", euid);

  if (egid != rgid)
	if ((grp = getgrgid(egid)) != NULL)
		printf(" egid=%d(%s)", egid, grp->gr_name);
	else
		printf(" egid=%d", egid);

  if (suid != euid)
	if ((pwd = getpwuid(suid)) != NULL)
		printf(" suid=%d(%s)", suid, pwd->pw_name);
	else
		printf(" suid=%d", suid);

  if (sgid != egid)
	if ((grp = getgrgid(sgid)) != NULL)
		printf(" sgid=%d(%s)", sgid, grp->gr_name);
	else
		printf(" sgid=%d", sgid);

  if (isug) {
	printf(" issetugid");
  }

  if (ngroups > 0) {
	printf(" groups=");
	for (g = 0; g < ngroups; g++) {
		if (g > 0) fputc(',', stdout);
		if ((grp = getgrgid(groups[g])) == NULL)
			printf("%d", groups[g]);
		else
			printf("%d(%s)", groups[g], grp->gr_name);
	}
  }

  printf("\n");
  return(0);
}
