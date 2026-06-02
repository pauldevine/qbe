/*
 * nullarg_probe.c — a NARROW integer literal (NULL / 0, always `w' = 2 bytes)
 * handed to a WIDE pointer parameter (`l' = 4 bytes under far-data) must be
 * WIDENED at the call site, or the 2-byte push shifts every later stack arg.
 *
 * This is the reverse of argwiden_probe.c (wide arg -> narrow param).  §2i's
 * coerce_arg only fired when BOTH the arg and the param were integer scalars,
 * so a pointer param (KIND==PTR) was excluded and NULL stayed `w 0'.  Canonical
 * victim ([[minic-wide-arg-narrow-param]], §2n): MicroPython's
 *   mp_arg_parse_all(0, NULL, kw_args, n_allowed, allowed, out_vals)
 * pushed NULL as 2 bytes where the callee reads a 4-byte far pointer, shifting
 * n_allowed / allowed / out_vals -> the arg-parse loop ran wild over the stack
 * and print(1+2) hung before emitting `3'.
 *
 * Mirrors that shape: a NULL pointer arg that is NOT the last argument, with
 * scalar args after it that must arrive intact.  On medium (near data) a
 * pointer param is itself `w' so NULL already matches -> no-op; the bug only
 * bites under far-data, hence gated medium + compact + large.
 */

#include <stdio.h>

/* NULL as the 2nd arg (a far pointer), with a count and a long AFTER it —
 * exactly the mp_arg_parse_all(n_pos, pos, n_allowed, ...) layout. */
static long report(int a, const char *ptr, int b, long c)
{
	return (ptr == 0 ? 0L : 9000L) + (long)a * 100 + (long)b * 10 + c;
}

/* NULL as the 1st arg, with an out-pointer LAST: proves the trailing pointer
 * argument still lands in the right slot (the callee writes through it). */
static void fill_one(const char *ptr, int n, int *out)
{
	*out = (ptr == 0) ? n : -1;
}

int main(void)
{
	long rv;
	int dst;

	rv = report(0, NULL, 5, 7L);
	if (rv == 57L) printf("report ok\r\n"); else printf("report FAIL %ld\r\n", rv);

	dst = -2;
	fill_one(NULL, 42, &dst);
	if (dst == 42)  printf("out ok\r\n");   else printf("out FAIL %d\r\n", dst);

	return 0;
}
