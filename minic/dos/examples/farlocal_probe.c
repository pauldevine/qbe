/*
 * farlocal_probe.c — far-data codegen for LOCAL aggregates and
 * multi-level pointers.  Pins three §1v fixes (all far-data only;
 * NEAR_DATA models were byte-identical so this is compact/large/huge):
 *
 *  1. i8086/isel.c — constant-size alloc in a NON-entry block.  minic
 *     emits a fixed `alloc4` at the C declaration point of a
 *     block-scoped local, which may sit inside a loop/if body (not the
 *     entry block).  The fast-alloc slot loop only scanned fn->start,
 *     so such an alloc reached emit as `Oalloc4 cls Kl` and died
 *     ("unsupported 32-bit op 81").  Now slotted in every block.
 *     (py/bc.c's mp_bytecode_get_source_line allocates its lineinfo
 *     buffer inside the decode loop — 23 core TUs hit this.)
 *
 *  2. minic.y — member-base address of a LOCAL aggregate under far-data.
 *     The member add used `=w add %local, off`, truncating the Kl slot
 *     address (ALLOC_T() is 'l'); the following loadfX then read the
 *     wrong place, and the const-fold case tripped gvn's assoccon KWIDE
 *     assert.  base_far now includes !NEAR_DATA() so it adds `=l`.
 *
 *  3. minic.y — multi-level pointer declarator dropped the pointee's FAR
 *     bit (`char **` built as far-ptr-to-NEAR-char*).  `*pp` then came
 *     out near, so `q - *pp` was a near-vs-far "non-homogeneous pointers
 *     in subtraction" error (and a miscompile where it slipped through).
 *     Fixed by keeping $1's FAR through IDIR_FAR (it shifts to the inner
 *     far position).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/farlocal_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/farlocal_probe/farlocal_probe.exe) \
 *              minic/dos/tests/farlocal_probe.golden.txt
 */

#include <stdio.h>

struct point { int x; int y; long tag; };
/* Word-sized members for the return-by-value case: a `long` member copied
 * out of a call result loses its high word ([[qbe-loadc-wordsize-i8086]],
 * pre-existing, model-independent) — orthogonal to the far-data fixes here,
 * and MicroPython's returned structs are word/size_t-sized anyway. */
struct triple { int a; int b; int c; };

/* Fill via a far pointer to a caller's local (escape use of &buf). */
static void fill3(int *buf, int base)
{
	buf[0] = base;
	buf[1] = base * 2;
	buf[2] = base * 3;
}

/* Return a struct by value built from a local — the shape that broke
 * (member-base add of a local aggregate, then far struct copy). */
static struct triple make_triple(int a, int b, int c)
{
	struct triple p;
	p.a = a;
	p.b = b;
	p.c = c;
	return p;
}

int main(void)
{
	int i, sum;
	struct point lp;
	struct triple r;
	char storage[8];
	char *p;
	char **pp;
	char *q;
	long diff;

	/* Fix 1: block-scoped array declared inside the loop body.  Its
	 * alloc lives in a non-entry block; old codegen died at qbe. */
	sum = 0;
	for (i = 0; i < 3; i++) {
		int buf[3];
		fill3(buf, i + 1);
		sum += buf[0] + buf[1] + buf[2];
	}
	/* (1+2+3)+(2+4+6)+(3+6+9) = 6+12+18 = 36 */
	if (sum == 36) printf("blockalloc ok\r\n");
	else           printf("blockalloc FAIL %d\r\n", sum);

	/* Fix 2: member access on a LOCAL aggregate (offset 0/2/4). */
	lp.x = 111;
	lp.y = 222;
	lp.tag = 333444L;
	if (lp.x == 111)      printf("localmem_x ok\r\n");   else printf("localmem_x FAIL %d\r\n", lp.x);
	if (lp.y == 222)      printf("localmem_y ok\r\n");   else printf("localmem_y FAIL %d\r\n", lp.y);
	if (lp.tag == 333444L) printf("localmem_tag ok\r\n"); else printf("localmem_tag FAIL %ld\r\n", lp.tag);

	/* Struct returned by value from a local (member-base + far copy). */
	r = make_triple(7, 9, 24680);
	if (r.a == 7 && r.b == 9 && r.c == 24680) printf("structret ok\r\n");
	else printf("structret FAIL %d %d %d\r\n", r.a, r.b, r.c);

	/* Fix 3: multi-level pointer.  *pp must be a far char*; q-*pp must
	 * be a homogeneous far-pointer subtraction yielding 3.  (Under huge,
	 * ptr-MINUS-ptr is computed on linear addresses via _qbe_huge_cmp —
	 * §1x; flat sub stays correct under compact/large/near.) */
	storage[0] = 'a'; storage[1] = 'b'; storage[2] = 'c';
	storage[3] = 'd'; storage[4] = 'e'; storage[5] = '\0';
	p = storage;
	pp = &p;
	if ((*pp)[0] == 'a' && (*pp)[2] == 'c') printf("dptr_deref ok\r\n");
	else printf("dptr_deref FAIL %c%c\r\n", (*pp)[0], (*pp)[2]);
	q = *pp + 3;
	diff = q - *pp;
	if (diff == 3 && *q == 'd') printf("dptr_diff ok\r\n");
	else printf("dptr_diff FAIL %ld %c\r\n", diff, *q);

	return 0;
}
