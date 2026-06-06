/*
 * void_ptr_cmp_probe.c -- pointer comparisons, including void* vs null
 * (MicroPython port, §1i; see NEXT_SESSION.md).
 *
 * minic's prom() fell through to its pointer-arithmetic "Scale" path for
 * comparison operators (==, !=, <, <=), computing SIZE(DREF(ptr)).  For
 * a `void *` (or any incomplete pointee) that is SIZE(void) -> fatal
 * "void has no size"; this blocked the ubiquitous MicroPython idiom
 *   void *ptr = gc_alloc(...);  if (ptr == 0 && n != 0) ...
 * (m_malloc in py/malloc.c).  The same Scale fall-through also wrongly
 * multiplied a comparison operand by the element size for pointer<->ptr
 * relational comparisons.  prom() now returns the pointer operand's type
 * directly for comparison ops and never scales.
 *
 * Exercises runtime, not just parse acceptance:
 *   1. void* == 0 / != 0 against a null and a non-null pointer.
 *   2. void* compared to another void* (equality both ways).
 *   3. pointer ordering (<, <=) over addresses within one array
 *      (well-defined) -- previously emitted a bogus scaling multiply.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/void_ptr_cmp_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/void_ptr_cmp_probe/void_ptr_cmp_probe.exe \
 *             | diff - minic/dos/tests/void_ptr_cmp_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

/* Return its argument unchanged, but opaque to the optimiser so the
 * caller can't constant-fold the null/non-null test away. */
void *
ident(void *p)
{
	return p;
}

int
main(void)
{
	int storage[4];
	void *nullp = ident(0);
	void *realp = ident(storage);
	void *same = realp;
	char *a = (char *)storage;
	char *b = a + 2;

	/* (1) void* vs null constant. */
	printf("null==0:%d null!=0:%d\r\n", nullp == 0, nullp != 0);  /* 1 0 */
	printf("real==0:%d real!=0:%d\r\n", realp == 0, realp != 0);  /* 0 1 */

	/* (2) void* vs void*. */
	printf("real==same:%d real==null:%d\r\n",
	       realp == same, realp == nullp);                        /* 1 0 */

	/* (3) pointer ordering over addresses (no scaling bug). */
	printf("a<b:%d b<a:%d a<=b:%d\r\n", a < b, b < a, a <= b);    /* 1 0 1 */

	return 0;
}
