/*
 * local_zeroinit_probe.c — a local aggregate initialized with `= {0}` (or a
 * partial initializer) must have EVERY member zeroed (C11 6.7.9p21: members
 * with no explicit initializer are initialized as if static => zero).
 *
 * The bug ([[minic-local-zeroinit-far]], §2j): minic lowers `S s = {0};` to a
 * compound-literal whose storage is zero-filled by a loop emitting NEAR
 * `storew 0` against `%_clit` with `=w add` offsets.  Under a far-data model
 * `%_clit` is a Kl (SS:offset) address, so `=w add` TRUNCATES the segment and
 * the zeros land in the wrong segment (DGROUP) — leaving the real stack slot as
 * garbage for every member past the first word.  Trailing pointer members then
 * read non-NULL garbage.
 *
 * Canonical victim: py/compile.c's `compiler_t comp_state = {0};` — the
 * `compile_error` field (a pointer, well past the first word) read non-NULL
 * garbage, so mp_compile raised a bogus exception on the (correct) parse tree of
 * `print(1+2)` (trace reached K3 with compile_error != NULL though nothing set
 * it).  Gated medium + compact + large.
 *
 * The §2j follow-up (this file's `init`/`mid`/`desig` value cases): a NON-zero
 * local aggregate initializer `S s = {a, b, &g, ...};` is now filled directly
 * into the destination (no compound-literal temp, no struct copy).  Under a
 * far-data model each member store MUST be `storef%c` at an `=l add` offset; a
 * near `=w add`/`store%c` truncates the segment of a pointer member written at
 * offset>0, so the pointer reads back with a garbage segment.  The cases below
 * DEREF a pointer member written past the first word, so a segment truncation
 * is bug-loud (wrong value / crash), not merely a NULL miss.
 */

#include <stdio.h>

/* Mixed members with pointers well past the first word — the compile_error
 * shape.  Big enough to exercise several zero-fill iterations. */
typedef struct {
	int a;
	int b;
	void *p1;
	int c;
	void *p2;
	long d;
	void *p3;
	int e[4];
	void *p4;
} big_t;

/* No array member — positional non-zero init without a braced sub-object. */
typedef struct {
	int a;
	void *p1;
	long b;
	void *p2;
} small_t;

static int all_zero(big_t *s)
{
	if (s->a || s->b || s->c || s->d || s->e[0] || s->e[1] || s->e[2] || s->e[3])
		return 0;
	if (s->p1 || s->p2 || s->p3 || s->p4)
		return 0;
	return 1;
}

int main(void)
{
	big_t s = {0};
	/* Pre-dirty an adjacent local so the stack slot is unlikely to be
	 * coincidentally zero (defeats a false pass). */
	volatile long dirt = -1L;
	(void)dirt;

	if (all_zero(&s)) printf("zero ok\r\n");
	else              printf("zero FAIL\r\n");

	/* Mid-block (C99) `= {0}` decl — exercises the stmt-rule path, distinct
	 * from the top-of-block dcls path that `s` above uses. */
	{
		big_t s2 = {0};
		if (all_zero(&s2)) printf("mid ok\r\n");
		else               printf("mid FAIL\r\n");
	}

	/* Report the pointer members individually so a failure is legible. */
	if (s.p1 == 0) printf("p1 ok\r\n"); else printf("p1 FAIL\r\n");
	if (s.p2 == 0) printf("p2 ok\r\n"); else printf("p2 FAIL\r\n");
	if (s.p3 == 0) printf("p3 ok\r\n"); else printf("p3 FAIL\r\n");
	if (s.p4 == 0) printf("p4 ok\r\n"); else printf("p4 FAIL\r\n");

	/* NON-zero POSITIONAL direct-fill, far-correctness guard.  The pointer
	 * members sit at offsets > 0; point them at known longs and deref
	 * through the struct so a truncated segment surfaces as a wrong value.
	 * (small_t has no array member: a braced array sub-object inside a
	 * compound literal is a separate, pre-existing minic gap, unrelated to
	 * the direct-fill far-correctness this case guards.) */
	{
		long g1 = 1111L;
		long g2 = 2222L;
		small_t v = { 11, &g1, 5555L, &g2 };   /* a, p1, b, p2 */
		int ok = 1;
		if (v.a != 11 || v.b != 5555L) ok = 0;
		if (*(long *)v.p1 != 1111L) ok = 0;  /* p1 at offset>0, deref */
		if (*(long *)v.p2 != 2222L) ok = 0;  /* last member, far offset */
		printf(ok ? "init ok\r\n" : "init FAIL\r\n");
	}

	/* Mid-block (stmt-rule) non-zero init — must re-fill on each entry. */
	{
		long h = 7777L;
		int i;
		int ok = 1;
		for (i = 0; i < 2; i++) {
			small_t w = { i, 0, 0, &h };   /* a, p1, b, p2 */
			if (w.a != i || w.b != 0 || w.p1 != 0) ok = 0;
			if (*(long *)w.p2 != 7777L) ok = 0;  /* deref far member */
		}
		printf(ok ? "mid2 ok\r\n" : "mid2 FAIL\r\n");
	}

	/* Designated-initializer direct fill into a big_t (a pointer member by
	 * name, well past the first word; the rest stay zeroed). */
	{
		long k = 8888L;
		big_t z = { .a = 1, .p3 = &k };
		int ok = 1;
		if (z.a != 1 || z.b != 0 || z.p1 != 0) ok = 0;
		if (*(long *)z.p3 != 8888L) ok = 0;
		printf(ok ? "desig ok\r\n" : "desig FAIL\r\n");
	}

	return 0;
}
