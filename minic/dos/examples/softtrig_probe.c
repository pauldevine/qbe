/*
 * softtrig_probe.c — exercise the transcendental soft-float libm helpers
 * (exp2f/log2f/expf/logf/powf in minic/dos/softfloat.c) through <math.h>.
 *
 * Built medium-model with --softfloat.  Each helper is single-precision and
 * lowered to a far `call _sf_*`; QBE cannot fold the call, so feeding plain
 * `f`-suffixed literals still exercises the real runtime path.  Results are
 * printed as their 32-bit bit pattern (via a union) so a wrong high word or a
 * mis-rounded result is loud against the golden.
 *
 * powf is the only one of these the curated MicroPython core links (objfloat
 * `**`, parsenum 1eN, round(x,n)).  The integer-exponent cases (2**10, 10**5,
 * 10**-2) pin the exact binary-exponentiation fast path; the fractional ones
 * pin the exp2/log2 core.
 */
#include <stdio.h>
#include <math.h>

static unsigned long fbits(float x)
{
	union { float f; unsigned long u; } v;
	v.f = x;
	return v.u;
}

int main(void)
{
	/* exp2 / log2 */
	printf("exp2_h=%08lx\n",  fbits(exp2f(0.5f)));      /* sqrt(2) */
	printf("exp2_t=%08lx\n",  fbits(exp2f(10.0f)));     /* 1024 */
	printf("log2_8=%08lx\n",  fbits(log2f(8.0f)));      /* 3 */
	printf("log2_t=%08lx\n",  fbits(log2f(10.0f)));     /* 3.321928 */

	/* exp / log (natural) */
	printf("exp_1=%08lx\n",   fbits(expf(1.0f)));       /* e */
	printf("exp_n=%08lx\n",   fbits(expf(-2.5f)));
	printf("log_e=%08lx\n",   fbits(logf(2.7182817f))); /* ~1 */
	printf("log_k=%08lx\n",   fbits(logf(1000.0f)));

	/* pow: integer fast path (exact) */
	printf("pow_210=%08lx\n", fbits(powf(2.0f, 10.0f)));   /* 1024 */
	printf("pow_105=%08lx\n", fbits(powf(10.0f, 5.0f)));   /* 100000 */
	printf("pow_10m2=%08lx\n",fbits(powf(10.0f, -2.0f)));  /* 0.01 */
	printf("pow_n23=%08lx\n", fbits(powf(-2.0f, 3.0f)));   /* -8 */
	printf("pow_n22=%08lx\n", fbits(powf(-2.0f, 2.0f)));   /* 4 */

	/* pow: fractional (exp2/log2 core) */
	printf("pow_205=%08lx\n", fbits(powf(2.0f, 0.5f)));    /* sqrt(2) */
	printf("pow_905=%08lx\n", fbits(powf(9.0f, 0.5f)));    /* 3 */
	printf("pow_333=%08lx\n", fbits(powf(3.0f, 3.3f)));

	/* pow: edge cases */
	printf("pow_y0=%08lx\n",  fbits(powf(5.0f, 0.0f)));    /* 1 */
	printf("pow_0p=%08lx\n",  fbits(powf(0.0f, 3.0f)));    /* 0 */
	printf("pow_nan=%d\n",    isnan(powf(-2.0f, 2.5f)));   /* 1 */
	return 0;
}
