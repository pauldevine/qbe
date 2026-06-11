/*
 * kl_ternary_mul_probe.c -- two i8086 32-bit (Kl) codegen fixes (§3p;
 * see NEXT_SESSION.md / build/sf-spike/SPIKE_FINDINGS.md).  Both were
 * found while bringing up software floating point (the first heavy Kl
 * consumer on the no-8087 Victor) and corrupt dense 32-bit arithmetic:
 *
 *   1. TERNARY WIDTH TRUNCATION (minic.y `case '?'`).  `cond ? A : B`
 *      where one arm is 32-bit (`unsigned long`) and the other is a
 *      16-bit `int` (e.g. the literal 0) used to emit a `=w` phi (the
 *      old type-unification only recognised the EXACT signed INT/LNG
 *      pair, not `unsigned long` = LNG|UNSIGNED), truncating the 32-bit
 *      arm to its low word.  This made e.g. soft-float's
 *      `ma = (e==0) ? 0 : (frac | 0x800000)` evaluate to 0.  The phi
 *      (and both arms) now take the wider arm's IL width.
 *
 *   2. Kl MULTIPLY (i8086/emit.c Omul).  The handler did a single
 *      16x16 `imul` of the operands' LOW words, which sign-extended a
 *      zero-extended (`extuw`) operand whose low word had bit 15 set
 *      (e.g. `(unsigned long)0xCCCD * x`), corrupting the high result
 *      word.  Now a proper 32x32->32 multiply via unsigned partials,
 *      correct for both sign- and zero-extended operands.
 *
 * Pure runtime check (values chosen so a wrong result is loud).  All
 * arithmetic is `unsigned long` (4 bytes on i8086) plus one signed case
 * to guard against a sign-handling regression in the new multiply.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/kl_ternary_mul_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/kl_ternary_mul_probe/kl_ternary_mul_probe.exe \
 *             | diff - minic/dos/tests/kl_ternary_mul_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under compact + large.
 */

#include <stdio.h>

typedef unsigned long U32;

/* Defeat constant folding: the compiler can't assume what id() returns. */
static U32 id(U32 x) { return x; }

int
main(void)
{
	U32 zero = id(0);
	U32 one  = id(1);

	/* (1) ternary with a 32-bit arm and a 16-bit literal arm.  The
	 * 32-bit value must survive (low word is 0, so truncation would
	 * read 0 here too -- pick a value with a NONZERO low word AND a
	 * nonzero high word so a `=w` phi is loud). */
	U32 big = (U32)0x00ABCDEF;
	U32 t1 = (zero == 0) ? big : (U32)7;   /* -> 0x00ABCDEF */
	U32 t2 = (one == 0)  ? (U32)7 : big;   /* -> 0x00ABCDEF (other arm) */

	/* (2) Kl multiply of zero-extended 16-bit operands; the multiplier
	 * 0xCCCD has bit 15 set, so the old low-word `imul` sign-extended
	 * it and produced 0xFFE00020 instead of 0x00800020. */
	U32 a = id((U32)0xCCCD);
	U32 b = id((U32)0x00A0);
	U32 prod = a * b;                      /* -> 0x00800020 */

	/* full 32-bit operands (exercise the cross terms a_lo*b_hi etc.) */
	U32 c = id((U32)0x00012345);
	U32 d = id((U32)0x00000010);
	U32 prod2 = c * d;                     /* -> 0x00123450 */

	/* signed long multiply must still be correct (sign-extended op). */
	long sp = id((U32)(long)-3) * (long)id(5);  /* -> -15 */

	printf("t1=%08lx\r\n", (unsigned long)t1);
	printf("t2=%08lx\r\n", (unsigned long)t2);
	printf("prod=%08lx\r\n", (unsigned long)prod);
	printf("prod2=%08lx\r\n", (unsigned long)prod2);
	printf("sp=%ld\r\n", sp);
	return 0;
}
