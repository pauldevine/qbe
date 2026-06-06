/*
 * longconst_probe.c — 32-bit `long` constants and `long` struct members.
 * Pins three §1w fixes (all model-INDEPENDENT — i8086 `int` is 16-bit, so
 * these bit under medium too; verified medium+compact+large+huge):
 *
 *  1. minic.y sext() — sign-extending a COMPILE-TIME CONSTANT emitted
 *     `=l extsw <const>`, and on i8086 `extsw` extends only the low 16
 *     bits, so `long x = 555666L` truncated to 31250 (and a bit-15-set
 *     value like 40000 went negative).  Now a constant is retyped LNG
 *     directly (its full value is already known).
 *
 *  2. minic.y lexer/'N' — integer literals were always typed INT and the
 *     L/l suffix was discarded, so a `long` literal wider than 16 bits
 *     (e.g. an argument `f(333444L)` to an `l` parameter, or a `%ld`
 *     vararg) was passed as a 16-bit word.  Literals now carry an `nlong`
 *     flag (L/l suffix, or value > 0xFFFF) and type LNG.
 *
 *  3. load.c def() — reconstructing a 4-byte slice (a `loadl` satisfied by
 *     two 2-byte `storew`s, e.g. a struct copy of a `long` member) used
 *     class Kw because the width test hardcoded `> 4`; on i8086 the
 *     `high << 16` then shifted a 16-bit temp to 0 and the high word was
 *     lost.  Fixed to `> T.wordsz` (no change on 32-bit-word targets).
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/longconst_probe.c
 */

#include <stdio.h>

struct rec { int x; int y; long tag; };

static struct rec make_rec(int x, int y, long tag)
{
	struct rec r;
	r.x = x;
	r.y = y;
	r.tag = tag;          /* long member set from a long parameter */
	return r;             /* struct copy of a long member (fix 3) */
}

int main(void)
{
	long a = 555666L;     /* fix 1: > 16 bits, was extsw-truncated to 31250 */
	long b = 40000L;      /* fix 1: bit 15 set, would go negative via extsw */
	long c = 0x000ABCDEL; /* fix 1/2: hex long const */
	struct rec r;

	if (a == 555666L)   printf("a ok\r\n");   else printf("a FAIL %ld\r\n", a);
	if (b == 40000L)    printf("b ok\r\n");   else printf("b FAIL %ld\r\n", b);
	if (c == 703710L)   printf("c ok\r\n");   else printf("c FAIL %ld\r\n", c);

	/* fix 2: long literal passed to a `long` parameter (must widen, not
	 * truncate to a 16-bit word). */
	r = make_rec(7, 9, 333444L);
	if (r.x == 7 && r.y == 9 && r.tag == 333444L) printf("structlong ok\r\n");
	else printf("structlong FAIL %d %d %ld\r\n", r.x, r.y, r.tag);

	/* fix 2: long literal as a %ld vararg (pushes 4 bytes, not 2). */
	printf("vararg %ld\r\n", 222333L);

	/* fold path: arithmetic on long constants stays 32-bit. */
	a = 100000L + 50000L;          /* 150000, overflows 16 bits */
	if (a == 150000L)  printf("addconst ok\r\n"); else printf("addconst FAIL %ld\r\n", a);

	return 0;
}
