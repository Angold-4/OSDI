/* 
 * sym.c for mdb 
 */

#include "mdb.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <a.out.h>
#include "proto.h"

#if	GNU_SUPPORT
#define ZMAGIC 0413
#define NMAGIC 0410
#define QMAGIC 0314
#endif

struct symtab_s
{
	struct nlist *start;
	struct nlist *end;
	int text;
	int data;
	unsigned nsym;
};

PRIVATE struct symtab_s symtab;
PRIVATE int type_of_exec;

FORWARD _PROTOTYPE( int check_exec, (struct exec *hdr) );
FORWARD _PROTOTYPE( void sortsyms , (struct nlist *array , struct nlist *top ));
FORWARD _PROTOTYPE( int symeq , (char *t , struct nlist *sp ));
FORWARD _PROTOTYPE( int symprefix , (char *t , struct nlist *sp ));
FORWARD _PROTOTYPE( struct nlist *findsname, (char *name, int is_text, int allflag) );
FORWARD _PROTOTYPE( void outsym, (struct nlist *sp, off_t off) );
FORWARD _PROTOTYPE( struct nlist *findsval, (off_t value, int where) );

PUBLIC void syminit( filename )
char *filename;
{
	int fd;
	struct exec header;
	register struct symtab_s *tp;

	tp = &symtab;
	if ( (fd = open( filename, O_RDONLY)) < 0) {
		fprintf(stderr, "Couldn't open %s.\n", filename);
		perror(filename);
		exit(1);
	}

	if( read( fd, (char *) &header, sizeof header ) != sizeof header )
	{
		fprintf(stderr, "Couldn't read %d bytes from %s.\n", sizeof(header), filename);
		close( fd );
		exit(1);
	}	
	type_of_exec = check_exec(&header);

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS) {
		close(fd);
		gnu_init(filename);
		return;
	}
#endif

	/* For MINIX EXEC */
	if ( lseek( fd, A_SYMPOS( header ), 0 ) != A_SYMPOS( header ) )
	{
		do_error( "mdb - reading header" );
		close( fd );
		exit(1);
	}
	if ( (int) header.a_syms < 0 ||
             (unsigned) header.a_syms != header.a_syms ||
	     (tp->start = (struct nlist *) malloc( (unsigned) header.a_syms ))
	     			== (struct nlist *) NULL &&
	     header.a_syms != 0 )
	{
		Printf("mdb: no room for symbol table" );
		close( fd );
		return;
	}
	if ( read( fd, (char *) tp->start, (int) header.a_syms ) < 0 )
	{
		do_error( "mdb - reading symbol table" );
		close( fd );
		return;
	}
	close( fd );
	tp->nsym = (unsigned) header.a_syms / sizeof (struct nlist);
	tp->end = tp->start + tp->nsym;
	tp->text = 0x07;
	tp->data = 0x0F;

	/* sort on value only, name search not used much and storage a problem */
	Printf("Sorting %d MINIX symbols ....", tp->nsym );
	sortsyms( tp->start, tp->end );
	Printf("\n");
}

/* Check exec file 
 * return type of exec
 * or exit
 */
PRIVATE int check_exec(hdr)
struct exec *hdr;
{
long magic;

  /* Check MAGIC number */
  if (hdr->a_magic[0] != A_MAGIC0 || hdr->a_magic[1] != A_MAGIC1) {
#if	GNU_SUPPORT
	memcpy(&magic, hdr, sizeof(long));
	/* Clear bits */
	magic &= 0xFFFF;

	if ( magic == ZMAGIC || magic == QMAGIC ) {
	    is_separate = FALSE;
	    return GNU_SYMBOLS;
	}
	if ( magic == NMAGIC ) {
	    is_separate = TRUE;
	    return GNU_SYMBOLS;
	}
#endif
	Printf("mdb: invalid magic number in exec header - %02x %02x\n",
	hdr->a_magic[0], 
	hdr->a_magic[1]);
	exit(1);
  }

  /* Check CPU */
#if (CHIP == INTEL)
#if (_WORD_SIZE == 4)
  if (hdr->a_cpu != A_I80386)
#else
  if (hdr->a_cpu != A_I8086)
#endif
#endif 
#if (CHIP == M68000)
  if (hdr->a_cpu != A_M68K)
#endif 
  {
 	Printf("mdb: invalid cpu in exec header - %04x\n",
	hdr->a_cpu);
	exit(1);
  }

