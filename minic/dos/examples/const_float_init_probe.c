/*
 * const_float_init_probe.c — `const`/`volatile`-qualified FLOATING-point
 * file-scope declarations (§6z).
 *
 * Bug-loud: the minic `type` grammar enumerated `CONST TINT`, `CONST
 * TCHAR`, ... and the `vol_qual` integer cases, but OMITTED the floating
 * forms — there was no `CONST TFLOAT`/`CONST TDOUBLE` nor `vol_qual
 * TFLOAT`/`vol_qual TDOUBLE` production.  So ANY `const float`,
 * `const double`, `volatile float`, or `const volatile double`
 * declaration was a hard PARSE ERROR ("parse error") before this TU could
 * even reach codegen.  Bare `float`/`double` (and `const int`) always
 * parsed, which is why it was easy to miss.  (This also blocked
 * MICROPY_PY_MATH_CONSTANTS, whose const-float definitions need the
 * `const float` spelling.)
 *
 * The const-expr FOLDING was never the problem — `2.0f * 3.14f` and
 * `3.14159 / 2.0` already fold to a single-precision `data` constant; the
 * fix is purely the missing qualifier+float grammar productions, both
 * mapping (like the bare rules) to INT|FLOAT — double aliases to single
 * (Ks) on i8086, and `const`/`volatile` add only the QVOLATILE bit.
 *
 * Values are printed as raw IEEE-754 single bit patterns through a union
 * (the float_literal_probe idiom) so the golden is exact and needs no
 * float printf.
 *
 * Build:  tools/build-example.sh --softfloat --model=medium \
 *             minic/dos/examples/const_float_init_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/const_float_init_probe/const_float_init_probe.exe \
 *             | diff - minic/dos/tests/const_float_init_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (MEDIUM + COMPACT, --softfloat) like the
 * sibling float probes.
 */
#include <stdio.h>

union fb { float f; unsigned long u; };

static unsigned long raw(float f) { union fb x; x.f = f; return x.u; }

/* const-qualified scalars (the previously-unparseable forms). */
static const float  pi   = 3.14159265358979323846;   /* 40490fdb */
static const double e     = 2.71828182845904523536;  /* double aliases to single: 402df854 */

/* const-expr folds behind a const qualifier. */
static const float  twopi = 2.0f * 3.14159265358979323846f; /* 40c90fdb */
static const float  half  = 3.14159265358979323846 / 2.0;   /* 3fc90fdb */

/* const float array. */
static const float  tbl[3] = { 1.0f, 0.5f, 0.25f };   /* 3f800000 3f000000 3e800000 */

/* non-static const -> external linkage (exported data). */
const float gquarter = 0.25f;                          /* 3e800000 */

/* volatile / const-volatile floating forms. */
static volatile float       vf  = 1.5f;                /* 3fc00000 */
static const volatile double cvd = 10.0;               /* single 10.0: 41200000 */

int
main(void)
{
	printf("pi %08lx\r\n",       raw(pi));
	printf("e %08lx\r\n",        raw((float)e));
	printf("twopi %08lx\r\n",    raw(twopi));
	printf("half %08lx\r\n",     raw(half));
	printf("tbl0 %08lx\r\n",     raw(tbl[0]));
	printf("tbl1 %08lx\r\n",     raw(tbl[1]));
	printf("tbl2 %08lx\r\n",     raw(tbl[2]));
	printf("gquarter %08lx\r\n", raw(gquarter));
	printf("vf %08lx\r\n",       raw(vf));
	printf("cvd %08lx\r\n",      raw((float)cvd));
	return 0;
}
