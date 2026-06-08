/*
 * float_literal_probe.c — single-precision FLOAT LITERAL (`1.5f`) probe.
 *
 * Complements softfloat_probe.c.  That probe deliberately avoids float
 * literals because minic used to type every floating-point literal as
 * `double` (Kd), which the i8086 soft-float backend does not implement
 * (it die()s).  minic now types an `f`/`F`-suffixed literal as single
 * precision (Ks), so `x + 1.5f` lowers to a real `_sf_add` far call.
 *
 * This probe exercises float literals in arithmetic, comparison, and
 * initialization.  Each arithmetic case combines a literal with a RUNTIME
 * float (built from a bit pattern through a union) so QBE cannot fold the
 * expression to a constant — the literal must survive as a true Ks operand
 * feeding an _sf_* helper.  Were the literal mis-typed as double, the build
 * would die in the i8086 backend, so merely running proves Ks typing.
 *
 * Build:  tools/build-example.sh --softfloat --model=medium \
 *             minic/dos/examples/float_literal_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/float_literal_probe/float_literal_probe.exe \
 *             | diff - minic/dos/tests/float_literal_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (MEDIUM only, --softfloat) like softfloat_probe.
 */
#include <stdio.h>

union fb { float f; unsigned long u; };

static float bits(unsigned long u) { union fb x; x.u = u; return x.f; }
static unsigned long raw(float f)  { union fb x; x.f = f; return x.u; }

int
main(void)
{
	/* Runtime floats (opaque to the folder) combined with literals. */
	float a = bits(0x40000000UL);   /* 2.0 */
	float b = bits(0x40400000UL);   /* 3.0 */

	/* Literal arithmetic against a runtime operand -> real _sf_* calls. */
	printf("add_2_1p5 %08lx\r\n", raw(a + 1.5f));   /* 40600000 (3.5)  */
	printf("sub_3_0p5 %08lx\r\n", raw(b - 0.5f));   /* 40200000 (2.5)  */
	printf("mul_2_2p5 %08lx\r\n", raw(a * 2.5f));   /* 40a00000 (5.0)  */
	printf("div_3_1p5 %08lx\r\n", raw(b / 1.5f));   /* 40000000 (2.0)  */

	/* Pure-literal initializer (folds to a Ks constant). */
	float c = 0.25f;
	printf("init_0p25 %08lx\r\n", raw(c));          /* 3e800000 (0.25) */
	printf("lit_10 %08lx\r\n",    raw(10.0f));      /* 41200000 (10.0) */
	printf("lit_dotfive %08lx\r\n", raw(.5f));      /* 3f000000 (0.5)  */

	/* Literal comparisons against a runtime float. */
	printf("lt_2_2p5 %d\r\n", (a < 2.5f));          /* 1 */
	printf("gt_2_1p5 %d\r\n", (a > 1.5f));          /* 1 */
	printf("eq_3_3 %d\r\n",   (b == 3.0f));         /* 1 */
	printf("le_2_2 %d\r\n",   (a <= 2.0f));         /* 1 */
	printf("ne_2_3 %d\r\n",   (a != 3.0f));         /* 1 */
	return 0;
}
