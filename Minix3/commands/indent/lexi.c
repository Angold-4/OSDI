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
 * Here we have the token scanner for indent.  It scans off one token and
 * puts it in the global variable "token".  It returns a code, indicating the
 * type of token scanned.
 */

#define PUBLIC extern
#include <ctype.h>
#include <string.h>
#include "globs.h"
#include "codes.h"
#include "proto.h"

#define alphanum 1
#define opchar 3

struct templ
{
   char           *rwd;
   int             rwcode;
};

struct templ    specials[100] =
{
   "switch", 1,
   "case", 2,
   "break", 0,
   "struct", 3,
   "union", 3,
   "enum", 3,
   "default", 2,
   "int", 4,
   "char", 4,
   "float", 4,
   "double", 4,
   "long", 4,
   "short", 4,
   "typedef", 4,
   "unsigned", 4,
   "register", 4,
   "static", 4,
   "global", 4,
   "extern", 4,
   "void", 4,
   "goto", 0,
   "return", 0,
   "if", 5,
   "while", 5,
   "for", 5,
   "else", 6,
   "do", 6,
   "sizeof", 7,
   0, 0
};

char            chartype[128] =
{					/* this is used to facilitate
					   the decision of what type
					   (alphanumeric, operator)
					   each character is */
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 3, 0, 0, 1, 3, 3, 0,
   0, 0, 3, 3, 0, 3, 0, 3,
   1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 0, 0, 3, 3, 3, 3,
   0, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 0, 0, 0, 3, 1,
   0, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 0, 3, 0, 3, 0
};




