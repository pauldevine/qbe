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

	return 0;
}
