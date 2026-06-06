/*
 * block_static_probe.c -- `static` declarations inside nested blocks
 * (MicroPython port, §1i; see NEXT_SESSION.md).
 *
 * minic only accepted `static` declarations in the function-top `dcls`
 * section; inside a nested `{ ... }` block (an if/else/loop body) the
 * statement grammar had no `static` variant, so
 *   const T *mp_obj_get_type(...) { ... else {
 *       static const mp_obj_type_t *const types[] = { 0, &mp_type_int, … };
 *       return types[i];
 *   } }
 * (py/obj.c) parse-errored.  Added statement-scope productions for
 * `static T v;`, `static T v = init;`, `static T a[] = {…};`,
 * `static T a[N] = {…};`, and `static T a[N];`, all lowered to mangled
 * file-scope data globals (persistent across calls), reusing the same
 * aggregate machinery as the function-top forms.
 *
 * Exercises runtime:
 *   1. static scalar with initializer in a nested block, persisting and
 *      incrementing across calls.
 *   2. static const pointer-table in a nested block, indexed (the obj.c
 *      idiom): items are 0 and &global.
 *   3. static scalar array in a nested block, written once then read.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/block_static_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/block_static_probe/block_static_probe.exe \
 *             | diff - minic/dos/tests/block_static_probe.golden.txt
 *
 * &global table items are near (medium) / would need far reloc under
 * far-data, so this probe is medium-only (matches mp_aggregate_probe).
 */

#include <stdio.h>

int g_a = 11;
int g_b = 22;

/* (1) static scalar in a nested block, persists across calls. */
int
counter(int go)
{
	if (go) {
		static int n = 100;
		return n++;
	}
	return -1;
}

/* (2) static const pointer table in a nested block (obj.c idiom). */
int *
pick(int i)
{
	if (i >= 0) {
		static int *const tbl[] = { 0, &g_a, &g_b, &g_a };
		return tbl[i & 3];
	}
	return 0;
}

int
main(void)
{
	int hist[4];
	int i;

	/* (1) persistence: 100, 101, 102. */
	printf("cnt=%d,%d,%d\r\n", counter(1), counter(1), counter(1));

	/* (2) table reads: tbl[0]=null, tbl[1]=&g_a(11), tbl[2]=&g_b(22). */
	printf("t0null=%d\r\n", pick(0) == 0);          /* 1 */
	printf("t1=%d t2=%d\r\n", *pick(1), *pick(2));   /* 11 22 */

	/* (3) static scalar array in a nested block. */
	{
		static int buf[4];
		for (i = 0; i < 4; i++)
			buf[i] = i * 10 + 3;
		for (i = 0; i < 4; i++)
			hist[i] = buf[i];
	}
	printf("buf=%d,%d,%d,%d\r\n", hist[0], hist[1], hist[2], hist[3]);
	/* 3,13,23,33 */

	return 0;
}
