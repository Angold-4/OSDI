/**
 * Copyright (c) 1985 Sun Microsystems, Inc.
 * Copyright (c) 1980 The Regents of the University of California.
 * Copyright (c) 1976 Board of Trustees of the University of Illinois.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley, the University of Illinois,
 * Urbana, and Sun Microsystems, Inc.  The name of either University
 * or Sun Microsystems may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * Argument scanning and profile reading code.  Default parameters are set
 * here as well.
 */

#define PUBLIC extern
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "globs.h"
#include "proto.h"

/* profile types */
#define	PRO_SPECIAL	1		/* special case */
#define	PRO_BOOL	2		/* boolean */
#define	PRO_INT		3		/* integer */
#define PRO_FONT	4		/* troff font */

/* profile specials for booleans */
#define	ON		1		/* turn it on */
#define	OFF		0		/* turn it off */

/* profile specials for specials */
#define	IGN		1		/* ignore it */
#define	CLI		2		/* case label indent (float) */
#define	STDIN		3		/* use stdin */
#define	KEY		4		/* type (keyword) */

/*
 * N.B.: because of the way the table here is scanned, options whose names
 * are substrings of other options must occur later; that is, with -lp vs -l,
 * -lp must be first.  Also, while (most) booleans occur more than once, the
 * last default value is the one actually assigned.
 */
struct pro
{
   char           *p_name;		/* name, eg -bl, -cli */
   int             p_type;		/* type (int, bool, special) */
   int             p_default;		/* the default value (if int) */
   int             p_special;		/* depends on type */
   int            *p_obj;		/* the associated variable */
}               pro[] =

{

   "T", PRO_SPECIAL, 0, KEY, 0,
   "bacc", PRO_BOOL, false, ON, &bl_around,
   "badp", PRO_BOOL, false, ON, &bl_at_proctop,
   "bad", PRO_BOOL, false, ON, &bl_aft_decl,
   "bap", PRO_BOOL, false, ON, &bl_a_procs,
   "bbb", PRO_BOOL, false, ON, &bl_bef_bk,
   "bc", PRO_BOOL, true, OFF, &ps.leave_comma,
   "bl", PRO_BOOL, true, OFF, &btype_2,
   "br", PRO_BOOL, true, ON, &btype_2,
   "bs", PRO_BOOL, false, ON, &Bill_Shannon,
   "cdb", PRO_BOOL, true, ON, &del_on_bl,
   "cd", PRO_INT, 0, 0, &ps.decl_com_ind,
   "ce", PRO_BOOL, true, ON, &cuddle_else,
   "ci", PRO_INT, 0, 0, &continuation_indent,
   "cli", PRO_SPECIAL, 0, CLI, 0,
   "c", PRO_INT, 33, 0, &ps.com_ind,
   "di", PRO_INT, 16, 0, &ps.decl_indent,
   "dj", PRO_BOOL, false, ON, &ps.ljust_decl,
   "d", PRO_INT, 0, 0, &ps.unindent_displace,
   "eei", PRO_BOOL, false, ON, &ex_expr_indent,
   "ei", PRO_BOOL, true, ON, &ps.else_if,
   "fbc", PRO_FONT, 0, 0, (int *) &blkcomf,
   "fbx", PRO_FONT, 0, 0, (int *) &boxcomf,
   "fb", PRO_FONT, 0, 0, (int *) &bodyf,
   "fc1", PRO_BOOL, true, ON, &format_col1_comments,
   "fc", PRO_FONT, 0, 0, (int *) &scomf,
   "fk", PRO_FONT, 0, 0, (int *) &keywordf,
   "fs", PRO_FONT, 0, 0, (int *) &stringf,
   "ip", PRO_BOOL, true, ON, &ps.indent_parameters,
   "i", PRO_INT, 8, 0, &ps.ind_size,
   "lc", PRO_INT, 0, 0, &bk_max_col,
   "lp", PRO_BOOL, true, ON, &lineup_to_parens,
   "l", PRO_INT, 78, 0, &max_col,
   "nbacc", PRO_BOOL, false, OFF, &bl_around,
   "nbadp", PRO_BOOL, false, OFF, &bl_at_proctop,
   "nbad", PRO_BOOL, false, OFF, &bl_aft_decl,
   "nbap", PRO_BOOL, false, OFF, &bl_a_procs,
   "nbbb", PRO_BOOL, false, OFF, &bl_bef_bk,
   "nbc", PRO_BOOL, true, ON, &ps.leave_comma,
   "nbs", PRO_BOOL, false, OFF, &Bill_Shannon,
   "ncdb", PRO_BOOL, true, OFF, &del_on_bl,
   "nce", PRO_BOOL, true, OFF, &cuddle_else,
   "ndj", PRO_BOOL, false, OFF, &ps.ljust_decl,
   "neei", PRO_BOOL, false, OFF, &ex_expr_indent,
   "nei", PRO_BOOL, true, OFF, &ps.else_if,
   "nfc1", PRO_BOOL, true, OFF, &format_col1_comments,
   "nip", PRO_BOOL, true, OFF, &ps.indent_parameters,
   "nlp", PRO_BOOL, true, OFF, &lineup_to_parens,
   "npcs", PRO_BOOL, false, OFF, &proc_calls_space,
   "npro", PRO_SPECIAL, 0, IGN, 0,
   "npsl", PRO_BOOL, true, OFF, &proc_str_line,
   "nps", PRO_BOOL, false, OFF, &ptr_binop,
   "nsc", PRO_BOOL, true, OFF, &star_comment_cont,
   "nsob", PRO_BOOL, false, OFF, &swallow_opt_bl,
   "nv", PRO_BOOL, false, OFF, &verbose,
   "pcs", PRO_BOOL, false, ON, &proc_calls_space,
   "psl", PRO_BOOL, true, ON, &proc_str_line,
   "ps", PRO_BOOL, false, ON, &ptr_binop,
   "sc", PRO_BOOL, true, ON, &star_comment_cont,
   "sob", PRO_BOOL, false, ON, &swallow_opt_bl,
   "st", PRO_SPECIAL, 0, STDIN, 0,
   "troff", PRO_BOOL, false, ON, &troff,
   "v", PRO_BOOL, false, ON, &verbose,
   /* whew! */
   0, 0, 0, 0, 0
};

