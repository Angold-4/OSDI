/* $Header$ */
/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
#define MININT		(1 << (sizeof(int) * 8 - 1))
#define MAXCHUNK	(~MININT)	/* Highest count we read(2).	*/
/* Unfortunately, MAXCHUNK is too large with some  compilers. Put it in
   an int!
*/

static int maxchunk = MAXCHUNK;

/*
 * We don't have to worry about byte order here.
 * Just read "cnt" bytes from file-descriptor "fd".
 */
int 
rd_bytes(fd, string, cnt)
	register char	*string;
	register long	cnt;
{

	while (cnt) {
		register int n = cnt >= maxchunk ? maxchunk : cnt;

		if (read(fd, string, n) != n)
			rd_fatal();
		string += n;
		cnt -= n;
	}
}
