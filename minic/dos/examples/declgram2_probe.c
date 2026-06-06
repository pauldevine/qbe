/*
 * declgram2_probe.c -- Phase 1 layer-2 declaration-grammar gate.
 *
 * Pins the declaration-grammar features added in the 2026-05-29 spike
 * follow-on (see NEXT_SESSION.md / MICROPYTHON_PORT.md).  These cleared
 * the entire MicroPython py/obj.h type/enum/struct grammar layer:
 *
 *   1. Canonical trailing-`int` integer specifiers (`long long int`,
 *      `unsigned long long int`, `long int`) and the `signed` keyword.
 *   2. enum with a constant-EXPRESSION initializer that references a
 *      prior enumerator (`BLUE = RED | GREEN`), folded by const_eval,
 *      plus a trailing comma in the enumerator list.
 *   3. `enum Tag` used as a type-specifier (param / return / variable).
 *   4. const-expression bitfield widths (`unsigned lo : (2 + 2)`).
 *   5. const-expression / parenthesized struct-array-member dimensions
 *      (`int regs[((3))]`).
 *
 * (`const void *` / `volatile void` and incomplete-struct forward decls
 * are also part of this layer but are parse-only — pinned by the spike,
 * not exercised here.  `void *` comparison hits a pre-existing void-
 * pointer-arithmetic limitation unrelated to this work.)
 *
 * These are grammar features but also exercise codegen: enum constant
 * folding, struct layout (bitfield + array sizing), and a real call
 * through an `enum Tag` prototype.  Runtime probe, not just a parse check.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/declgram2_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/declgram2_probe/declgram2_probe.exe \
 *             | diff - minic/dos/tests/declgram2_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

/* Feature 2: enum const-expr initializers (with prior refs) + trailing comma */
enum colors {
	RED = 1,
	GREEN = RED << 1,
	BLUE = RED | GREEN,
	NEXT = BLUE * 4,
};

/* Feature 3: enum Tag as a type-specifier (param + return) */
enum colors echo_color(enum colors c) { return c; }

/* Feature 4 + 5: const-expr bitfield widths and a parenthesized array dim */
struct packed {
	unsigned lo : (2 + 2);   /* 4-bit field, holds 0..15 */
	unsigned hi : 1;
	int regs[((3))];         /* 3 ints */
};

int main()
{
	struct packed pk;
	enum colors cc;
	signed char sc;            /* Feature 1: signed keyword */
	long long int ll;          /* Feature 1: long long int (32-bit here) */
	int sum;

	/* Feature 2: constant folding with references to prior enumerators. */
	printf("enum=%d,%d,%d,%d (want 1,2,3,12)\r\n", RED, GREEN, BLUE, NEXT);

	/* Feature 3: call through an enum-Tag-typed prototype. */
	cc = echo_color(BLUE);
	printf("tag=%d (want 3)\r\n", cc);

	/* Feature 4: the (2+2)-wide bitfield holds 15; hi holds 1. */
	pk.lo = 15;
	pk.hi = 1;
	printf("bf=%d,%d (want 15,1)\r\n", pk.lo, pk.hi);

	/* Feature 5: regs has 3 addressable ints (the [((3))] dim). */
	pk.regs[0] = 10;
	pk.regs[1] = 20;
	pk.regs[2] = 30;
	sum = pk.regs[0] + pk.regs[1] + pk.regs[2];
	printf("regs=%d (want 60)\r\n", sum);

	/* Feature 1: signed char keeps its sign. */
	sc = -5;
	printf("sc=%d (want -5)\r\n", sc);

	/* Feature 1: long long int holds a >16-bit value (32-bit type). */
	ll = 100000;
	printf("ll=%d (want 10000)\r\n", (int)(ll / 10));

	return 0;
}
