/*
 * for_comma_inc_probe.c -- comma expression in a C99 for-init increment,
 * needed by py/bc.c and py/gc.c (MicroPython port; see NEXT_SESSION.md).
 *
 * The C99-declaration for-init rules used `exp0` (no comma) for the test
 * and increment clauses, while the plain `for` used `comma_exp0`.  So
 *   for (size_t i = n; i > 0; i--, ptrs++)
 * parse-errored on the `i--, ptrs++` increment.  Both C99 for rules (the
 * single declarator and the two-pointer-declarator form) now use
 * comma_exp0 for test/increment, matching the plain `for`.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/for_comma_inc_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/for_comma_inc_probe/for_comma_inc_probe.exe \
 *             | diff - minic/dos/tests/for_comma_inc_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

int
main(void)
{
	int arr[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
	int *p;
	int sum, steps;

	/* Single-declarator C99 for-init with a comma increment: walk a
	 * pointer forward while counting down an index (bc/gc spelling). */
	sum = 0;
	p = arr;
	for (int i = 8; i > 0; i--, p++)
		sum += *p;
	printf("sum=%d\r\n", sum);           /* 360 */

	/* Comma in the increment with two moving pointers via the
	 * two-pointer-declarator C99 for-init form. */
	steps = 0;
	for (int *a = arr, *b = arr + 7; a < b; a++, b--)
		steps++;
	printf("steps=%d\r\n", steps);       /* 4 */

	/* Comma in the increment, three side effects. */
	sum = 0;
	{
		int j = 0;
		int k = 100;
		for (int i = 0; i < 4; i++, j++, k--)
			sum += j + k;
	}
	printf("mix=%d\r\n", sum);           /* (0+100)+(1+99)+(2+98)+(3+97)=400 */
	return 0;
}