  is_separate = FALSE;
#ifdef MINIX_PC
  if (hdr->a_flags & A_SEP)
  	is_separate = TRUE;
#endif 
/* 
 * A_EXEC is not being set by current cc 
 * It was set in Minix 1.5.0
 */ 
#if 0 
  /* Check flags - separate I & D or not */
  if (hdr->a_flags & A_EXEC)
	is_separate = FALSE;
  else {
	Printf("mdb: object file not exec %04x\n", 
	hdr->a_flags);
	exit(1);
  }
#endif
  return MINIX_SYMBOLS;
}


PUBLIC long symbolvalue( name, is_text )
char *name;
int is_text;
{
register struct nlist *sp;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS )
		return gnu_symbolvalue( name, is_text );
#endif
	
	/* For MINIX EXEC */
	sp = findsname(name, is_text, 0);
	if (sp != NULL) 
		return sp->n_value;
	else 
		return 0L;
}

PRIVATE struct nlist *findsname( name, is_text, allflag )
char *name;
int is_text;
int allflag;
{
	char *s;
	unsigned char sclass;
	int schar;
	char *send;
	register struct nlist *sp;
	register struct symtab_s *tp;

	tp = &symtab;
	if ( allflag )
	{
		/* find and print all matching symbols */
		for ( sp = tp->start; sp < tp->end; ++sp )
		{
			if ( symprefix( name, sp ) )
			{
				sp = sp;
				for ( s = sp->n_name, send = s + sizeof sp->n_name;
				      *s != 0 && s < send; ++s )
					outbyte( *s );
				for ( ; s <= send; ++s )
					outspace();
				switch( sp->n_sclass & N_SECT )
				{
					case N_ABS: schar = 'a'; break;
					case N_TEXT: schar = 't'; break;
					case N_DATA: schar = 'd'; break;
					case N_BSS: schar = 'b'; break;
					default: schar = '?'; break;
				}
				if ( (sp->n_sclass & N_CLASS) == C_EXT && schar != '?' )
					schar += 'A' - 'a';
				outbyte( schar );
				outspace();
#if (_WORD_SIZE == 2)
				outh16( (u16_t) sp->n_value );
#else
				outh32( sp->n_value );
#endif
				outbyte('\n');
			}
		}
	}
	else
	{
		/* find symbol by dumb linear search */
		for ( sp = tp->start; sp < tp->end; ++sp )
		{
			sclass = sp->n_sclass & N_SECT;
			if ( (is_text && sclass == N_TEXT ||
			      !is_text && (sclass == N_DATA || sclass == N_BSS)) &&
					 symeq( name, sp ) )
				return sp;
		}
	}
	return NULL;
}

PRIVATE struct nlist *findsval( value, where )
off_t value;
int where;
{
	int left;
	int middle;
	int right;
	unsigned char sclass;
	register struct nlist *sp;
	register struct symtab_s *tp;

	tp = &symtab;

	/* find last symbol with value <= desired one by binary search */
	for ( left = 0, right = tp->nsym - 1; left <= right; )
	{
		middle = (left + right) / 2;
		sp = tp->start + middle;
		if ( value < sp->n_value )
			right = middle - 1;
		else
			left = middle + 1;
	}
	if ( right >= 0 )
		/* otherwise tp->start + right may wrap around to > tp->start !! */
		for ( sp = tp->start + right; sp >= tp->start; --sp )
		{
			if ( (sp->n_sclass & N_CLASS) != C_EXT ) continue; 
			sclass = sp->n_sclass & N_SECT;
			if ( (where == CSEG && sclass == N_TEXT ||
						where != CSEG && (sclass == N_DATA || sclass == N_BSS)) )
			return sp;
		}
	return NULL;
}


PUBLIC void printhex(v)
off_t v;
{
    if ( v >= 65536L )
	outh32( v );
    else if ( v >= 256 )
	outh16( (u16_t) v );
    else
	outh8(  (u8_t) v );
}


PRIVATE void outsym( sp, off )
struct nlist *sp;
off_t off;
{
	register char *s;
	char *send;

	for ( s = sp->n_name, send = s + sizeof sp->n_name; *s != 0 && s < send; ++s )
		outbyte( *s );
	if ( (off -= sp->n_value) != 0 )
	{
		outbyte( '+' );
		printhex(off);
	}
}

/* shell sort symbols on value */

PRIVATE void sortsyms( array, top )
struct nlist *array;
struct nlist *top;
{
	int gap;
	int i;
	int j;
	register struct nlist *left;
	register struct nlist *right;
	struct nlist swaptemp;
	int size;

