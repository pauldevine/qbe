/*
 * scalar_array_probe.c -- scalar array initializers with constant
 * expression items, plus offsetof().
 *
 * Pins the grammar/runtime work added 2026-05-29 (see NEXT_SESSION.md /
 * MICROPYTHON_PORT.md):
 *
 *   (a) `T NAME[] = { ... }` where T is a scalar (integer/char) element
 *       type now emits a real data block (emit_scalar_array_data); the
 *       rule used to die("array initializer requires struct or pointer
 *       type").  This is what MicroPython's `py/unicode.c`
 *       `static const uint8_t attr[] = { ((0x02)|(0x01)), ... };` needs.
 *
 *   (b) brace-list items are now full constant expressions folded by
 *       const_eval (sai_item: expr), so `(a) | (b) | (c)`, shifts, etc.
 *       are accepted -- not just bare NUM / -NUM / STR.
 *
 *   (c) offsetof(type, member) is now defined in <stddef.h> as the
 *       classic `((size_t)&((type *)0)->member)` form, which MiniC folds
 *       to the member's byte offset.  MicroPython's flexible-array
 *       object structs (`m_new_obj_var`) rely on it.
 *
 * Exercises (codegen + runtime, not just parse):
 *   1. uint8_t array with OR-of-flags initializers, indexed at runtime.
 *   2. uint16_t (half-word) array with shift/OR initializers.
 *   3. long (word-pair) array with arithmetic initializers.
 *   4. offsetof() on a struct with a trailing flexible array member.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/scalar_array_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/scalar_array_probe/scalar_array_probe.exe \
 *             | diff - minic/dos/tests/scalar_array_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (medium + large).
 */

#include <stdio.h>
#include <stddef.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

/* uint8_t array, items are OR-of-flags constant expressions. */
static const uint8_t flags[] = {
	((0x02) | (0x01)),          /* 3  */
	((0x04) | (0x01) | (0x40)), /* 69 */
	(0x10) | (0x08) | (0x01),   /* 25 */
	0,
};

/* uint16_t array, items use shifts. */
static const uint16_t shifts[] = {
	(1 << 0) | (1 << 4),   /* 17  */
	(1 << 8) | (1 << 1),   /* 258 */
	(3 << 4),              /* 48  */
};

/* long array, items use arithmetic. */
static const long sums[] = {
	1000 + 234,            /* 1234 */
	2 * 3 * 100,           /* 600  */
	(0x100 | 0x20),        /* 288  */
};

/* int array exercising the const_eval extensions added alongside this
 * work: cast (identity fold), ternary, comparison and logical folding. */
static const int folded[] = {
	(int)(3 + 4),                      /* cast fold -> 7        */
	(1 < 2) ? 10 : 20,                 /* ternary + < -> 10     */
	(5 == 5) ? (3 << 2) : 0,           /* == + shift -> 12      */
	(0 || 1) ? 100 : 0,                /* logical-or -> 100     */
};

/* struct with a trailing flexible array member, for offsetof. */
struct vstruct {
	int a;
	long b;
	char items[];
};

int
main(void)
{
	int i;
	long t;

	t = 0;
	for (i = 0; i < 4; i++)
		t += flags[i];
	printf("flags=%ld (want 97)\r\n", t);

	t = 0;
	for (i = 0; i < 3; i++)
		t += shifts[i];
	printf("shifts=%ld (want 323)\r\n", t);

	t = 0;
	for (i = 0; i < 3; i++)
		t += sums[i];
	printf("sums=%ld (want 2122)\r\n", t);

	t = 0;
	for (i = 0; i < 4; i++)
		t += folded[i];
	printf("folded=%ld (want 129)\r\n", t);

	/* offsetof: layout-dependent (§4g far-data 4-aligns the 4-byte `long b`
	 * to offset 4; medium/NEAR_DATA packs it at 2), so assert the
	 * model-independent RELATIONSHIPS rather than packed magic numbers:
	 * a is first, b follows a, and items immediately follows the long b. */
	printf("offsetof %s\r\n",
	    (offsetof(struct vstruct, a) == 0
	     && offsetof(struct vstruct, b) >= sizeof(int)
	     && offsetof(struct vstruct, items)
	        == offsetof(struct vstruct, b) + sizeof(long)) ? "ok" : "FAIL");

	return 0;
}
