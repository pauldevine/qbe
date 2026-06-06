/*
 * typedef_fnptr_probe.c — coerce arguments on an INDIRECT call through a
 * fn-ptr declared via a TYPEDEF (§2s).
 *
 * §2q' covered the plain `T (*fp)(...)' declarator and struct fn-ptr members,
 * but a fn-ptr declared through a TYPEDEF was left as a documented gap: minic
 * resolves the typedef name to its bare type at the variable/member
 * declaration, losing the proto, so coerce_arg never fired and a narrow arg
 * handed to a wide param shifted every later stack slot (the §2q LOAD_FAST
 * mechanism: a 2-byte push where the callee reads 4 bytes mis-reads `kind').
 *
 * §2s records the proto on the typedef entry (typh[].fpid, surfaced via
 * g_td_fpid) so a `F fp;' variable AND a `F cb;' struct member inherit it.
 * This probe mirrors argmix_probe.c's (l, w, l, w) shape with a narrow uint16_t
 * 3rd arg (widened w->l) and a trailing `int k', but every fn-ptr is declared
 * via the typedef `F'.  Without the §2s fix `k' is read from the wrong slot ->
 * the results come back non-zero.  Gated medium + compact + large (the middle
 * `unsigned long' arg is `l' in every model, so the coercion is forced even on
 * near-data medium where pointers are themselves `w').
 *
 * The method table is filled at RUNTIME (not a static-const initializer) so the
 * probe needs no --far-static-data, exactly as argmix_probe.c.
 */

#include <stdio.h>

static int g;

static int take4(void *p, unsigned a, unsigned long n, int k)
{
	if (p != (void *)&g) return -1000;          /* far ptr arrived intact   */
	return (int)(a * 1000u) + (int)(n * 100u) + k;
}

/* The typedef under test — the proto must transfer to every declarator. */
typedef int (*F)(void *p, unsigned a, unsigned long n, int k);

/* A struct fn-ptr member declared via the typedef name (`F cb;'). */
struct ops {
	F local;
};

int main(void)
{
	unsigned short narrow_n;     /* the uint16_t local_num analogue (Kw)     */
	F fp = take4;                /* fn-ptr VARIABLE declared via typedef     */
	struct ops bc_ops;
	const struct ops *t = &bc_ops;
	int r;

	bc_ops.local = take4;        /* runtime fill (see header note) */

	/* --- fn-ptr variable declared via typedef --- */
	narrow_n = 2;
	r = fp(&g, 1, narrow_n, 0);
	if (r == 1200) printf("tv0 ok\r\n"); else printf("tv0 FAIL %d\r\n", r);

	narrow_n = 5;
	r = (*fp)(&g, 6, narrow_n, 9);
	if (r == 6509) printf("tv9 ok\r\n"); else printf("tv9 FAIL %d\r\n", r);

	/* --- struct member declared via typedef --- */
	narrow_n = 2;
	r = t->local(&g, 1, narrow_n, 0);
	if (r == 1200) printf("tm0 ok\r\n"); else printf("tm0 FAIL %d\r\n", r);

	narrow_n = 3;
	r = t->local(&g, 4, narrow_n, 7);
	if (r == 4307) printf("tm7 ok\r\n"); else printf("tm7 FAIL %d\r\n", r);

	return 0;
}
