#ifndef _MATH_H
#define _MATH_H

/*
 * math.h — single-precision soft-float libm surface for the i8086/no-8087
 * target.  The implementations live in minic/dos/softfloat.c (linked with
 * build-example.sh --softfloat).  Only the EXACT / algebraic functions are
 * provided so far; powf/expf/logf and the trig/transcendental family are
 * provided so far; the transcendental family is exp2/log2/exp/log/pow
 * (Taylor + atanh series, single-precision accurate to a few ulps).  Of
 * those, only powf is referenced by the curated MicroPython core.  The trig
 * family is still TODO (see softfloat.c).
 *
 * `float` is the only floating type that lowers to the _sf_* helpers; the
 * `f`-suffixed names are the ones MicroPython uses under
 * MICROPY_FLOAT_IMPL_FLOAT (via MICROPY_FLOAT_C_FUN), and the bare classifier
 * macros (isnan/isinf/signbit) are used directly.
 */

int   sf_isnan(float);
int   sf_isinf(float);
int   sf_signbit(float);
float sf_fabs(float);
float sf_copysign(float, float);
float sf_nan(const char *);
float sf_trunc(float);
float sf_floor(float);
float sf_ceil(float);
float sf_round(float);
float sf_nearbyint(float);
float sf_fmod(float, float);
float sf_exp2f(float);
float sf_log2f(float);
float sf_expf(float);
float sf_logf(float);
float sf_powf(float, float);

#define isnan(x)          sf_isnan(x)
#define isinf(x)          sf_isinf(x)
#define signbit(x)        sf_signbit(x)

#define fabsf(x)          sf_fabs(x)
#define fabs(x)           sf_fabs(x)
#define copysignf(x, y)   sf_copysign((x), (y))
#define copysign(x, y)    sf_copysign((x), (y))
#define nanf(s)           sf_nan(s)
#define nan(s)            sf_nan(s)
#define truncf(x)         sf_trunc(x)
#define trunc(x)          sf_trunc(x)
#define floorf(x)         sf_floor(x)
#define floor(x)          sf_floor(x)
#define ceilf(x)          sf_ceil(x)
#define ceil(x)           sf_ceil(x)
#define roundf(x)         sf_round(x)
#define round(x)          sf_round(x)
#define nearbyintf(x)     sf_nearbyint(x)
#define nearbyint(x)      sf_nearbyint(x)
#define fmodf(x, y)       sf_fmod((x), (y))
#define fmod(x, y)        sf_fmod((x), (y))
#define exp2f(x)          sf_exp2f(x)
#define exp2(x)           sf_exp2f(x)
#define log2f(x)          sf_log2f(x)
#define log2(x)           sf_log2f(x)
#define expf(x)           sf_expf(x)
#define exp(x)            sf_expf(x)
#define logf(x)           sf_logf(x)
#define log(x)            sf_logf(x)
#define powf(x, y)        sf_powf((x), (y))
#define pow(x, y)         sf_powf((x), (y))

#endif /* _MATH_H */
