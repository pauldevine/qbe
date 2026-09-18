/*
 * twod_probe.c -- C-Kermit link campaign item 2 (G12): true 2-D arrays at
 * file scope, and brace initializers for arrays of array-typedef rows.
 *
 *     static char rq_tok[16][128+1];                 was a parse error
 *     static char *txtp[11][64] = { {..}, {..} };    was a parse error
 *     row_t tab[2] = { {1,2,3}, {4} };               SILENTLY WRONG before:
 *                                                    each braced row was read
 *                                                    as ONE element (tab got
 *                                                    {1,4}, 2 ints not 6)
 *
 * and sizeof(a[i]) of such an array gave a pointer's size, not the row's.
 * Rows may be short (zero-filled), braces may be elided, and a row may be
 * designated (`[2] = {..}`).  The initializers contain casts (`(char *)0`),
 * whose type keyword used to clear the array-typedef state mid-declaration.
 *
 * Bug-loud: the pre-fix compiler rejects the 2-D declarators; the typedef
 * rows alone print wrong values.  Output is values only: one golden.
 */
#include <stdio.h>
#include <string.h>

typedef int row_t[3];
typedef char *srow_t[2];

static char rq_tok[4][9];
static char *pats[3][4] = {
	{ (char *)0, (char *)0 },
	{ "*.txt", "*.c", "*.h" },
	[2] = { "*.bin" }
};
int grid[][3] = { { 1, 2, 3 }, { 4 }, 7, 8, 9, 10 };
long lg[2][2] = { { 70000L, 1 }, { 2, 3 } };
row_t tab[2] = { { 1, 2, 3 }, { 4 } };
static srow_t names[] = { { "ab", (char *)0 }, { "cd", "ef" } };

static int count(char *p[], int n)
{
	int i, c = 0;
	for (i = 0; i < n && p[i]; i++)
		c++;
	return c;
}

int main(void)
{
	int i, j, s;

	for (i = 0; i < 4; i++)
		strcpy(rq_tok[i], i & 1 ? "odd" : "even");
	rq_tok[3][1] = 'D';
	printf("rq %s %s %s %s\n", rq_tok[0], rq_tok[1], rq_tok[2], rq_tok[3]);
	printf("rq sizes %d %d\n", (int)sizeof(rq_tok), (int)sizeof(rq_tok[0]));

	printf("pats %d %d %d\n", count(pats[0], 4), count(pats[1], 4), count(pats[2], 4));
	printf("pats %s %s %s\n", pats[1][0], pats[1][2], pats[2][0]);
	printf("pats nulls %d\n", pats[1][3] == 0 && pats[2][1] == 0);

	s = 0;
	for (i = 0; i < 4; i++)
		for (j = 0; j < 3; j++)
			s += (i * 3 + j + 1) * grid[i][j];
	printf("grid %d rows %d\n", s, (int)(sizeof(grid) / sizeof(grid[0])));
	printf("lg %ld %ld\n", lg[0][0] + lg[1][1], lg[1][0]);

	printf("tab %d %d %d %d %d %d\n", tab[0][0], tab[0][1], tab[0][2],
	    tab[1][0], tab[1][1], tab[1][2]);
	printf("tab sizes %d %d\n", (int)(sizeof(tab) / sizeof(int)),
	    (int)(sizeof(tab[1]) / sizeof(int)));
	tab[1][2] = 9;
	printf("tab write %d %d\n", tab[1][2], tab[0][2]);
	printf("names %s %s %s %d\n", names[0][0], names[1][0], names[1][1],
	    names[0][1] == 0);
	printf("twod_probe done\n");
	return 0;
}
