/*
 * char_promote_probe.c — bug-loud gate for integer promotion of char
 * arithmetic (C11 6.3.1.1).
 *
 * THE BUG: minic's prom() typed `char OP char` as CHAR (both operands had
 * the same ctyp, so the "same type → that type" rule won).  The 16-bit
 * word result was computed correctly, but any later int-context use of the
 * CHAR-typed temp re-extended it as a BYTE: `int s; s += c1 + c2` with
 * c1=114, c2=101 gave extsb(215) = -41 instead of 215.  Found by the
 * gcc.dg runtime sweep (tools/gccdg) via the struct_array_init probe's
 * block-sum check.
 *
 * THE FIX: char OP char promotes both operands (extsb/extub per their own
 * signedness) and yields INT, like the comparison path always did.
 * Golden = plain C semantics (native cc run), model independent; on the
 * UNFIXED compiler the sums/products below print byte-wrapped values.
 */
#include <stdio.h>

static char cf(void) { return 100; }

int main(void)
{
	char a = 114, b = 101, m = 20;
	unsigned char ua = 200, ub = 100;
	char neg = -56;
	int s = 0, t, u = 0, p, q, r, w;

	s += a + b;                 /* 215: compound assign of a char sum */
	t = a + b;                  /* 215 */
	u += a * m;                 /* 2280: char product */
	p = ua + ub;                /* 300: unsigned char sum (promotes to int) */
	q = neg + a;                /* 58: signed char with negative value */
	r = (a + b) * 2;            /* 430: promoted sum reused */
	w = cf() + cf() + cf();     /* 300: char-returning calls */
	printf("s=%d t=%d u=%d p=%d q=%d r=%d w=%d\n", s, t, u, p, q, r, w);
	printf("cmp %d %d %d\n", (a + b) > 200, (ua + ub) == 300, (a - b) == 13);
	/* bitwise NOT promotes too: ~(unsigned char)200 is -201, not 55 (and the
	 * op was emitted with the byte memory class, which QBE rejects) */
	printf("not %d %d %d\n", ~ua, ~m, (int)(short)~(short)5);
	printf("char_promote_probe done\n");
	return 0;
}
