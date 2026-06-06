/*
 * fnptr_argwiden_probe.c — the §2i wide-arg->narrow-param fix covers DIRECT
 * named calls only.  A call THROUGH a function pointer carries no recorded
 * parameter types (minic's fn-ptr type is FUNC(rettype) — no param list), so a
 * wide (`l`, 4-byte) argument handed to a narrow (`int`/`size_t` == `w`,
 * 2-byte) parameter of the pointed-to function is STILL not narrowed, shifting
 * every later stack argument by 2 bytes.
 *
 * KNOWN-FAILING / NOT GATED (§2j): this documents a real LATENT bug, but it is
 * NOT the §2i compile blocker (that turned out to be local-struct `= {0}`
 * partial zero-init — see local_zeroinit_probe.c).  Fixing this needs param
 * types carried through the fn-POINTER type (the type currently encodes only
 * the return type), so a name-keyed table like §2i's fnproto can't reach the
 * indirect/member-dispatch ('I') path.  Left here as a ready repro: bug-loud
 * `r FAIL 99` / `base FAIL 0` under medium/compact/large.  MicroPython's
 * MINIMUM-ROM config does NOT use the emit_t method table (MICROPY_EMIT_NATIVE
 * is 0, so EMIT_ARG is a direct mp_emit_bc_* call), so this gap does not
 * currently block the port.
 *
 * Same shape as argwiden_probe.c (a wide arg BEFORE a pointer-out arg) but every
 * call goes through a function pointer.
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

/* Three args, wide value in the MIDDLE — exactly the mp_parse_num_base shape. */
static int parse_like(const char *base, unsigned len, int *outbase)
{
	*outbase = 10;
	return (int)len + (base[0] - '0');   /* base[0]=='1' -> 1 */
}

typedef int (*store_fn)(unsigned, int *);
typedef int (*parse_fn)(const char *, unsigned, int *);

int main(void)
{
	long big = 7;            /* `l` value */
	int r = 99;             /* sentinel: must become 7 */
	int rv;
	char buf[8];
	const char *p, *q;
	int gotbase, plret;
	store_fn sf = store_via;
	parse_fn pf = parse_like;

	/* Case 1: wide arg then out-pointer, called through a fn pointer. */
	rv = sf(big, &r);
	if (r == 7)    printf("r ok\r\n");      else printf("r FAIL %d\r\n", r);
	if (rv == 8)   printf("rv ok\r\n");     else printf("rv FAIL %d\r\n", rv);

	/* Case 2: the pointer-difference passed INLINE as the middle arg, &out
	 * last, through a fn pointer (the emit_t method-dispatch shape). */
	strcpy(buf, "1+2");
	p = buf;            /* points at '1' */
	q = buf + 1;        /* points just past '1' */
	gotbase = 0;
	plret = pf(p, q - p, &gotbase);
	if (gotbase == 10) printf("base ok\r\n");   else printf("base FAIL %d\r\n", gotbase);
	if (plret == 2)    printf("plret ok\r\n");  else printf("plret FAIL %d\r\n", plret);

	return 0;
}
