/*
 * ptrdiff_probe.c -- near-pointer (ptrdiff_t) subtraction.
 *
 * BUG-LOUD: on the unfixed toolchain this file does not produce a wrong
 * answer -- it CRASHES THE COMPILER.  minic's prom() typed EVERY pointer
 * difference as LNG (32-bit), even for a near pointer.  In a near-data model
 * (tiny/small/medium) a near char* is a 16-bit value, so `p - base` emitted
 * `%t =l sub %tw1, %tw2` -- a 32-bit subtract of two 16-bit (`w`) operands.
 * That class-inconsistent IR tripped QBE gvn `assoccon`'s width assert
 * (gvn.c:210, `KWIDE(i2->cls) >= KWIDE(i1->cls)`) once the loads were
 * forwarded, SIGABRTing the `qbe -t i8086 -m medium` step (Abort trap 6).
 *
 * Two fixes, both gated by this probe:
 *   1. minic prom(): near ptrdiff is INT (Kw, 16-bit), not LNG -- mirrors
 *      the far-aware second '-' handler (ISFAR ? LNG : INT).
 *   2. QBE gvn assoccon(): bail (don't fold) instead of asserting when the
 *      inner def is narrower than the outer op -- a backend must never
 *      SIGABRT on width-mismatched IR.
 *
 * Output is model-independent: sizeof(int)==2 on every model here, so byte
 * differences via (char*) and element counts via typed pointers are fixed.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/ptrdiff_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/ptrdiff_probe/ptrdiff_probe.exe \
 *             | diff - minic/dos/tests/ptrdiff_probe.golden.txt
 */
#include <stdio.h>

static int a[6];
static char buf[10];

struct point { int x; int y; };
static struct point pts[4];

int
main(void)
{
	int i;
	char *base, *p;
	int *ip, *iq;
	struct point *sp, *sq;

	/* The original crash form: (char*) element-address difference in a
	 * loop -- byte offset of a[i] from a[0] (stride sizeof(int)==2). */
	base = (char *)&a[0];
	for (i = 0; i < 6; i++) {
		p = (char *)&a[i];
		printf("byte %d %d\n", i, (int)(p - base));
	}

	/* char-array byte differences. */
	printf("char %d\n", (int)(&buf[7] - &buf[2]));

	/* Typed int* difference -> ELEMENT count, not bytes. */
	ip = &a[1];
	iq = &a[5];
	printf("elem %d\n", (int)(iq - ip));

	/* Struct* difference -> element count (sizeof(struct point)==4). */
	sp = &pts[0];
	sq = &pts[3];
	printf("struct %d\n", (int)(sq - sp));
	printf("structbytes %d\n", (int)((char *)sq - (char *)sp));

	printf("done\n");
	return 0;
}
