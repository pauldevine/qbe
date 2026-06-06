/*
 * oo_designate_probe.c -- out-of-order designated struct initializers +
 * attribute-before-static function definitions (MicroPython port, §3c;
 * see NEXT_SESSION.md).  Both are minic frontend fixes needed to enable
 * MICROPY_PY_BUILTINS_SLICE: objslice.c's `mp_type_slice` lists its
 * `.slot_index_*` members out of declaration order, and vm.c's slice
 * helper is spelled `MP_NOINLINE static mp_obj_t *build_slice_…(…)`
 * (i.e. __attribute__ BEFORE the storage class).
 *
 *   1. File-scope struct whose designators appear in a DIFFERENT order
 *      than the member declarations (the mp_obj_type_t shape).  The old
 *      single-pass emitter die()d on the first backwards offset
 *      ("out-of-order designated initializer unsupported"); the two-pass
 *      emitter buffers values into member-indexed slots and emits in
 *      declaration order.
 *
 *   2. Out-of-order designators over a bitfield run sharing one storage
 *      unit (.z,.x,.y of an x:4/y:4/z:8 word) plus a trailing scalar.
 *
 *   3. Partial out-of-order init (only some members set, gaps zeroed).
 *
 *   4. `__attribute__((noinline)) static T *f(...)` -- attribute before
 *      `static`, returning a pointer.  Only `static __attribute__` and
 *      bare `__attribute__` were handled before; the mirror rule is new.
 *
 * Layout is model-agnostic (no pointers in the structs, so sizes match
 * across medium/large): unsigned char = 1, unsigned short = 2,
 * int = 2 bytes on i8086.  Frontend-only, near/far-agnostic.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/oo_designate_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/oo_designate_probe/oo_designate_probe.exe \
 *             | diff - minic/dos/tests/oo_designate_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under compact + large.
 */

#include <stdio.h>

/* (1) members declared a..s; designators given OUT of declaration order. */
struct slots {
	unsigned char  a;   /* off 0 */
	unsigned char  b;   /* off 1 */
	unsigned short n;   /* off 2 */
	unsigned char  p;   /* off 4 */
	unsigned char  u;   /* off 5 */
	unsigned short s;   /* off 6 */
};
static const struct slots g = {
	.a = 1, .s = 6, .u = 5, .p = 4, .n = 3, .b = 2,
};

/* (2) out-of-order designators over a packed bitfield unit + a tail. */
struct bits { unsigned x:4; unsigned y:4; unsigned z:8; int tail; };
static const struct bits gb = { .z = 7, .x = 1, .y = 2, .tail = 99 };

/* (3) partial out-of-order: only .c and .a set, .b zero-filled. */
struct three { int a; int b; int c; };
static const struct three gp = { .c = 30, .a = 10 };

/* (4) __attribute__ BEFORE static, pointer return. */
__attribute__((noinline)) static int *
pick(int *arr, int i)
{
	return &arr[i];
}

int
main(void)
{
	int buf[4];
	int *q;

	buf[0] = 100; buf[1] = 200; buf[2] = 300; buf[3] = 400;

	printf("g=%d,%d,%d,%d,%d,%d\r\n", g.a, g.b, g.n, g.p, g.u, g.s);
	/* want: 1,2,3,4,5,6 */
	printf("gb=%d,%d,%d,%d\r\n", gb.x, gb.y, gb.z, gb.tail);
	/* want: 1,2,7,99 */
	printf("gp=%d,%d,%d\r\n", gp.a, gp.b, gp.c);
	/* want: 10,0,30 */

	q = pick(buf, 2);
	printf("pick=%d\r\n", *q);   /* want: 300 */

	return 0;
}
