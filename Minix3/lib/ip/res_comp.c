/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)res_comp.c	6.18 (Berkeley) 6/27/90";
#endif /* LIBC_SCCS and not lint */

#if _MINIX
#include <sys/types.h>
#include <stdlib.h>

#include <net/gen/in.h>
#include <net/gen/nameser.h>
#include <net/gen/resolv.h>

static int dn_find _ARGS(( const u_char *exp_dn, const u_char *msg,
	u_char **dnptrs, u_char **lastdnptr ));
int dn_skipname _ARGS(( const u_char *comp_dn, const u_char *eom ));

#define getshort _getshort
#define getlong _getlong
#define putshort __putshort
#define putlong __putlong
#else
#include <sys/types.h>
#include <stdio.h>
#include <arpa/nameser.h>

static dn_find();
#endif

#ifdef __STDC__
#define CONST	const
#else
#define CONST
#endif

/*
 * Expand compressed domain name 'comp_dn' to full domain name.
 * 'msg' is a pointer to the begining of the message,
 * 'eomorig' points to the first location after the message,
 * 'exp_dn' is a pointer to a buffer of size 'length' for the result.
 * Return size of compressed name or -1 if there was an error.
 */
dn_expand(msg, eomorig, comp_dn, exp_dn, length)
	CONST u_char *msg, *eomorig, *comp_dn;
	u_char *exp_dn;
	int length;
{
	register CONST u_char *cp;
	register u_char *dn;
	register int n, c;
	CONST u_char *eom;
	int len = -1, checked = 0;

	dn = exp_dn;
	cp = comp_dn;
	eom = exp_dn + length;
	/*
	 * fetch next label in domain name
	 */
	while (n = *cp++) {
		/*
		 * Check for indirection
		 */
		switch (n & INDIR_MASK) {
		case 0:
			if (dn != exp_dn) {
				if (dn >= eom)
					return (-1);
				*dn++ = '.';
			}
			if (dn+n >= eom)
				return (-1);
			checked += n + 1;
			while (--n >= 0) {
				if ((c = *cp++) == '.') {
					if (dn + n + 2 >= eom)
						return (-1);
					*dn++ = '\\';
				}
				*dn++ = c;
				if (cp >= eomorig)	/* out of range */
					return(-1);
			}
			break;

		case INDIR_MASK:
			if (len < 0)
				len = cp - comp_dn + 1;
			cp = msg + (((n & 0x3f) << 8) | (*cp & 0xff));
			if (cp < msg || cp >= eomorig)	/* out of range */
				return(-1);
			checked += 2;
			/*
			 * Check for loops in the compressed name;
			 * if we've looked at the whole message,
			 * there must be a loop.
			 */
			if (checked >= eomorig - msg)
				return (-1);
			break;

		default:
			return (-1);			/* flag error */
		}
	}
	*dn = '\0';
	if (len < 0)
		len = cp - comp_dn;
	return (len);
}

/*
 * Compress domain name 'exp_dn' into 'comp_dn'.
 * Return the size of the compressed name or -1.
 * 'length' is the size of the array pointed to by 'comp_dn'.
 * 'dnptrs' is a list of pointers to previous compressed names. dnptrs[0]
 * is a pointer to the beginning of the message. The list ends with NULL.
 * 'lastdnptr' is a pointer to the end of the arrary pointed to
 * by 'dnptrs'. Side effect is to update the list of pointers for
 * labels inserted into the message as we compress the name.
 * If 'dnptr' is NULL, we don't try to compress names. If 'lastdnptr'
 * is NULL, we don't update the list.
 */
