/*
 * softlibm_probe.c — exercise the algebraic soft-float libm helpers
 * (minic/dos/softfloat.c) reached through <math.h> macro names.
 *
 * Built medium-model with --softfloat.  Each helper is single-precision and
 * lowered to a far `call _sf_*`; QBE cannot fold the call, so feeding plain
 * `f`-suffixed literals still exercises the real runtime path.  Results are
 * printed as their 32-bit bit pattern (via a union) so a wrong high word or a
 * mis-rounded result is loud against the golden.
 *
 * powf / the transcendentals are intentionally NOT tested here — they are not
 * implemented yet (they need a soft expf/logf); this probe pins the exact /
 * algebraic surface only.  See NEXT_SESSION.md.
 */
#include <stdio.h>
#include <math.h>

static unsigned long fbits(float x)
{
	union { float f; unsigned long u; } v;
	v.f = x;
	return v.u;
}

static float frombits(unsigned long b)
{
	union { float f; unsigned long u; } v;
	v.u = b;
	return v.f;
}

int main(void)
{
	float pinf = frombits(0x7F800000UL);   /* +inf */
	float qnan = frombits(0x7FC00000UL);   /* quiet NaN */

	/* classifiers */
	printf("isnan_nan=%d\n",  isnan(qnan));
	printf("isnan_one=%d\n",  isnan(1.0f));
	printf("isinf_inf=%d\n",  isinf(pinf));
	printf("isinf_one=%d\n",  isinf(1.0f));
	printf("sbit_neg=%d\n",   signbit(-3.0f));
	printf("sbit_pos=%d\n",   signbit(3.0f));

	/* sign manipulation */
	printf("fabs_neg=%08lx\n",     fbits(fabsf(-2.5f)));
	printf("copysign=%08lx\n",     fbits(copysignf(2.5f, -1.0f)));
	printf("nan_bits=%08lx\n",     fbits(nanf("")));

	/* trunc / floor / ceil */
	printf("trunc_p=%08lx\n",      fbits(truncf(2.7f)));
	printf("trunc_n=%08lx\n",      fbits(truncf(-2.7f)));
	printf("floor_p=%08lx\n",      fbits(floorf(2.7f)));
	printf("floor_n=%08lx\n",      fbits(floorf(-2.3f)));
	printf("floor_i=%08lx\n",      fbits(floorf(5.0f)));
	printf("ceil_p=%08lx\n",       fbits(ceilf(2.3f)));
	printf("ceil_n=%08lx\n",       fbits(ceilf(-2.7f)));

	/* round half away from zero */
	printf("round_h=%08lx\n",      fbits(roundf(2.5f)));
	printf("round_hn=%08lx\n",     fbits(roundf(-2.5f)));
	printf("round_d=%08lx\n",      fbits(roundf(2.4f)));

	/* round half to even */
	printf("nbint_2h=%08lx\n",     fbits(nearbyintf(2.5f)));
	printf("nbint_3h=%08lx\n",     fbits(nearbyintf(3.5f)));
	printf("nbint_d=%08lx\n",      fbits(nearbyintf(2.4f)));

	/* fmod */
	printf("fmod_a=%08lx\n",       fbits(fmodf(7.0f, 3.0f)));
	printf("fmod_b=%08lx\n",       fbits(fmodf(-7.0f, 3.0f)));
	printf("fmod_c=%08lx\n",       fbits(fmodf(5.5f, 2.0f)));
	return 0;
}
