/*
 * softfloat_probe.c — end-to-end single-precision software-float probe.
 *
 * Exercises the i8086 backend's lowering of the native `float` (Ks) type to
 * the soft-float helper calls (_sf_add/_sf_sub/_sf_mul/_sf_div/_sf_cmp/
 * _sf_from_int/_sf_to_int) provided by minic/dos/softfloat.c.  The Victor
 * has no 8087, so every float op MUST go through these helpers — this probe
 * is the runtime proof that arithmetic, comparison, and int<->float
 * conversion all produce the correct IEEE-754 binary32 results.
 *
 * Build:  tools/build-example.sh --softfloat --model=medium \
 *             minic/dos/examples/softfloat_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/softfloat_probe/softfloat_probe.exe \
 *             | diff - minic/dos/tests/softfloat_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (MEDIUM only) with --softfloat.  Far-data
 * models (compact/large/huge) are NOT covered yet: minic's far load/store
 * (loadfw/storefw) truncates a float (Ks) through a far pointer to 16 bits,
 * so far-data single-precision float is a deferred follow-up (the
 * [[storefar-lacks-storefl]] family extended to Ks).  The soft-float
 * backend lowering itself (this milestone) is model-independent.
 *
 * IMPORTANT — what this probe deliberately AVOIDS (current minic gaps,
 * documented in NEXT_SESSION.md):
 *   - unary minus on a float (`-x`): minic desugars it to `0.0 - x` in
 *     double, also Kd.  Negation is exercised via subtraction instead.
 * Both stay single-precision; the soft-float path is what's under test.
 *
 * NOTE: `f`-suffixed float LITERALS (`1.5f`) are now typed single-precision
 * (Ks) by minic — see minic/dos/examples/float_literal_probe.c.  This probe
 * still uses bit patterns so it can exercise exact IEEE-754 operands (e.g.
 * 0.1) that no short decimal literal can express.
 */
#include <stdio.h>

/* Reinterpret a 32-bit pattern as float and back without a float literal.
 * On DOS, `unsigned long` is exactly 32 bits (matching a float). */
union fb { float f; unsigned long u; };

static float bits(unsigned long u) { union fb x; x.u = u; return x.f; }
static unsigned long raw(float f)  { union fb x; x.f = f; return x.u; }

int
main(void)
{
	float f1  = bits(0x3f800000UL);   /* 1.0  */
	float f2  = bits(0x40000000UL);   /* 2.0  */
	float f3  = bits(0x40400000UL);   /* 3.0  */
	float f10 = bits(0x41200000UL);   /* 10.0 */
	float f01 = bits(0x3dcccccdUL);   /* 0.1  */
	float f02 = bits(0x3e4ccccdUL);   /* 0.2  */
	float fneg7 = bits(0xc0e00000UL); /* -7.0 */

	/* Arithmetic — print the result's 32-bit bit pattern. */
	printf("add_1_2 %08lx\r\n",   raw(f1 + f2));    /* 40400000  (3.0)  */
	printf("add_01_02 %08lx\r\n", raw(f01 + f02));  /* 3e99999a  (0.3)  */
	printf("sub_3_1 %08lx\r\n",   raw(f3 - f1));    /* 40000000  (2.0)  */
	printf("sub_1_3 %08lx\r\n",   raw(f1 - f3));    /* c0000000  (-2.0) */
	printf("mul_01_10 %08lx\r\n", raw(f01 * f10));  /* 3f800000  (1.0)  */
	printf("mul_3_3 %08lx\r\n",   raw(f3 * f3));    /* 41100000  (9.0)  */
	printf("div_3_2 %08lx\r\n",   raw(f3 / f2));    /* 3fc00000  (1.5)  */
	printf("div_1_10 %08lx\r\n",  raw(f1 / f10));   /* 3dcccccd  (0.1)  */
	printf("addneg %08lx\r\n",    raw(f3 + fneg7)); /* c0800000  (-4.0) */

	/* int -> float (signed + unsigned), float -> int. */
	printf("cvt_5 %08lx\r\n",   raw((float)5));        /* 40a00000 (5.0)  */
	printf("cvt_neg3 %08lx\r\n", raw((float)(-3)));    /* c0400000 (-3.0) */
	printf("cvtu_40000 %08lx\r\n", raw((float)40000U));/* 471c4000 (40000)*/
	printf("toi_3 %d\r\n",   (int)f3);                 /* 3  */
	printf("toi_neg7 %d\r\n", (int)fneg7);             /* -7 */

	/* Comparisons. */
	printf("lt_1_2 %d\r\n", (f1 < f2));   /* 1 */
	printf("lt_2_1 %d\r\n", (f2 < f1));   /* 0 */
	printf("le_2_2 %d\r\n", (f2 <= f2));  /* 1 */
	printf("gt_3_2 %d\r\n", (f3 > f2));   /* 1 */
	printf("ge_2_3 %d\r\n", (f2 >= f3));  /* 0 */
	printf("eq_2_2 %d\r\n", (f2 == f2));  /* 1 */
	printf("ne_1_2 %d\r\n", (f1 != f2));  /* 1 */
	printf("lt_neg_pos %d\r\n", (fneg7 < f1)); /* 1 */
	return 0;
}
