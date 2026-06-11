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
int   sf_isfinite(float);
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
float sf_inff(void);
float sf_sqrt(float);
float sf_sin(float);
float sf_cos(float);
float sf_tan(float);
float sf_asin(float);
float sf_acos(float);
float sf_atan(float);
float sf_atan2(float, float);
float sf_frexp(float, int *);
float sf_ldexp(float, int);
float sf_modf(float, float *);

/* The bare libm names below also exist as REAL exported functions in
 * softfloat.c so they can be taken by ADDRESS (MicroPython's modmath.c
 * passes them as function pointers, where a function-like macro does not
 * expand).  These prototypes MUST come before the same-named function-like
 * macros below: a later `float sqrtf(float);` line would itself be
 * macro-expanded. */
float sqrtf(float);
float sinf(float);
float cosf(float);
float tanf(float);
float asinf(float);
float acosf(float);
float atanf(float);
float atan2f(float, float);
float expf(float);
float powf(float, float);
float fmodf(float, float);

#define M_E               2.71828182845904523536
#define M_PI              3.14159265358979323846
#define M_SQRT2           1.41421356237309504880

#define INFINITY          (sf_inff())
#define HUGE_VALF         (sf_inff())
#define HUGE_VAL          (sf_inff())
#define NAN               (sf_nan(""))

#define isnan(x)          sf_isnan(x)
#define isinf(x)          sf_isinf(x)
#define isfinite(x)       sf_isfinite(x)
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
#define sqrtf(x)          sf_sqrt(x)
#define sqrt(x)           sf_sqrt(x)
#define sinf(x)           sf_sin(x)
#define sin(x)            sf_sin(x)
#define cosf(x)           sf_cos(x)
#define cos(x)            sf_cos(x)
#define tanf(x)           sf_tan(x)
#define tan(x)            sf_tan(x)
#define asinf(x)          sf_asin(x)
#define asin(x)           sf_asin(x)
#define acosf(x)          sf_acos(x)
#define acos(x)           sf_acos(x)
#define atanf(x)          sf_atan(x)
#define atan(x)           sf_atan(x)
#define atan2f(y, x)      sf_atan2((y), (x))
#define atan2(y, x)       sf_atan2((y), (x))
#define frexpf(x, e)      sf_frexp((x), (e))
#define frexp(x, e)       sf_frexp((x), (e))
#define ldexpf(x, n)      sf_ldexp((x), (n))
#define ldexp(x, n)       sf_ldexp((x), (n))
#define modff(x, ip)      sf_modf((x), (ip))
#define modf(x, ip)       sf_modf((x), (ip))

#endif /* _MATH_H */
