/*
 * argwiden_probe.c — a WIDE (`l`, 4-byte) call argument handed to a NARROW
 * (`int`/`size_t` == `w`, 2-byte) prototype parameter must be NARROWED at the
 * call site, or every later stack argument shifts by 2 bytes.
 *
 * The bug ([[minic-wide-arg-narrow-param]], §2h diagnosis): minic recorded only
 * a function's RETURN type, never its parameter types, so emit_arg sized each
 * argument by the ARGUMENT'S own type.  Passing a `long` (or a far-pointer
 * difference, which is `l`) to an `int`/`size_t` parameter pushed 4 bytes where
 * the callee reads 2 — so the NEXT argument (here an `int *` out-pointer) was
 * read from the wrong slot, and the callee's write through it landed in garbage
 * while the caller's real variable stayed unchanged.  Canonical victim:
 * mp_parse_num_base(str, top - str, &base) — `*base = 10` wrote a wild address,
 * base stayed 0, and integer parsing of "1" raised ValueError.
 *
 * Mirrors that shape exactly: a wide arg BEFORE a pointer-out arg.  Without the
 * fix `*out` never receives the value (caller's r stays at its sentinel); with
 * it the write lands correctly.  Model-independent: on medium it is the
 * symmetric long->int-param shift; on compact/large it is the far-pointer-diff
 * case.  Gated medium + compact + large.
 */

#include <stdio.h>
#include <string.h>

/* Wide param (n) BEFORE the pointer-out param (out): if n is mis-sized the
 * callee reads `out` from the wrong stack slot. */
static int store_via(unsigned n, int *out)
{
	*out = (int)n;
	return (int)(n + 1);
}

/* Three args, wide value in the MIDDLE — exactly the mp_parse_num_base shape
 * (str, len, &base).  `len` is the far-pointer difference top-base. */
static int parse_like(const char *base, unsigned len, int *outbase)
{
	*outbase = 10;
	return (int)len + (base[0] - '0');   /* base[0]=='1' -> 1 */
}

int main(void)
{
	long big = 7;            /* `l` value */
	int r = 99;             /* sentinel: must become 7 */
	int rv;
	char buf[8];
	const char *p, *q;
	int gotbase, plret;

	/* Case 1: wide arg then out-pointer. */
	rv = store_via(big, &r);
	if (r == 7)    printf("r ok\r\n");      else printf("r FAIL %d\r\n", r);
	if (rv == 8)   printf("rv ok\r\n");     else printf("rv FAIL %d\r\n", rv);

	/* Case 2: the pointer-difference is passed INLINE as the middle arg (an `l`
	 * value, exactly like mp_parse_num_base(str, top - str, &base)), with &out
	 * last — so the call-site narrowing, not an intervening assignment, is what
	 * keeps the stack layout correct. */
	strcpy(buf, "1+2");
	p = buf;            /* points at '1' */
	q = buf + 1;        /* points just past '1' */
	gotbase = 0;
	plret = parse_like(p, q - p, &gotbase);
	if (gotbase == 10) printf("base ok\r\n");   else printf("base FAIL %d\r\n", gotbase);
	if (plret == 2)    printf("plret ok\r\n");  else printf("plret FAIL %d\r\n", plret);

	return 0;
}
