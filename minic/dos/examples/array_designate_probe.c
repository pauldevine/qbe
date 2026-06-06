/*
 * array_designate_probe.c -- out-of-order designated array initializers
 * and a trailing comma in a brace-init list (MicroPython port; see
 * NEXT_SESSION.md).  Both surface in py/objtype.c.
 *
 * Two frontend fixes:
 *  1. agg_emit_array now buffers values into index-addressed slots, so a
 *     file-scope `[k] = v` designator may appear in any order — e.g.
 *     MicroPython's `[MP_BINARY_OP_ADD] = ...` jump tables, whose enum
 *     indices are not ascending (previously: "out-of-order array
 *     designator unsupported").  A positional item lands at the running
 *     cursor; a designator sets the cursor (C99 6.7.8); gaps zero-fill.
 *  2. A brace-init list may end with a trailing comma (`{ ..., }`),
 *     which a local designated-struct initializer in objtype.c uses.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/array_designate_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/array_designate_probe/array_designate_probe.exe \
 *             | diff - minic/dos/tests/array_designate_probe.golden.txt
 *
 * File-scope address-of stays near, so this is a medium-model probe.
 */

#include <stdio.h>

/* Out-of-order designated array init at file scope: index 3, then 1,
 * then 4 — with gaps (0 and 2) that must zero-fill. */
static const int tab[5] = {
	[3] = 30,
	[1] = 10,
	[4] = 40,
};

/* A designator followed by positional items: [2]=200 then 201, 202
 * occupy indices 2,3,4 (cursor advances from the designator). */
static const int mix[6] = {
	[2] = 200, 201, 202,
};

struct point { int x, y, z; };

int
main(void)
{
	int i;
	long sum = 0;

	for (i = 0; i < 5; i++)
		sum += tab[i];
	/* tab = { 0, 10, 0, 30, 40 } -> 80 */
	printf("tab=%ld t0=%d t2=%d\r\n", sum, tab[0], tab[2]);  /* 80 0 0 */

	sum = 0;
	for (i = 0; i < 6; i++)
		sum += mix[i];
	/* mix = { 0, 0, 200, 201, 202, 0 } -> 603 */
	printf("mix=%ld\r\n", sum);                              /* 603 */

	/* Trailing comma in a local designated-struct initializer. */
	{
		struct point p = { .x = 1, .y = 2, .z = 3, };
		printf("p=%d\r\n", p.x + p.y + p.z);             /* 6 */
	}
	return 0;
}
