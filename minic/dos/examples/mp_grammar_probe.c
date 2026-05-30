/*
 * mp_grammar_probe.c -- frontend grammar features added for the
 * MicroPython port (2026-05-29, §1h; see NEXT_SESSION.md /
 * MICROPYTHON_PORT.md).  Each exercises codegen + runtime, not just
 * parse acceptance:
 *
 *   1. Constant-expression local array dimensions -- `T name[expr]`
 *      where expr folds via const_eval (sizeof / arithmetic).  Was
 *      restricted to a bare integer literal.  MicroPython spells e.g.
 *      `byte buf[(((sizeof(mp_uint_t)) * 8 + 6) / 7)]`.
 *
 *   1b. sizeof(arrayvar) -- varh now records each array declarator's
 *      total byte size, so sizeof(buf) returns the whole-array size
 *      (was the pointer-to-element size, a silent miscompile for the
 *      `p = buf + sizeof(buf)` idiom MicroPython relies on).  Covers
 *      local arrays, file-scope scalar arrays, and struct arrays.
 *
 *   2. Adjacent string-literal concatenation -- `"a" "b" "c"` lexes as
 *      one string.  MicroPython's mp_printf format strings are built
 *      this way (`"range(" "%d" ")"`).
 *
 *   3. `static __attribute__((noreturn)) T f(...)` -- attribute spelled
 *      AFTER `static` (was only accepted before it).  Parse-only, but
 *      the function still runs.
 *
 *   4. `_Static_assert(general-const-expr, "msg")` -- the condition is
 *      now a folded constant expression (was a bare NUM).  Compile-time
 *      only, no runtime effect; included here so the file exercises the
 *      grammar path.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/mp_grammar_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/mp_grammar_probe/mp_grammar_probe.exe \
 *             | diff - minic/dos/tests/mp_grammar_probe.golden.txt
 *
 * Wired into tools/test-dos.sh.  These are all near/far-agnostic
 * frontend features, so the probe runs under medium + large.
 */

#include <stdio.h>

typedef unsigned long mp_uint_t;

/* (4) general const-expr _Static_assert (arithmetic, folds true). */
_Static_assert(((0x10) + 0x0b) + (0) == ((0x10) + 0x0b), "arith assert");

/* (3) attribute after static. */
static __attribute__((noreturn)) void boom(void)
{
	for (;;) {
	}
}

/* (1b) file-scope arrays whose whole-array sizeof must be tracked.
 * i8086: int is 2 bytes. */
static const int tbl[] = { 10, 20, 30, 40, 50 };  /* 5*2 = 10 bytes */
struct e { int k; int v; };
static const struct e et[] = { {1, 2}, {3, 4}, {5, 6} };  /* 3*4 = 12 */

int
main(void)
{
	/* (1) const-expr local array dims.  i8086: long is 4 bytes,
	 * int is 2 bytes. */
	unsigned char buf[(((sizeof(mp_uint_t)) * 8 + 6) / 7)];  /* 5 */
	char wide[sizeof(long) * 2];                              /* 8 */
	int squares[2 * 3];                                       /* 6 ints */
	int i;

	(void)boom;  /* reference it so it is not dead-stripped away */

	/* (1b) whole-array sizeof. */
	printf("dim_buf=%d (want 5)\r\n", (int)sizeof(buf));
	printf("dim_wide=%d (want 8)\r\n", (int)sizeof(wide));
	printf("dim_sq=%d (want 12)\r\n", (int)sizeof(squares));
	printf("sz_tbl=%d (want 10)\r\n", (int)sizeof(tbl));
	printf("sz_et=%d (want 12)\r\n", (int)sizeof(et));

	for (i = 0; i < 6; i++)
		squares[i] = i * i;
	printf("sq5=%d (want 25)\r\n", squares[5]);

	buf[0] = 0x41;
	buf[4] = 0x45;
	printf("buf0=%d buf4=%d (want 65 69)\r\n", buf[0], buf[4]);

	/* (2) adjacent string-literal concatenation. */
	printf("concat=[" "%d" "," "%d" "]\r\n", 1, 2);
	printf("range(" "%d" ", " "%d" ")\r\n", 10, 20);

	return 0;
}
