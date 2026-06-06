/*
 * const_init_probe.c -- file-scope constant-expression scalar initializers
 * needed by py/parse.c (MicroPython port; see NEXT_SESSION.md).
 *
 * minic previously accepted only a bare NUM, (NUM), -NUM, or (-NUM) as a
 * file-scope scalar initializer.  py/parse.c writes
 *
 *     static const size_t FIRST_RULE_WITH_OFFSET_ABOVE_255 =
 *         PAD1_file_input >= 0x100 ? RULE_file_input :
 *         ...                                          // ~160 levels
 *         0;
 *
 * a long nested ternary chain over enum constants.  Two fixes:
 *   1. A `static T X = <const expr>;` rule folds the initializer via
 *      const_eval (subsumes the old NUM/(-NUM)/(NUM) rules, byte-identical
 *      output; STR and aggregate `{…}` inits stay on their own rules).
 *   2. The generated parser's StackSize was raised 500 -> 4000: a deep
 *      right-associative ternary chain cannot reduce until the whole chain
 *      is shifted, overflowing the old stack at ~120 levels.
 *
 * This probe exercises arithmetic / bitwise / shift / comparison / cast /
 * enum-constant folding and a moderately deep ternary chain.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/const_init_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/const_init_probe/const_init_probe.exe \
 *             | diff - minic/dos/tests/const_init_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

enum { A = 1, B = 2, C = 4, D = 0x200, E = 8 };

static const int g_arith = A + B * C;              /* 1 + 8 = 9 */
static const int g_bits  = (A | B | C) & ~B;       /* 7 & ~2 = 5 */
static const int g_shift = E << 2;                 /* 32 */
static const int g_cmp   = (D >= 0x100);           /* 1 */
static const int g_cast  = (int)((C << 3) | 1);    /* 33 */

/* A nested ternary chain (right-associative): picks the first arm whose
 * guard is >= 0x100.  Only D (0x200) qualifies, so result is D. */
static const int g_tern =
    A >= 0x100 ? A :
    B >= 0x100 ? B :
    C >= 0x100 ? C :
    D >= 0x100 ? D :
    E >= 0x100 ? E :
    0;                                             /* D = 512 */

int
main(void)
{
	printf("arith=%d\r\n", g_arith);   /* 9 */
	printf("bits=%d\r\n", g_bits);     /* 5 */
	printf("shift=%d\r\n", g_shift);   /* 32 */
	printf("cmp=%d\r\n", g_cmp);       /* 1 */
	printf("cast=%d\r\n", g_cast);     /* 33 */
	printf("tern=%d\r\n", g_tern);     /* 512 */
	return 0;
}
