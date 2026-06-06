/*
 * for_multiscalar_probe.c -- multi-scalar-declarator C99 for-init, needed
 * by py/gc.c (MicroPython port; see NEXT_SESSION.md).
 *
 * The C99-declaration for-init rules handled a single declarator and the
 * two-pointer `for (T *a=e1, *b=e2; ...)` form (for_multidecl_probe.c),
 * but not several scalar declarators sharing one base type:
 *   for (size_t block = 0, len = 0, len_free = 0; !finish; ...)
 * A new rule allocates each declarator and chains the initializers into
 * one comma-expression run at loop entry.  Distinguished from the
 * two-pointer form by the token after the first comma (IDENT vs `*`).
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/for_multiscalar_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/for_multiscalar_probe/for_multiscalar_probe.exe \
 *             | diff - minic/dos/tests/for_multiscalar_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

int
main(void)
{
	long sum;
	int n;

	/* Three scalar declarators, all initialized, sharing `long`; each
	 * advances independently in the comma increment, so all three slots
	 * must be distinct and live (the gc.c spelling). */
	sum = 0;
	n = 0;
	for (long block = 0, len = 10, len_free = 100; block < 5;
	     block++, len += 2, len_free -= 5) {
		sum += len + len_free;
		n++;
	}
	/* len:      10,12,14,16,18
	 * len_free: 100,95,90,85,80
	 * pair sums: 110,107,104,101,98 -> 520 */
	printf("sum=%ld n=%d\r\n", sum, n);   /* sum=520 n=5 */

	/* Mixed: a declarator without an initializer (assigned in the body
	 * before use) between two initialized ones. */
	sum = 0;
	for (int i = 0, acc, step = 3; i < 4; i++) {
		acc = i * step;
		sum += acc;
	}
	printf("acc=%ld\r\n", sum);            /* 0+3+6+9 = 18 */

	return 0;
}