int
lexi()
{
   register char  *tok;			/* local pointer to next char
					   in token */
   int             unary_delim;		/* this is set to 1 if the
					   current token
					
					forces a following operator to
					   be unary */
   static int      last_code;		/* the last token type returned */
   static int      l_struct;		/* set to 1 if the last token
					   was 'struct' */
   int             code;		/* internal code to be returned */
   char            qchar;		/* the delimiter character for
					   a string */

   tok = token;				/* point to start of place to
					   save token */
   unary_delim = false;
   ps.col_1 = ps.last_nl;		/* tell world that this token
					   started in column 1 iff the
					   last thing scanned was nl */
   ps.last_nl = false;

   while (*buf_ptr == ' ' || *buf_ptr == '\t')
   {					/* get rid of blanks */
      ps.col_1 = false;			/* leading blanks imply token
					   is not in column 1 */
      if (++buf_ptr >= buf_end)
	 fill_buffer();
   }

   /* Scan an alphanumeric token */
   if (chartype[*buf_ptr] == alphanum || (buf_ptr[0] == '.' && isdigit(buf_ptr[1])))
   {
      /* we have a character or number */
      register char  *j;		/* used for searching thru list
					   of
					
					reserved words */
      register struct templ *p;

      if (isdigit(*buf_ptr) || (buf_ptr[0] == '.' && isdigit(buf_ptr[1])))
      {
	 int             seendot = 0, seenexp = 0;
	 if (*buf_ptr == '0' &&
	     (buf_ptr[1] == 'x' || buf_ptr[1] == 'X'))
	 {
	    *tok++ = *buf_ptr++;
	    *tok++ = *buf_ptr++;
	    while (isxdigit(*buf_ptr))
	       *tok++ = *buf_ptr++;
	 } else
	    while (1)
	    {
	       if (*buf_ptr == '.')
		  if (seendot)
		     break;
		  else
		     seendot++;
	       *tok++ = *buf_ptr++;
	       if (!isdigit(*buf_ptr) && *buf_ptr != '.')
		  if ((*buf_ptr != 'E' && *buf_ptr != 'e') || seenexp)
		     break;
		  else
		  {
		     seenexp++;
		     seendot++;
		     *tok++ = *buf_ptr++;
		     if (*buf_ptr == '+' || *buf_ptr == '-')
			*tok++ = *buf_ptr++;
		  }
	    }
	 if (*buf_ptr == 'L' || *buf_ptr == 'l')
	    *tok++ = *buf_ptr++;
      } else
	 while (chartype[*buf_ptr] == alphanum)
	 {				/* copy it over */
	    *tok++ = *buf_ptr++;
	    if (buf_ptr >= buf_end)
	       fill_buffer();
	 }
      *tok++ = '\0';
      while (*buf_ptr == ' ' || *buf_ptr == '\t')
      {					/* get rid of blanks */
	 if (++buf_ptr >= buf_end)
	    fill_buffer();
      }
      ps.its_a_keyword = false;
      ps.sizeof_keyword = false;
      if (l_struct)
      {					/* if last token was 'struct',
					   then this token should be
					   treated as a declaration */
	 l_struct = false;
	 last_code = ident;
	 ps.last_u_d = true;
	 return (decl);
      }
      ps.last_u_d = false;		/* Operator after indentifier
					   is binary */
      last_code = ident;		/* Remember that this is the
					   code we will return */

      /* This loop will check if the token is a keyword. */
      for (p = specials; (j = p->rwd) != 0; p++)
      {
	 tok = token;			/* point at scanned token */
	 if (*j++ != *tok++ || *j++ != *tok++)
	    continue;			/* This test depends on the
					   fact that identifiers are
					   always at least 1 character
					   long (ie. the first two
					   bytes of the identifier are
					   always meaningful) */
	 if (tok[-1] == 0)
	    break;			/* If its a one-character
					   identifier */
	 while (*tok++ == *j)
	    if (*j++ == 0)
	       goto found_keyword;	/* I wish that C had a
					   multi-level break... */
      }
      if (p->rwd)
      {					/* we have a keyword */
   found_keyword:
	 ps.its_a_keyword = true;
	 ps.last_u_d = true;
	 switch (p->rwcode)
	 {
	 case 1:			/* it is a switch */
	    return (swstmt);
	 case 2:			/* a case or default */
	    return (casestmt);

	 case 3:			/* a "struct" */
	    if (ps.p_l_follow)
	       break;			/* inside parens: cast */
	    l_struct = true;

	    /* Next time around, we will want to know that we have had
	       a 'struct' */
	 case 4:			/* one of the declaration
					   keywords */
	    if (ps.p_l_follow)
	    {
	       ps.cast_mask |= 1 << ps.p_l_follow;
	       break;			/* inside parens: cast */
	    }
	    last_code = decl;
	    return (decl);

	 case 5:			/* if, while, for */
	    return (sp_paren);

	 case 6:			/* do, else */
	    return (sp_nparen);

	 case 7:
	    ps.sizeof_keyword = true;
	 default:			/* all others are treated like
					   any other identifier */
	    return (ident);
	 }				/* end of switch */
      }					/* end of if (found_it) */
      if (*buf_ptr == '(' && ps.tos <= 1 && ps.ind_level == 0)
      {
	 register char  *tp = buf_ptr;
	 while (tp < buf_end)
	    if (*tp++ == ')' && *tp == ';')
	       goto not_proc;
	 strncpy(ps.procname, token, sizeof ps.procname - 1);
	 ps.in_par_decl = 1;
   not_proc:;
      }
      /* The following hack attempts to guess whether or not the
         current token is in fact a declaration keyword -- one that has
         been typedefd */
      if (((*buf_ptr == '*' && buf_ptr[1] != '=') || isalpha(*buf_ptr) || *buf_ptr == '_')
	  && !ps.p_l_follow
	  && !ps.block_init
	  && (ps.last_token == rparen || ps.last_token == semicolon ||
	      ps.last_token == decl ||
	      ps.last_token == lbrace || ps.last_token == rbrace))
      {
	 ps.its_a_keyword = true;
	 ps.last_u_d = true;
	 last_code = decl;
	 return decl;
      }
      if (last_code == decl)		/* if this is a declared
					   variable, then following
					   sign is unary */
	 ps.last_u_d = true;		/* will make "int a -1" work */
      last_code = ident;
      return (ident);			/* the ident is not in the list */
   }					/* end of procesing for alpanum
					   character */
   /* l l l Scan a non-alphanumeric token */
   *tok++ = *buf_ptr;			/* if it is only a
					   one-character token, it is
					   moved here */
   *tok = '\0';
   if (++buf_ptr >= buf_end)
      fill_buffer();

   switch (*token)
   {
   case '\n':
      unary_delim = ps.last_u_d;
      ps.last_nl = true;		/* remember that we just had a
					   newline */
      code = (had_eof ? 0 : newline);

      /* if data has been exausted, the newline is a dummy, and we
         should return code to stop */
      break;

   case '\'':				/* start of quoted character */
   case '"':				/* start of string */
      qchar = *token;
      if (troff)
      {
	 tok[-1] = '`';
	 if (qchar == '"')
	    *tok++ = '`';
	 tok = chfont(&bodyf, &stringf, tok);
      }
      do
      {					/* copy the string */
	 while (1)
	 {				/* move one character or
					   [/<char>]<char> */
	    if (*buf_ptr == '\n')
	    {
	       printf("%d: Unterminated literal\n", line_no);
	       goto stop_lit;
	    }
	    *tok = *buf_ptr++;
	    if (buf_ptr >= buf_end)
	       fill_buffer();
	    if (had_eof || ((tok - token) > (bufsize - 2)))
	    {
	       printf("Unterminated literal\n");
	       ++tok;
	       goto stop_lit;
	       /* get outof literal copying loop */
	    }
	    if (*tok == BACKSLASH)
	    {				/* if escape, copy extra char */
	       if (*buf_ptr == '\n')	/* check for escaped newline */
		  ++line_no;
	       if (troff)
	       {
		  *++tok = BACKSLASH;
		  if (*buf_ptr == BACKSLASH)
		     *++tok = BACKSLASH;
	       }
	       *++tok = *buf_ptr++;
	       ++tok;			/* we must increment this again
					   because we copied two chars */
	       if (buf_ptr >= buf_end)
		  fill_buffer();
	    } else
	       break;			/* we copied one character */
	 }				/* end of while (1) */
      } while (*tok++ != qchar);
      if (troff)
      {
	 tok = chfont(&stringf, &bodyf, tok - 1);
	 if (qchar == '"')
	    *tok++ = '\'';
      }
stop_lit:
      code = ident;
      break;

   case ('('):
   case ('['):
      unary_delim = true;
      code = lparen;
      break;

   case (')'):
   case (']'):
      code = rparen;
      break;

   case '#':
      unary_delim = ps.last_u_d;
      code = preesc;
      break;

   case '?':
      unary_delim = true;
      code = question;
      break;

   case (':'):
      code = colon;
      unary_delim = true;
      break;

   case (';'):
      unary_delim = true;
      code = semicolon;
      break;

   case ('{'):
      unary_delim = true;

      /* if (ps.in_or_st) ps.block_init = 1; */
      code = ps.block_init ? lparen : lbrace;
      break;

   case ('}'):
      unary_delim = true;
      code = ps.block_init ? rparen : rbrace;
      break;

   case 014:				/* a form feed */
      unary_delim = ps.last_u_d;
      ps.last_nl = true;		/* remember this so we can set
					   'ps.col_1' right */
      code = form_feed;
      break;

   case (','):
      unary_delim = true;
      code = comma;
      break;

   case '.':
      unary_delim = false;
      code = period;
      break;

   case '-':
   case '+':				/* check for -, +, --, ++ */
      code = (ps.last_u_d ? unary_op : binary_op);
      unary_delim = true;

      if (*buf_ptr == token[0])
      {
	 /* check for doubled character */
	 *tok++ = *buf_ptr++;
	 /* buffer overflow will be checked at end of loop */
	 if (last_code == ident || last_code == rparen)
	 {
	    code = (ps.last_u_d ? unary_op : postop);
	    /* check for following ++ or -- */
	    unary_delim = false;
	 }
      } else if (*buf_ptr == '=')
	 /* check for operator += */
	 *tok++ = *buf_ptr++;
      else if (*buf_ptr == '>')
      {
	 /* check for operator -> */
	 *tok++ = *buf_ptr++;
	 if (!ptr_binop)
	 {
	    unary_delim = false;
	    code = unary_op;
	    ps.want_blank = false;
	 }
      }
      break;				/* buffer overflow will be
					   checked at end of switch */

   case '=':
      if (ps.in_or_st)
	 ps.block_init = 1;
#ifdef undef
      if (chartype[*buf_ptr] == opchar)
      {					/* we have two char assignment */
	 tok[-1] = *buf_ptr++;
	 if ((tok[-1] == '<' || tok[-1] == '>') && tok[-1] == *buf_ptr)
	    *tok++ = *buf_ptr++;
	 *tok++ = '=';			/* Flip =+ to += */
	 *tok = 0;
      }
#else
      if (*buf_ptr == '=')
      {					/* == */
	 *tok++ = '=';			/* Flip =+ to += */
	 buf_ptr++;
	 *tok = 0;
      }
#endif
      code = binary_op;
      unary_delim = true;
      break;
      /* can drop thru!!! */

   case '>':
   case '<':
   case '!':				/* ops like <, <<, <=, !=, etc */
      if (*buf_ptr == '>' || *buf_ptr == '<' || *buf_ptr == '=')
      {
	 *tok++ = *buf_ptr;
	 if (++buf_ptr >= buf_end)
	    fill_buffer();
      }
      if (*buf_ptr == '=')
	 *tok++ = *buf_ptr++;
      code = (ps.last_u_d ? unary_op : binary_op);
      unary_delim = true;
      break;

   default:
      if (token[0] == '/' && *buf_ptr == '*')
      {
	 /* it is start of comment */
	 *tok++ = '*';

	 if (++buf_ptr >= buf_end)
	    fill_buffer();

	 code = comment;
	 unary_delim = ps.last_u_d;
	 break;
      }
      while (*(tok - 1) == *buf_ptr || *buf_ptr == '=')
      {
	 /* handle ||, &&, etc, and also things as in int *****i */
	 *tok++ = *buf_ptr;
	 if (++buf_ptr >= buf_end)
	    fill_buffer();
      }
      code = (ps.last_u_d ? unary_op : binary_op);
      unary_delim = true;


   }					/* end of switch */
   if (code != newline)
   {
      l_struct = false;
      last_code = code;
   }
   if (buf_ptr >= buf_end)		/* check for input buffer empty */
      fill_buffer();
   ps.last_u_d = unary_delim;
   *tok = '\0';				/* null terminate the token */
   return (code);
}

/*
 * Add the given keyword to the keyword table, using val as the keyword type
 */
void addkey(key, val)
   char           *key;
   int val;
{
   register struct templ *p = specials;
   while (p->rwd)
      if (p->rwd[0] == key[0] && strcmp(p->rwd, key) == 0)
	 return;
      else
	 p++;
   if (p >= specials + sizeof specials / sizeof specials[0])
      return;				/* For now, table overflows are
					   silently ignored */
   p->rwd = key;
   p->rwcode = val;
   p[1].rwd = 0;
   p[1].rwcode = 0;
   return;
}
