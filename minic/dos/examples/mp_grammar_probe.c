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
 *   5. Local aggregate initializer -- `struct P p = { 1, 2 };` (and the
 *      `.field =` designated form) desugars to a compound-literal
 *      assignment.  MicroPython spells `mp_print_t print = { vstr, fn };`.
 *
 *   6. File-scope designated array initializer -- `static const T t[] =
 *      { [IDX] = v, … }` places by index (sai_designate zero-fills
 *      gaps).  MicroPython spells `scope_simple_name_table[]` this way.
 *
 *   7. Sized file-scope array initializer -- `static const T t[N] =
 *      { … }` with fewer items than N (trailing elements zero-filled).
 *      MicroPython spells `static const char pad_spaces[16] = {…}`.
 *
 *   8. sizeof(expr) -- general expression operand (was type / ident /
 *      type[expr] only).  Evaluated via typeof_expr (emit-and-discard).
 *      Unblocks the MicroPython count idiom
 *      `sizeof(arr) / sizeof(arr[0])` and `sizeof(*ptr)`.
 *
 *   9. Local unsized array initializer -- `T a[] = { x, y };` (count
 *      inferred), block-scoped sized/unsized array init with deferred
 *      stores, and arithmetic initializer items (`{ x, x*2 }`; inititem
 *      widened from unary to full expr).
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

/* (6) file-scope designated array init, in source order with a gap at
 * SL_B (zero-filled by sai_designate). */
enum { SL_A, SL_B, SL_C, SL_D };
static const int da[] = { [SL_A] = 11, [SL_C] = 33 };  /* {11,0,33} */

/* (7) sized file-scope array, fewer items than the declared count. */
static const char sized[8] = { 'a', 'b', 'c' };  /* {a,b,c,0,0,0,0,0} */

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

	/* (5) local aggregate initializer (positional + designated). */
	{
		struct e lp = { 100, 200 };
		struct e ld = { .v = 9, .k = 8 };
		printf("lp=%d,%d (want 100 200)\r\n", lp.k, lp.v);
		printf("ld=%d,%d (want 8 9)\r\n", ld.k, ld.v);
	}

	/* (6) designated file-scope array, read back including the gap. */
	printf("da=%d,%d,%d (want 11 0 33)\r\n", da[SL_A], da[SL_B], da[SL_C]);

	/* (7) sized file-scope array, declared length 8 with 3 items. */
	printf("sized=%d,%d,%d,%d,%d (want 97 98 99 0 0)\r\n",
	       sized[0], sized[1], sized[2], sized[3], sized[7]);

	/* (8) sizeof(expr): count idiom + element + deref.  i8086 int=2.
	 * NB: each count is printed in its own call — two `/` divisions
	 * feeding one call hit a pre-existing i8086 div AX/DX clobber bug
	 * (unrelated to sizeof; see [[i8086-two-div-one-call-clobber]]). */
	{
		int *ip = squares;
		printf("cnt_tbl=%d (want 5)\r\n",
		       (int)(sizeof(tbl) / sizeof(tbl[0])));
		printf("cnt_sq=%d (want 6)\r\n",
		       (int)(sizeof(squares) / sizeof(squares[0])));
		printf("elem=%d deref=%d (want 2 2)\r\n",
		       (int)sizeof(squares[0]), (int)sizeof(*ip));
	}

	/* (9) local unsized array, block-scoped array init, arithmetic
	 * initializer items. */
	{
		int seed = 5;
		int un[] = { seed, seed * 2, seed * 3 };  /* inferred [3] */
		printf("un=%d,%d,%d n=%d (5 10 15 6)\r\n",
		       un[0], un[1], un[2], (int)sizeof(un));
		if (seed > 0) {
			int blk[4] = { seed };  /* block-scoped, zero tail */
			printf("blk=%d,%d (5 0)\r\n", blk[0], blk[3]);
		}
	}

	return 0;
}