/*
 * set_profile reads $HOME/.indent.pro and ./.indent.pro and handles
 * arguments given in these files.
 */
void set_profile()
{
   register FILE  *f;
   char            fname[BUFSIZ];
   static char     prof[] = ".indent.pro";

   sprintf(fname, "%s/%s", getenv("HOME"), prof);
   if ((f = fopen(fname, "r")) != NULL)
   {
      scan_profile(f);
      (void) fclose(f);
   }
   if ((f = fopen(prof, "r")) != NULL)
   {
      scan_profile(f);
      (void) fclose(f);
   }
}

void scan_profile(f)
   register FILE  *f;
{
   register int    i;
   register char  *p;
   char            buf[BUFSIZ];

   while (1)
   {
      for (p = buf; (i = getc(f)) != EOF && (*p = (char)i) > ' '; ++p);
      if (p != buf)
      {
	 *p++ = 0;
	 if (verbose)
	    printf("profile: %s\n", buf);
	 set_option(buf);
      } else if (i == EOF)
	 return;
   }
}

char           *param_start;

int eqin(s1, s2)
   register char  *s1;
   register char  *s2;
{
   while (*s1)
   {
      if (*s1++ != *s2++)
	 return (false);
   }
   param_start = s2;
   return (true);
}

/*
 * Set the defaults.
 */
void set_defaults()
{
   register struct pro *p;

   /* Because ps.case_indent is a float, we can't initialize it from
      the table: */
   ps.case_indent = 0;			/* -cli0.0 */
   for (p = pro; p->p_name; p++)
      if (p->p_type != PRO_SPECIAL && p->p_type != PRO_FONT)
	 *p->p_obj = p->p_default;
}

void set_option(arg)
   register char  *arg;
{
   register struct pro *p;

   arg++;				/* ignore leading "-" */
   for (p = pro; p->p_name; p++)
      if (*p->p_name == *arg && eqin(p->p_name, arg))
	 goto found;
   fprintf(stderr, "indent: unknown parameter \"%s\"\n", arg - 1);
   exit(1);
found:
   switch (p->p_type)
   {

   case PRO_SPECIAL:
      switch (p->p_special)
      {

      case IGN:
	 break;

      case CLI:
	 if (*param_start == 0)
	    goto need_param;
	 ps.case_indent = atoi(param_start);
	 break;

      case STDIN:
	 if (input == 0)
	    input = stdin;
	 if (output == 0)
	    output = stdout;
	 break;

      case KEY:
	 if (*param_start == 0)
	    goto need_param;
	 {
	    register char  *str = (char *) malloc(strlen(param_start) + 1);
	    strcpy(str, param_start);
	    addkey(str, 4);
	 }
	 break;

      default:
	 fprintf(stderr, "\
indent: set_option: internal error: p_special %d\n", p->p_special);
	 exit(1);
      }
      break;

   case PRO_BOOL:
      if (p->p_special == OFF)
	 *p->p_obj = false;
      else
	 *p->p_obj = true;
      break;

   case PRO_INT:
      if (*param_start == 0)
      {
   need_param:
	 fprintf(stderr, "indent: ``%s'' requires a parameter\n",
		 arg - 1);
	 exit(1);
      }
      *p->p_obj = atoi(param_start);
      break;

   case PRO_FONT:
      parsefont((struct fstate *) p->p_obj, param_start);
      break;

   default:
      fprintf(stderr, "indent: set_option: internal error: p_type %d\n",
	      p->p_type);
      exit(1);
   }
}
