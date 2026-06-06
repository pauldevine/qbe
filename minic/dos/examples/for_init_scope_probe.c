/*
 * for_init_scope_probe.c -- inner-block scope for a C99 for-init
 * declarator, needed by py/compile.c (MicroPython port; see
 * NEXT_SESSION.md / [[minic-inner-block-scope]]).
 *
 * §1k gave block-scoped statement declarations alpha-renaming, but a
 * for-init declarator's uses (test, increment, body) are all lexed
 * inside the single FOR production -- before that production reduces --
 * so a varadd/rename done at reduce time was too late to stamp them.
 * c_del_stmt in py/compile.c has two sibling for-loops, `for (int i…)`
 * then `for (size_t i…)`, which collided with "double definition".
 *
 * The first for-init declarator is now reduced by its own `forinit_var`
 * nonterminal at `type IDENT =`, a single-action state miniyacc
 * default-reduces without lexing lookahead, so the rename binding is
 * established before the test/increment/body uses are lexed and the
 * lexer stamps them to the renamed slot.  Cases (c)/(d) re-check that
 * the factoring left the two-pointer and multi-scalar for-init forms
 * working (regression guard).
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/for_init_scope_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/for_init_scope_probe/for_init_scope_probe.exe \
 *             | diff - minic/dos/tests/for_init_scope_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

int
main(void)
{
	long total;

	/* (a) Sibling for-loops reuse one name with DIFFERENT types -- the
	 * compile.c c_del_stmt case.  The second loop's `i` must rename and
	 * its test/increment/body uses must resolve to the renamed slot. */
	total = 0;
	for (int i = 0; i < 4; i++)
		total += i;                  /* 0+1+2+3 = 6 */
	for (long i = 0; i < 3; i++)
		total += i * 10;             /* 0+10+20 = 30 */
	printf("a=%ld\r\n", total);          /* 36 */

	/* (b) Sibling for-loops reuse one name with the SAME type -- must
	 * still fold to one slot (no spurious rename, stevie behaviour). */
	total = 0;
	for (int j = 0; j < 5; j++)
		total += j;                  /* 0+1+2+3+4 = 10 */
	for (int j = 0; j < 2; j++)
		total += j;                  /* 0+1 = 1 */
	printf("b=%ld\r\n", total);          /* 11 */

	/* (c) Multi-scalar for-init still works after the factoring: two
	 * declarators sharing one base type, second one constant. */
	total = 0;
	for (long a = 0, b = 100; a < 3; a++)
		total += a + b;              /* 100+101+102 = 303 */
	printf("c=%ld\r\n", total);          /* 303 */

	/* (d) Two-pointer for-init still works after the factoring. */
	total = 0;
	{
		char s1[3];
		char s2[3];
		s1[0] = 'A'; s1[1] = 'B'; s1[2] = 0;
		s2[0] = 'C'; s2[1] = 'D'; s2[2] = 0;
		for (char *p = s1, *q = s2; *p; p++, q++)
			total += (long)*p + (long)*q;
	}
	/* ('A'+'C') + ('B'+'D') = 132 + 134 = 266 */
	printf("d=%ld\r\n", total);          /* 266 */

	/* (e) A for-init declarator collides with an earlier plain block
	 * local of a different type (the general sibling-scope case). */
	total = 0;
	{
		char k = 3;
		total += k;                  /* 3 */
	}
	for (long k = 0; k < 4; k++)
		total += k;                  /* 0+1+2+3 = 6 */
	printf("e=%ld\r\n", total);          /* 9 */

	return 0;
}
