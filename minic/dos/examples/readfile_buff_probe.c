/* readfile_buff_probe.c — closer replica of stevie/fileio.c readfile()
 * to isolate whether buff accumulates content correctly before strcpy.
 *
 * Mirrors:
 *   - `register` storage class on hot locals
 *   - 256-byte buff[]
 *   - getc loop with NL handling
 *   - prints buff hex dump right at the strcpy moment
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NL '\n'
#define NUL 0
#define MAXLINE 256

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buff[MAXLINE];
	register int i, c;
	register long nchars = 0;
	int linecnt = 0;
	register char *s;
	char *lp_s;

	if (argc < 2) { printf("usage\n"); return 1; }
	f = fopen(argv[1], "r");
	if (f == NULL) { printf("fopen failed\n"); return 1; }

	i = 0;
	do {
		c = getc(f);
		if (c == EOF) {
			if (i == 0) break;
			c = NL;
		}
		if (c == NL || i == (MAXLINE-1)) {
			buff[i] = '\0';
			/* Print buff content + strlen at this exact point */
			printf("L%d: i=%d buff[0..3]=%02x %02x %02x %02x strlen=%d str=[%s]\n",
				linecnt, i,
				(unsigned char)buff[0], (unsigned char)buff[1],
				(unsigned char)buff[2], (unsigned char)buff[3],
				(int)strlen(buff), buff);
			/* Simulate stevie's newline()+strcpy */
			lp_s = malloc(strlen(buff) + 1);
			strcpy(lp_s, buff);
			printf("  after strcpy: lp_s=[%s] strlen=%d\n", lp_s, (int)strlen(lp_s));
			i = 0;
			linecnt++;
		} else {
			buff[i++] = c;
		}
		nchars++;
	} while (1);
	fclose(f);
	printf("done linecnt=%d nchars=%ld\n", linecnt, nchars);
	return 0;
}