	size = top - array;
	/* choose gaps according to Knuth V3 p95 */
	for ( gap = 1, i = 4; (j = 3 * i + 1) < size; gap = i, i = j )
		;
	do
	{
		for ( j = gap; j < size; ++j )
			for ( i = j - gap; i >= 0; i -= gap )
			{
				left = array + i; 
				right = array + (i + gap);
				if ( (off_t) left->n_value <=
				     right->n_value )
					break;
				swaptemp = *left;
				*left = *right;
				*right = swaptemp;
			}
	}
	while ( (gap /= 3) != 0 );
}

PUBLIC void symbolic( value, separator )
off_t value;
int separator;
{
	register struct nlist *sp;
	long off;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS ) {
		gnu_symbolic( value, separator );
		return;
	}
#endif

	/* For MINIX EXEC */

	if (value < st_addr || value > end_addr) {
		outstr("0x");
		printhex(value);
		outbyte(separator);
		return;
	}

	if ( (sp = findsval( value, CSEG )) != NULL )
	{
		outsym( sp, value );
	}
	else if ( (sp = findsval( value, DSEG )) != NULL )
	{
		outsym( sp, value );
	}
	else
	{
		outstr("_start");
		off = value - st_addr; 
		if ( off != 0 )  
		{
		outbyte( '+' );
		printhex(off);
		}
	}
	outbyte( separator );
}


PRIVATE int symeq( t, sp )
register char *t;
struct nlist *sp;
{
	return strncmp( t, sp->n_name, sizeof sp->n_name ) == 0;
}

PRIVATE int symprefix( t, sp )
register char *t;
struct nlist *sp;
{
	register char *s;
	char *send;

	for ( ; *t == '_'; ++t )
		;
	for ( s = sp->n_name, send = s + sizeof sp->n_name;
	      s < send && *s == '_'; ++s )
		;
	return strncmp( s, t, (size_t)(send - s) ) == 0;
}



/* list all symbols - test for selection criteria */

PUBLIC void listsym(cmd)
char *cmd;
{
	register struct symtab_s *tp;
	register struct nlist *sp;
	char *s;
	char *send;
	char schar;	
	char tchar;

	/* set selection */
	cmd = skip(cmd+1);
	if( *cmd == '\n' || *cmd == ';' ) 
		tchar = '*';
	else
		tchar = *cmd;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS ) {
		gnu_listsym(tchar);
		return;
	}
#endif

	/* For MINIX EXEC */

	tp = &symtab;
    	for ( sp = tp->start; sp < tp->end; ++sp )
	{
	     switch( sp->n_sclass & N_SECT )
	     {
			case N_ABS:	schar = 'a'; break;
			case N_TEXT:	schar = 't'; break;
			case N_DATA:	schar = 'd'; break;
			case N_BSS:	schar = 'b'; break;
			default: 	schar = '?'; break;
	     }

	     if ( (sp->n_sclass & N_CLASS) == C_EXT && schar != '?' )
		schar += 'A' - 'a';

	     /* check for selection */	
	     if ( tchar != '*' && schar != tchar)
		continue; 	

	     /* print symbol type and value */	
	     for ( s = sp->n_name, send = s + sizeof sp->n_name;
		 *s != 0 && s < send; ++s ) outbyte( *s );
	     for ( ; s <= send; ++s ) outspace();
	     outbyte( schar );
	     outspace();
#if (_WORD_SIZE == 2)
	     outh16( (u16_t) sp->n_value );
#else
	     outh32( sp->n_value );
#endif
	     outbyte('\n');
	}
}


PUBLIC int text_symbol(value)
off_t value;
{
struct nlist *sp;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS ) 
	    return gnu_text_symbol(value);
#endif

	if ((sp = findsval(value, CSEG)) != NULL && sp->n_value == value)
	{
	    outsym(sp, value);
	    return TRUE;
	 }
	else
	    return FALSE;
}

PUBLIC int finds_data(off,data_seg)
off_t off;
int data_seg;
{
struct nlist *sp;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS )
		return gnu_finds_data(off,data_seg);
#endif

	if ((sp = findsval(off, data_seg)) != NULL)
   	{
	    outsym(sp, off);
	    return TRUE;
    	}
    	else 
	    return FALSE;
}

PUBLIC int finds_pc(pc)
off_t pc;
{
struct nlist *sp;

#if	EXTRA_SYMBOLS
	if ( type_of_exec == GNU_SYMBOLS )
		return gnu_finds_pc(pc);
#endif

	if ((sp = findsval(pc, CSEG)) != NULL)
    	{
	    outsym(sp, pc);
	    return TRUE;
        }
	else
	    return FALSE;
}
