/*
 * double_float_probe.c — `double` aliases to single-precision (Ks) on the
 * FPU-less i8086 target, plus static float initializers and the float<->long
 * conversion (Ostosi/Ostoui with a Kl result).
 *
 * This target has no 8087 and no 64-bit integer to build a soft-double, so
 * minic types `double` identically to `float`: 4 bytes, single precision, and
 * every op lowers through the _sf_* helpers.  A `double` that still produced a
 * Kd value would die() in the i8086 backend, so merely BUILDING + RUNNING any
 * `double` arithmetic here proves the aliasing.  Likewise a static float
 * initializer used to die "unsupported operation in constant expression", and
 * `(long)floatval` used to die "unsupported 32-bit op (cls Kl)".
 *
 * Build:  tools/build-example.sh --softfloat --model=medium \
 *             minic/dos/examples/double_float_probe.c
 * Wired into tools/test-dos.sh (MEDIUM only, --softfloat).
 */
#include <stdio.h>

union fb { float f; unsigned long u; };

static float fbits(unsigned long u) { union fb x; x.u = u; return x.f; }
static unsigned long fraw(float f)  { union fb x; x.f = f; return x.u; }

/* Static float initializers — the cival_float_text / emit_global_float_init
 * and agg_emit_scalar float-member paths. */
float  g_half = 1.5f;            /* 3fc00000 */
float  g_neg  = -0.5f;           /* bf000000 */
double g_dbl  = 2.5;             /* double==single: 4 bytes, 40200000 */

struct fpair { float a; float b; };
const struct fpair g_pair = { 0.25f, (float)(3.0) };   /* 3e800000, 40400000 */

int
main(void)
{
	double d;
	float f, f2, big, fn;
	double dd;
	long L, neg;
	int I, n;

	/* sizeof(double)==4 proves double aliases to single. */
	printf("szd %d\r\n", (int)sizeof(double));     /* 4 */
	printf("szf %d\r\n", (int)sizeof(float));      /* 4 */

	/* Static float global / struct-member bit patterns. */
	printf("ghalf %08lx\r\n", fraw(g_half));       /* 3fc00000 */
	printf("gneg %08lx\r\n",  fraw(g_neg));        /* bf000000 */
	printf("gdbl %08lx\r\n",  fraw(g_dbl));        /* 40200000 */
	printf("pa %08lx\r\n",    fraw(g_pair.a));     /* 3e800000 */
	printf("pb %08lx\r\n",    fraw(g_pair.b));     /* 40400000 */

	/* double arithmetic — single precision (would die() as Kd otherwise). */
	d = 2.0;
	d = d * 2.5;                                   /* 5.0 */
	printf("dmul %08lx\r\n", fraw(d));             /* 40a00000 */

	/* float<->double conversions are identity (no exts/truncd emitted). */
	f = fbits(0x40490fdbUL);                       /* ~pi */
	dd = f;                                        /* widen: identity */
	f2 = (float)dd;                                /* narrow: identity */
	printf("fconv %08lx\r\n", fraw(f2));           /* 40490fdb */

	/* float -> long (Ostosi, Kl result — the mp_float_hash shape). */
	big = fbits(0x42c80000UL);                     /* 100.0 */
	L = (long)big;                                 /* 100 */
	neg = (long)fbits(0xc2c80000UL);               /* -100.0 -> -100 */
	I = (int)fbits(0x40200000UL);                  /* 2.5 -> 2 */
	printf("ftol %ld\r\n", L);                     /* 100 */
	printf("ftol_neg %ld\r\n", neg);               /* -100 */
	printf("ftoi %d\r\n", I);                      /* 2 */

	/* int -> float (swtof) round-trips. */
	n = 7;
	fn = (float)n;                                 /* 7.0 */
	printf("itof %08lx\r\n", fraw(fn));            /* 40e00000 */
	return 0;
}
