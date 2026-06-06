/*
 * comma_probe.c -- Phase 1 comma-operator gate.
 *
 * Pins the binary comma-operator support added 2026-05-29
 * (see NEXT_SESSION.md Layer 2).  Before this, minic's parenthesized
 * expression grammar used `expr` (no comma operator), so the
 * m_malloc/m_free/m_del family — which expands to parenthesized comma
 * expressions used as statements, e.g. `((void)(n), m_free(p));` —
 * failed to parse across the MicroPython core.
 *
 * The codegen for a ',' node already existed (evaluate left for side
 * effects, value is the right operand); only the grammar was missing.
 * This probe verifies the runtime SEMANTICS: left operand is evaluated
 * (its side effects happen) and the value of the whole expression is
 * the rightmost operand.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/comma_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/comma_probe/comma_probe.exe \
 *             | diff - minic/dos/tests/comma_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

static int g;

/* The exact MicroPython m_free shape: cast-to-void then a call, as a
 * parenthesized comma expression used as a statement. */
static void fake_free(void *p)
{
	g = (int)((long)p & 0xff);
}

static void m_free_shim(void *p, int n)
{
	((void)(n), fake_free(p));
}

int main()
{
	int a, b, x;
	int dummy;

	/* Feature 1: value of a comma expression is the rightmost operand,
	 * and every left operand is evaluated for its side effects. */
	a = 1;
	b = 2;
	x = (a++, b++, a + b);   /* a->2, b->3, value = 2+3 = 5 */
	printf("x=%d a=%d b=%d (want 5 2 3)\r\n", x, a, b);

	/* Feature 2: the m_free-style statement form parses and runs; the
 	 * left `(void)(n)` is a no-op, the right call fires. */
	g = 0;
	dummy = 0x77;
	m_free_shim(&dummy, 5);
	printf("freed=%d (want nonzero)\r\n", g != 0);

	/* Feature 3: comma operator inside a function-call argument's parens
	 * (the comma there is the operator, not an arg separator). */
	g = 7;
	printf("nested=%d (want 9)\r\n", (g = g + 1, g + 1));

	return 0;
}