int
dn_comp(exp_dn, comp_dn, length, dnptrs, lastdnptr)
	CONST u_char *exp_dn;
	u_char *comp_dn;
	int length;
	u_char **dnptrs, **lastdnptr;
{
	register u_char *cp;
	register CONST u_char *dn;
	register int c, l;
	u_char **cpp, **lpp, *sp, *eob;
	u_char *msg;

	dn = exp_dn;
	cp = comp_dn;
	eob = cp + length;
	if (dnptrs != NULL) {
		if ((msg = *dnptrs++) != NULL) {
			for (cpp = dnptrs; *cpp != NULL; cpp++)
				;
			lpp = cpp;	/* end of list to search */
		}
	} else
		msg = NULL;
	for (c = *dn++; c != '\0'; ) {
		/* look to see if we can use pointers */
		if (msg != NULL) {
			if ((l = dn_find(dn-1, msg, dnptrs, lpp)) >= 0) {
				if (cp+1 >= eob)
					return (-1);
				*cp++ = (l >> 8) | INDIR_MASK;
				*cp++ = l % 256;
				return (cp - comp_dn);
			}
			/* not found, save it */
			if (lastdnptr != NULL && cpp < lastdnptr-1) {
				*cpp++ = cp;
				*cpp = NULL;
			}
		}
		sp = cp++;	/* save ptr to length byte */
		do {
			if (c == '.') {
				c = *dn++;
				break;
			}
			if (c == '\\') {
				if ((c = *dn++) == '\0')
					break;
			}
			if (cp >= eob) {
				if (msg != NULL)
					*lpp = NULL;
				return (-1);
			}
			*cp++ = c;
		} while ((c = *dn++) != '\0');
		/* catch trailing '.'s but not '..' */
		if ((l = cp - sp - 1) == 0 && c == '\0') {
			cp--;
			break;
		}
		if (l <= 0 || l > MAXLABEL) {
			if (msg != NULL)
				*lpp = NULL;
			return (-1);
		}
		*sp = l;
	}
	if (cp >= eob) {
		if (msg != NULL)
			*lpp = NULL;
		return (-1);
	}
	*cp++ = '\0';
	return (cp - comp_dn);
}

/*
 * Skip over a compressed domain name. Return the size or -1.
 */
dn_skipname(comp_dn, eom)
	CONST u_char *comp_dn, *eom;
{
	register CONST u_char *cp;
	register int n;

	cp = comp_dn;
	while (cp < eom && (n = *cp++)) {
		/*
		 * check for indirection
		 */
		switch (n & INDIR_MASK) {
		case 0:		/* normal case, n == len */
			cp += n;
			continue;
		default:	/* illegal type */
			return (-1);
		case INDIR_MASK:	/* indirection */
			cp++;
		}
		break;
	}
	return (cp - comp_dn);
}

/*
 * Search for expanded name from a list of previously compressed names.
 * Return the offset from msg if found or -1.
 * dnptrs is the pointer to the first name on the list,
 * not the pointer to the start of the message.
 */
static int
dn_find(exp_dn, msg, dnptrs, lastdnptr)
	CONST u_char *exp_dn, *msg;
	u_char **dnptrs, **lastdnptr;
{
	CONST register u_char *dn, *cp;
	register u_char **cpp;
	register int n;
	CONST u_char *sp;

	for (cpp = dnptrs; cpp < lastdnptr; cpp++) {
		dn = exp_dn;
		sp = cp = *cpp;
		while (n = *cp++) {
			/*
			 * check for indirection
			 */
			switch (n & INDIR_MASK) {
			case 0:		/* normal case, n == len */
				while (--n >= 0) {
					if (*dn == '.')
						goto next;
					if (*dn == '\\')
						dn++;
					if (*dn++ != *cp++)
						goto next;
				}
				if ((n = *dn++) == '\0' && *cp == '\0')
					return (sp - msg);
				if (n == '.')
					continue;
				goto next;

			default:	/* illegal type */
				return (-1);

			case INDIR_MASK:	/* indirection */
				cp = msg + (((n & 0x3f) << 8) | *cp);
			}
		}
		if (*dn == '\0')
			return (sp - msg);
	next:	;
	}
	return (-1);
}

/*
 * Routines to insert/extract short/long's. Must account for byte
 * order and non-alignment problems. This code at least has the
 * advantage of being portable.
 *
 * used by sendmail.
 */

u16_t
getshort(msgp)
	CONST u8_t *msgp;
{
	return ((msgp[0] << 8) | (msgp[1] << 0));
}

u32_t
getlong(msgp)
	CONST u8_t *msgp;
{
	return (  ((u32_t) msgp[0] << 24)
		| ((u32_t) msgp[1] << 16)
		| ((u32_t) msgp[2] <<  8)
		| ((u32_t) msgp[3] <<  0));
}


void
putshort(s, msgp)
	register U16_t s;
	register u8_t *msgp;
{

	msgp[1] = s;
	msgp[0] = s >> 8;
}

void
putlong(l, msgp)
	register u32_t l;
	register u8_t *msgp;
{

	msgp[3] = l;
	msgp[2] = (l >>= 8);
	msgp[1] = (l >>= 8);
	msgp[0] = l >> 8;
}
