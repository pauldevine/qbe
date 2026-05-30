/*
 * for_multidecl_probe.c -- two-pointer-declarator C99 for-init
 * (MicroPython port, §1i; see NEXT_SESSION.md).
 *
 * minic's for-loop declaration form accepted only one declarator
 * (`for (T v = e; ...)`).  The symmetric two-pointer form
 *   for (const byte *s = data, *top = data + len; s < top; s++)
 * (py/objstr.c, py/qstr.c) parse-errored.  Added a production for
 * `for (T *a = e1, *b = e2; test; inc) body` -- both declarators share
 * the pointer type (the first star is folded into the type-specifier),
 * inits run left-to-right via a comma node.
 *
 * Exercises runtime: walk a buffer with a start/end pointer pair from a
 * single for-init, accumulating a checksum and a count, and a second
 * loop computing a min over an int range with two cursors.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/for_multidecl_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/for_multidecl_probe/for_multidecl_probe.exe \
 *             | diff - minic/dos/tests/for_multidecl_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

typedef unsigned char byte;

int
sumwalk(const byte *data, int len)
{
	int sum = 0, count = 0;
	for (const byte *s = data, *top = data + len; s < top; s++) {
		sum += *s;
		count++;
	}
	return sum * 100 + count;
}

int
main(void)
{
	static const byte buf[5] = { 3, 1, 4, 1, 5 };
	int r = sumwalk(buf, 5);
	printf("sum=%d count=%d\r\n", r / 100, r % 100);   /* 14 5 */

	/* second pair-cursor loop, declarators in the for-init itself. */
	{
		int arr[6] = { 10, 2, 30, 4, 50, 6 };
		int best = 0;
		for (int *lo = arr, *hi = arr + 5; lo < hi; lo++) {
			int d = *hi - *lo;
			if (d < 0)
				d = -d;
			if (d > best)
				best = d;
			hi--;
		}
		printf("best=%d\r\n", best);                /* max|hi-lo| over closing ends -> 48 */
	}
	return 0;
}
