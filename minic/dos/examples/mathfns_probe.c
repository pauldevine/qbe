/*
 * mathfns_probe.c — pin the §5b soft-libm additions (sqrt + trig +
 * inverse-trig + frexp/ldexp/modf + isfinite) reached through <math.h>,
 * plus the BARE-SYMBOL function-pointer path MicroPython's modmath.c uses
 * (math_generic_1(x, sqrtf): a function-like macro does not expand without
 * a following parenthesis, so `sqrtf` must exist as a REAL function).
 *
 * Built medium + compact with --softfloat.  Results print as 32-bit bit
 * patterns so a mis-rounded low bit is loud against the golden.  The golden
 * is captured from the DOSBox run and every line is independently verified
 * against host-double references by build/mathfns-verify.py (sqrt lines
 * must be exactly the host value — correctly rounded; trig within a few
 * ulps of the host double rounded to float).
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

/* The modmath.c shape: bare libm names taken as function pointers. */
static unsigned long via1(float (*f)(float), float x)
{
	return fbits(f(x));
}

static unsigned long via2(float (*f)(float, float), float x, float y)
{
	return fbits(f(x, y));
}

int main(void)
{
	float pinf = frombits(0x7F800000UL);
	int e;
	float ip;

	/* sqrt — correctly rounded incl. the negative-half-exponent case */
	printf("sqrt4=%08lx\n",    fbits(sqrtf(4.0f)));
	printf("sqrt2=%08lx\n",    fbits(sqrtf(2.0f)));
	printf("sqrt625=%08lx\n",  fbits(sqrtf(6.25f)));
	printf("sqrthalf=%08lx\n", fbits(sqrtf(0.5f)));
	printf("sqrttiny=%08lx\n", fbits(sqrtf(2e-20f)));
	printf("sqrtbig=%08lx\n",  fbits(sqrtf(3e30f)));
	printf("sqrtneg=%08lx\n",  fbits(sqrtf(-1.0f)));
	printf("sqrtinf=%08lx\n",  fbits(sqrtf(pinf)));
	printf("sqrtzero=%08lx\n", fbits(sqrtf(0.0f)));

	/* sin/cos/tan — small, multi-octant, negative, large-arg */
	printf("sin1=%08lx\n",     fbits(sinf(1.0f)));
	printf("sinneg=%08lx\n",   fbits(sinf(-0.5f)));
	printf("sin3=%08lx\n",     fbits(sinf(3.0f)));
	printf("sin100=%08lx\n",   fbits(sinf(100.0f)));
	printf("sin1000=%08lx\n",  fbits(sinf(1000.0f)));
	printf("cos1=%08lx\n",     fbits(cosf(1.0f)));
	printf("cos4=%08lx\n",     fbits(cosf(4.0f)));
	printf("cosneg=%08lx\n",   fbits(cosf(-2.0f)));
	printf("cos100=%08lx\n",   fbits(cosf(100.0f)));
	printf("tan1=%08lx\n",     fbits(tanf(1.0f)));
	printf("tanneg=%08lx\n",   fbits(tanf(-0.3f)));
	printf("saninf=%08lx\n",   fbits(sinf(pinf)));

	/* asin/acos/atan/atan2 */
	printf("asinh5=%08lx\n",   fbits(asinf(0.5f)));
	printf("asin1=%08lx\n",    fbits(asinf(1.0f)));
	printf("asinn1=%08lx\n",   fbits(asinf(-1.0f)));
	printf("asindom=%08lx\n",  fbits(asinf(1.5f)));
	printf("acosh5=%08lx\n",   fbits(acosf(0.5f)));
	printf("acos1=%08lx\n",    fbits(acosf(1.0f)));
	printf("acosn1=%08lx\n",   fbits(acosf(-1.0f)));
	printf("acosn99=%08lx\n",  fbits(acosf(-0.99f)));
	printf("atan1=%08lx\n",    fbits(atanf(1.0f)));
	printf("atansm=%08lx\n",   fbits(atanf(0.1f)));
	printf("atanbig=%08lx\n",  fbits(atanf(50.0f)));
	printf("ataninf=%08lx\n",  fbits(atanf(pinf)));
	printf("atanneg=%08lx\n",  fbits(atanf(-2.0f)));
	printf("at2q1=%08lx\n",    fbits(atan2f(1.0f, 1.0f)));
	printf("at2q2=%08lx\n",    fbits(atan2f(1.0f, -1.0f)));
	printf("at2q3=%08lx\n",    fbits(atan2f(-1.0f, -1.0f)));
	printf("at2q4=%08lx\n",    fbits(atan2f(-1.0f, 1.0f)));
	printf("at2y0=%08lx\n",    fbits(atan2f(0.0f, -2.0f)));
	printf("at2x0=%08lx\n",    fbits(atan2f(3.0f, 0.0f)));
	printf("at2inf=%08lx\n",   fbits(atan2f(pinf, pinf)));

	/* frexp / ldexp / modf / isfinite */
	printf("frx8=%08lx e=%d\n",  fbits(frexpf(8.0f, &e)), e);
	printf("frx3=%08lx e=%d\n",  fbits(frexpf(-3.0f, &e)), e);
	printf("frx0=%08lx e=%d\n",  fbits(frexpf(0.0f, &e)), e);
	printf("ldx3=%08lx\n",       fbits(ldexpf(1.5f, 4)));
	printf("ldxn=%08lx\n",       fbits(ldexpf(-6.0f, -3)));
	printf("ldxovf=%08lx\n",     fbits(ldexpf(1.0f, 300)));
	printf("ldxunf=%08lx\n",     fbits(ldexpf(1.0f, -300)));
	ip = 99.0f;
	printf("modf=%08lx ip=%08lx\n",  fbits(modff(2.75f, &ip)), fbits(ip));
	printf("modfn=%08lx ip=%08lx\n", fbits(modff(-2.0f, &ip)), fbits(ip));
	printf("isfin=%d %d %d\n", isfinite(1.0f), isfinite(pinf), isfinite(frombits(0x7FC00000UL)));

	/* function-pointer path (bare symbols, like modmath.c) */
	printf("fpsqrt=%08lx\n",   via1(sqrtf, 9.0f));
	printf("fpsin=%08lx\n",    via1(sinf, 2.0f));
	printf("fpcos=%08lx\n",    via1(cosf, 2.0f));
	printf("fptan=%08lx\n",    via1(tanf, 0.5f));
	printf("fpasin=%08lx\n",   via1(asinf, 0.25f));
	printf("fpacos=%08lx\n",   via1(acosf, 0.25f));
	printf("fpatan=%08lx\n",   via1(atanf, 3.0f));
	printf("fpexp=%08lx\n",    via1(expf, 1.0f));
	printf("fpat2=%08lx\n",    via2(atan2f, 2.0f, 3.0f));
	printf("fppow=%08lx\n",    via2(powf, 2.0f, 10.0f));
	printf("fpfmod=%08lx\n",   via2(fmodf, 7.5f, 2.0f));

	printf("DONE\n");
	return 0;
}
