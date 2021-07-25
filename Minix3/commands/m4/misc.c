/*
 * misc.c
 * Facility: m4 macro processor
 * by: oz
 */
 
#include "mdef.h"
#include "extr.h" 
 
/*
 * indx - find the index of second str in the
 *        first str.
 */
int indx(s1, s2)
char *s1;
char *s2;
{
        register char *t;
        register char *p;
        register char *m;
 
        for (p = s1; *p; p++) {
                for (t = p, m = s2; *m && *m == *t; m++, t++)
                        ;
                if (!*m)
                        return(p - s1);
        }
        return (-1);
}
 
/*
 *  putback - push character back onto input
 *
 */
void putback (c)
char c;
{
        if (bp < endpbb)
                *bp++ = c;
        else
                error("m4: too many characters pushed back");
}
 
/*
 *  pbstr - push string back onto input
 *          putback is replicated to improve
 *          performance.
 *
 */
void pbstr(s)
register char *s;
{
        register char *es;
	register char *zp;

	es = s;
	zp = bp;

        while (*es)
                es++;
        es--;
        while (es >= s)
                if (zp < endpbb)
                        *zp++ = *es--;
        if ((bp = zp) == endpbb)
                error("m4: too many characters pushed back");
}
 
/*
 *  pbnum - convert number to string, push back on input.
 *
 */
void pbnum (n)
int n;
{
        register int num;
 
        num = (n < 0) ? -n : n;
        do {
                putback(num % 10 + '0');
        }
        while ((num /= 10) > 0);

        if (n < 0) putback('-');
}
 
/*
 *  chrsave - put single char on string space
 *
 */
void chrsave (c)
char c;
{
/***        if (sp < 0)
                putc(c, active);
        else ***/ if (ep < endest)
                *ep++ = c;
        else
                error("m4: string space overflow");
}
 
/*
 * getdiv - read in a diversion file, and
 *          trash it.
 */
void getdiv(ind)
int ind;
{
        register int c;
        register FILE *dfil;
 
        if (active == outfile[ind])
                error("m4: undivert: diversion still active.");
        (void) fclose(outfile[ind]);
        outfile[ind] = NULL;
        m4temp[UNIQUE] = ind + '0';
        if ((dfil = fopen(m4temp, "r")) == NULL)
                error("m4: cannot undivert.");
        else
                while((c = getc(dfil)) != EOF)
                        putc(c, active);
        (void) fclose(dfil);

#if vms
        if (remove(m4temp))
#else
	if (unlink(m4temp) == -1)
#endif
                error("m4: cannot unlink.");
}
 
/*
 * Very fatal error. Close all files
 * and die hard.
 */
void error(s)
char *s;
{
        killdiv();
        fprintf(stderr,"%s\n",s);
        exit(1);
}
 
/*
 * Interrupt handling
 */
static char *msg = "\ninterrupted.";
 
void onintr(s) 
int s;				/* ANSI requires the parameter */
{
        error(msg);
}
 
/*
 * killdiv - get rid of the diversion files
 *
 */
void killdiv() {
        register int n;
 
        for (n = 0; n < MAXOUT; n++)
                if (outfile[n] != NULL) {
                        (void) fclose (outfile[n]);
                        m4temp[UNIQUE] = n + '0';
#if vms
			(void) remove (m4temp);
#else
                        (void) unlink (m4temp);
#endif
                }
}
 
/*
 * save a string somewhere..
 *
 */
char *strsave(s)
char *s;
{
	register int n;
        char *p;

	n = strlen(s)+1;
	p = (char *) malloc(n);
        if (p != NULL) (void) memcpy(p, s, n);
        return (p);
}
 
void usage() {
        fprintf(stderr, "Usage: m4 [-Dname[=val]] [-Uname]\n");
        exit(1);
}

#ifdef GETOPT
/*
 * H. Spencer getopt - get option letter from argv
 * 
 *
#include <stdio.h>
 *
 */

char	*optarg;	/* Global argument pointer. */
int	optind = 0;	/* Global argv index. */

static char	*scan = NULL;	/* Private scan pointer. */

int
getopt(argc, argv, optstring)
int argc;
char *argv[];
char *optstring;
{
	register char c;
	register char *place;

	optarg = NULL;

	if (scan == NULL || *scan == '\0') {
		if (optind == 0)
			optind++;
	
		if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
			return(EOF);
		if (strcmp(argv[optind], "--")==0) {
			optind++;
			return(EOF);
		}
	
		scan = argv[optind]+1;
		optind++;
	}

	c = *scan++;
	place = index(optstring, c);

	if (place == NULL || c == ':') {
		fprintf(stderr, "%s: unknown option -%c\n", argv[0], c);
		return('?');
	}

	place++;
	if (*place == ':') {
		if (*scan != '\0') {
			optarg = scan;
			scan = NULL;
		} else {
			optarg = argv[optind];
			optind++;
		}
	}

	return(c);
}
   
#endif

#ifdef DUFFCP
/*
 * This code uses Duff's Device (tm Tom Duff)
 * to unroll the copying loop:
 * while (count-- > 0)
 *	*to++ = *from++;
 */

#define COPYBYTE 	*to++ = *from++

void memcpy(to, from, count)
register char *from, *to;
register int count;
{
	if (count > 0) {
		register int loops = (count+8-1) >> 3;	/* div 8 round up */

		switch (count&(8-1)) {			/* mod 8 */
		case 0: do {
			COPYBYTE;
		case 7:	COPYBYTE;
		case 6:	COPYBYTE;
		case 5:	COPYBYTE;
		case 4:	COPYBYTE;
		case 3:	COPYBYTE;
		case 2:	COPYBYTE;
		case 1:	COPYBYTE;
			} while (--loops > 0);
		}

	}
}

#endif
