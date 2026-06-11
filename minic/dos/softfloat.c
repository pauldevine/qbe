/*
 * softfloat.c — single-precision (IEEE-754 binary32) software floating point
 * for the i8086 / no-8087 target.  SPIKE for routing minic/qbe Ks operations
 * through soft-float helper calls instead of 8087 instructions.
 *
 * Each "float" is its 32-bit bit pattern carried in a U32 (which is exactly
 * how the i8086 backend already carries a Ks value: a 32-bit DX:AX word).
 *
 * HARD CONSTRAINTS of this target (see CLAUDE.md):
 *   - There is NO 64-bit integer type.  Kl / `long` is 32 bits.  The 24x24
 *     mantissa multiply therefore builds a 48-bit product as a (phi,plo) pair
 *     via 16-bit partial products; the mantissa divide is bitwise
 *     shift-subtract (same shape as libstub's _qbe_div32u).
 *   - `int`/`unsigned` are 16-bit.  EVERY shift whose result needs bit >=16
 *     must operate on a U32.  We never write a bare `1 << n`; use B(n).
 *
 * Scope / known simplifications (documented; fine for the spike and for a
 * first MicroPython float bring-up — revisit if a consumer needs more):
 *   - Subnormals are flushed to (signed) zero on input and underflow.
 *   - Rounding is round-to-nearest, ties-to-even on the normal paths.
 *   - NaN is canonicalised to a single quiet NaN; no NaN payloads/signalling.
 */

#ifdef SF_HOST
#include <stdint.h>
typedef uint32_t U32;
typedef int32_t  S32;
typedef uint16_t U16;
#else
typedef unsigned long U32;
typedef long          S32;
typedef unsigned int  U16;
#endif

#define B(n)        ((U32)1 << (n))
#define EXP_MASK    ((U32)0xFF)
#define MANT_MASK   ((U32)0x7FFFFF)   /* low 23 bits */
#define IMPLICIT    ((U32)0x800000)   /* 1 << 23 */
#define SIGN_BIT    ((U32)0x80000000)
#define ABS_MASK    ((U32)0x7FFFFFFF)
#define QNAN        ((U32)0x7FC00000)

#define EXP_OF(a)   ((int)(((a) >> 23) & EXP_MASK))
#define FRAC_OF(a)  ((a) & MANT_MASK)
#define SIGN_OF(a)  ((int)((a) >> 31))

static U32 sf_inf(int sign)  { return ((U32)sign << 31) | ((U32)0xFF << 23); }
static int sf_is_nan(U32 a)  { return EXP_OF(a) == 0xFF && FRAC_OF(a) != 0; }

/* x >> s, but force bit0 to 1 if any nonzero bits were shifted out (sticky). */
static U32 shr_sticky(U32 x, int s)
{
	U32 lost;
	if (s <= 0)
		return x;
	if (s >= 32)
		return x ? 1 : 0;
	lost = x & (B(s) - 1);
	x >>= s;
	if (lost)
		x |= 1;
	return x;
}

/* Position of the highest set bit (0..31).  x must be nonzero. */
static int topbit(U32 x)
{
	int n = 0;
	while (x > 1) { x >>= 1; n++; }
	return n;
}

/*
 * Round + pack a normalized result.
 *   sign  : 0/1
 *   exp   : biased stored exponent assuming `sig` has its leading 1 at bit 23
 *   sig   : 24-bit significand, leading 1 at bit 23 (range [1<<23, 1<<24))
 *   guard : the bit immediately below sig's LSB (the round bit)
 *   sticky: OR of all bits below the guard bit
 * Flushes subnormal/underflow to signed zero; overflows to signed inf.
 */
static U32 sf_round_pack(int sign, int exp, U32 sig, int guard, int sticky)
{
	if (guard && (sticky || (sig & 1))) {
		sig++;
		if (sig == B(24)) {     /* 1.111..1 + ulp -> 10.000.. */
			sig >>= 1;
			exp++;
		}
	}
	if (exp >= 0xFF)
		return sf_inf(sign);
	if (exp <= 0)
		return (U32)sign << 31;   /* flush subnormals to signed zero */
	return ((U32)sign << 31) | ((U32)exp << 23) | (sig & MANT_MASK);
}

U32 sf_add(U32 a, U32 b)
{
	int sa = SIGN_OF(a), sb = SIGN_OF(b);
	int ea = EXP_OF(a),  eb = EXP_OF(b);
	U32 ma, mb, m;
	int exp, sign;

	if (sf_is_nan(a) || sf_is_nan(b))
		return QNAN;
	if (ea == 0xFF) {                 /* a = +/-inf */
		if (eb == 0xFF && sa != sb)
			return QNAN;          /* inf + -inf */
		return a;
	}
	if (eb == 0xFF)
		return b;

	/* Build 24-bit mantissas; subnormals (exp==0,frac!=0) are flushed to 0. */
	ma = (ea == 0) ? 0 : (FRAC_OF(a) | IMPLICIT);
	mb = (eb == 0) ? 0 : (FRAC_OF(b) | IMPLICIT);

	if (ma == 0 && mb == 0)
		return (sa && sb) ? SIGN_BIT : 0;   /* (-0)+(-0) = -0 else +0 */
	if (ma == 0) return b;
	if (mb == 0) return a;

	/* Shift left 3 to make room for guard/round/sticky in bits [2:0]. */
	ma <<= 3;
	mb <<= 3;
	if (ea >= eb) { exp = ea; mb = shr_sticky(mb, ea - eb); }
	else          { exp = eb; ma = shr_sticky(ma, eb - ea); }

	if (sa == sb) {
		sign = sa;
		m = ma + mb;
		if (m & B(27)) {              /* carry out of bit 26 */
			m = (m >> 1) | (m & 1);   /* keep sticky in bit0 */
			exp++;
		}
	} else {
		if (ma >= mb) { sign = sa; m = ma - mb; }
		else          { sign = sb; m = mb - ma; }
		if (m == 0)
			return 0;                 /* exact cancellation -> +0 */
		while (!(m & B(26))) {        /* renormalize leading 1 to bit 26 */
			m <<= 1;
			exp--;
		}
	}

	/* leading 1 at bit 26 => 24-bit significand at bits [26:3]. */
	return sf_round_pack(sign, exp, m >> 3, (int)((m >> 2) & 1), (m & 3) != 0);
}

U32 sf_sub(U32 a, U32 b)
{
	return sf_add(a, b ^ SIGN_BIT);
}

U32 sf_mul(U32 a, U32 b)
{
	int sa = SIGN_OF(a), sb = SIGN_OF(b);
	int ea = EXP_OF(a),  eb = EXP_OF(b);
	int sign = sa ^ sb;
	U32 ma, mb;
	U16 ah, al, bh, bl;
	U32 p0, mid, lo, hi, tmp, sig;
	int exp, guard, sticky;

	if (sf_is_nan(a) || sf_is_nan(b))
		return QNAN;
	if (ea == 0xFF) {                 /* a inf */
		if (eb == 0 && FRAC_OF(b) == 0) return QNAN;  /* inf * 0 */
		return sf_inf(sign);
	}
	if (eb == 0xFF) {
		if (ea == 0 && FRAC_OF(a) == 0) return QNAN;
		return sf_inf(sign);
	}
	if (ea == 0 || eb == 0)
		return (U32)sign << 31;       /* x * 0 (subnormals flushed) -> signed 0 */

	ma = FRAC_OF(a) | IMPLICIT;       /* 24-bit, leading 1 at bit 23 */
	mb = FRAC_OF(b) | IMPLICIT;
	exp = ea + eb - 127;

	/* 24x24 -> 48-bit product (phi:plo) via 16-bit partials (no uint64). */
	ah = (U16)(ma >> 16); al = (U16)(ma & 0xFFFF);
	bh = (U16)(mb >> 16); bl = (U16)(mb & 0xFFFF);
	p0  = (U32)al * (U32)bl;                       /* bits 0..31  */
	mid = (U32)al * (U32)bh + (U32)ah * (U32)bl;   /* bits 16..   */
	tmp = (mid & 0xFFFF) << 16;
	lo  = p0 + tmp;
	hi  = (U32)ah * (U32)bh + (mid >> 16) + ((lo < p0) ? 1 : 0);
	/* full = hi * 2^32 + lo, with hi < 2^16 (only 48 bits used) */

	if (hi & B(15)) {                 /* product in [2,4): leading 1 at bit 47 */
		sig    = (hi << 8) | (lo >> 24);          /* bits [47:24] */
		guard  = (int)((lo >> 23) & 1);
		sticky = (lo & ((U32)0x7FFFFF)) != 0;
		exp++;
	} else {                          /* product in [1,2): leading 1 at bit 46 */
		sig    = (hi << 9) | (lo >> 23);          /* bits [46:23] */
		guard  = (int)((lo >> 22) & 1);
		sticky = (lo & ((U32)0x3FFFFF)) != 0;
	}
	return sf_round_pack(sign, exp, sig, guard, sticky);
}

U32 sf_div(U32 a, U32 b)
{
	int sa = SIGN_OF(a), sb = SIGN_OF(b);
	int ea = EXP_OF(a),  eb = EXP_OF(b);
	int sign = sa ^ sb;
	U32 ma, mb, q, sig;
	int exp, i, guard, sticky;

	if (sf_is_nan(a) || sf_is_nan(b))
		return QNAN;
	if (ea == 0xFF) {                 /* a inf */
		if (eb == 0xFF) return QNAN;  /* inf / inf */
		return sf_inf(sign);
	}
	if (eb == 0xFF)                   /* finite / inf -> 0 */
		return (U32)sign << 31;
	if (ea == 0) {                    /* a is zero (subnormals flushed) */
		if (eb == 0) return QNAN;     /* 0 / 0 */
		return (U32)sign << 31;
	}
	if (eb == 0)                      /* x / 0 -> inf */
		return sf_inf(sign);

	ma = FRAC_OF(a) | IMPLICIT;
	mb = FRAC_OF(b) | IMPLICIT;
	exp = ea - eb + 127;

	/* Force the quotient into [1,2): the shift-subtract step below extracts
	 * one quotient bit per iteration, which is only valid while the running
	 * remainder is < mb.  ma,mb in [2^23,2^24) so ma/mb in (0.5,2); if ma<mb
	 * the quotient would be <1, so scale the dividend up one bit. */
	if (ma < mb) { ma <<= 1; exp--; }

	/* 25 quotient bits: bit24 = the integer 1, bits[24:1] = 24-bit
	 * significand (leading 1 at bit 23 after >>1), bit0 = guard.  Whatever
	 * remains in ma after the loop is the sticky residue. */
	q = 0;
	for (i = 0; i < 25; i++) {
		q <<= 1;
		if (ma >= mb) { ma -= mb; q |= 1; }
		ma <<= 1;
	}
	sig    = q >> 1;
	guard  = (int)(q & 1);
	sticky = (ma != 0);
	return sf_round_pack(sign, exp, sig, guard, sticky);
}

/* Signed 32-bit int -> float (round-to-nearest-even). */
U32 sf_from_int(S32 v)
{
	int sign, exp, L, sh, guard, sticky;
	U32 u, sig;

	if (v == 0)
		return 0;
	sign = (v < 0) ? 1 : 0;
	u = sign ? (~(U32)v + 1) : (U32)v;   /* magnitude; correct for INT_MIN */
	L = topbit(u);
	exp = 127 + L;
	if (L <= 23) {
		sig = u << (23 - L);
		guard = 0;
		sticky = 0;
	} else {
		sh = L - 23;
		sig    = u >> sh;
		guard  = (int)((u >> (sh - 1)) & 1);
		sticky = (u & (B(sh - 1) - 1)) != 0;
	}
	return sf_round_pack(sign, exp, sig, guard, sticky);
}

/* float -> signed 32-bit int, truncating toward zero. */
S32 sf_to_int(U32 a)
{
	int sign = SIGN_OF(a), e = EXP_OF(a);
	U32 m, v;
	int sh;

	if (e == 0xFF)                       /* inf/nan */
		return sf_is_nan(a) ? 0 : (sign ? (S32)SIGN_BIT : (S32)ABS_MASK);
	if (e == 0)
		return 0;                        /* zero / subnormal */
	e -= 127;                            /* unbiased: value = 1.frac * 2^e */
	if (e < 0)
		return 0;                        /* |x| < 1 truncates to 0 */
	if (e >= 31)                         /* out of range -> clamp */
		return sign ? (S32)SIGN_BIT : (S32)ABS_MASK;
	m = FRAC_OF(a) | IMPLICIT;           /* leading 1 at bit 23 */
	sh = e - 23;
	v = (sh >= 0) ? (m << sh) : (m >> (-sh));
	return sign ? -(S32)v : (S32)v;
}

/* Compare: -1 (a<b), 0 (a==b), 1 (a>b), 2 (unordered: a or b is NaN). */
int sf_cmp(U32 a, U32 b)
{
	int sa, sb, za, zb;
	U32 mA, mB;

	if (sf_is_nan(a) || sf_is_nan(b))
		return 2;
	za = (EXP_OF(a) == 0);               /* zero (subnormals flushed) */
	zb = (EXP_OF(b) == 0);
	if (za && zb)
		return 0;                        /* +0 == -0 */
	sa = SIGN_OF(a);
	sb = SIGN_OF(b);
	if (sa != sb)
		return sa ? -1 : 1;              /* negative < positive */
	mA = a & ABS_MASK;
	mB = b & ABS_MASK;
	if (mA == mB)
		return 0;
	if (sa)                              /* both negative: bigger mag = smaller */
		return (mA > mB) ? -1 : 1;
	return (mA > mB) ? 1 : -1;
}

/* ======================================================================
 * Algebraic soft-libm surface (for MICROPY_FLOAT_IMPL_FLOAT).
 *
 * The helpers above take a U32 because the i8086 backend lowers a Ks
 * arithmetic op directly to `call far _sf_add` etc., handing over the raw
 * 32-bit bit pattern.  The helpers below are instead called from C SOURCE
 * (formatfloat.c does `fabsf(x)`, objfloat.c does `floorf(x)`, ...), so they
 * must have honest `float`/`int` signatures and reinterpret the value to its
 * bit pattern internally.  A `float` and a `U32` argument occupy the same
 * 32-bit register pair, but minic would *convert* (truncate) a float passed
 * to a U32 parameter, so the reinterpret has to happen here via a union.
 *
 * Only EXACT / algebraic operations live here — no transcendentals.  powf
 * (needed by objfloat `**`, parsenum exponents, round/pow builtins) needs a
 * soft expf/logf and is a separate piece of work; it is intentionally absent.
 * ====================================================================== */

#define ONE_F   ((U32)0x3F800000)    /* 1.0f */
#define HALF_F  ((U32)0x3F000000)    /* 0.5f */

union sf_cvt { float f; U32 u; };

static U32 sf_bits(float x)     { union sf_cvt v; v.f = x; return v.u; }
static float sf_frombits(U32 b) { union sf_cvt v; v.u = b; return v.f; }

int sf_isnan(float x)   { U32 a = sf_bits(x); return EXP_OF(a) == 0xFF && FRAC_OF(a) != 0; }
int sf_isinf(float x)   { U32 a = sf_bits(x); return EXP_OF(a) == 0xFF && FRAC_OF(a) == 0; }
int sf_signbit(float x) { return (int)(sf_bits(x) >> 31); }

float sf_fabs(float x)  { return sf_frombits(sf_bits(x) & ABS_MASK); }

float sf_copysign(float x, float y)
{
	return sf_frombits((sf_bits(x) & ABS_MASK) | (sf_bits(y) & SIGN_BIT));
}

/* nan("tag"): the tag/payload is ignored — we canonicalise to one quiet NaN. */
float sf_nan(const char *tag) { (void)tag; return sf_frombits(QNAN); }

/* +inf as a float — backs the INFINITY / HUGE_VALF macros in <math.h>.
   (Those are compile-time constants in hosted C, but minic has no float-inf
   literal; this is a cheap runtime builder, gc-stripped when unreferenced.) */
float sf_inff(void) { return sf_frombits(sf_inf(0)); }

/* Truncate toward zero: clear the fractional mantissa bits. */
float sf_trunc(float x)
{
	U32 a = sf_bits(x);
	int e = EXP_OF(a) - 127;             /* unbiased: value = 1.frac * 2^e */
	U32 mask;

	if (EXP_OF(a) == 0xFF)               /* inf / nan */
		return x;
	if (e < 0)                           /* |x| < 1 -> +/- 0 */
		return sf_frombits(a & SIGN_BIT);
	if (e >= 23)                         /* no fractional bits */
		return x;
	mask = MANT_MASK >> e;               /* low (23-e) bits are fractional */
	return sf_frombits(a & ~mask);
}

/* Floor toward -inf. */
float sf_floor(float x)
{
	U32 a = sf_bits(x);
	U32 t;

	if (EXP_OF(a) == 0xFF)
		return x;
	t = sf_bits(sf_trunc(x));
	if (SIGN_OF(a) && t != a)            /* x < 0 and not integral: trunc - 1 */
		return sf_frombits(sf_sub(t, ONE_F));
	return sf_frombits(t);
}

/* Ceil toward +inf:  ceil(x) = -floor(-x). */
float sf_ceil(float x)
{
	U32 a = sf_bits(x);
	if (EXP_OF(a) == 0xFF)
		return x;
	return sf_frombits(sf_bits(sf_floor(sf_frombits(a ^ SIGN_BIT))) ^ SIGN_BIT);
}

/* Round half away from zero (C round()). */
float sf_round(float x)
{
	U32 a = sf_bits(x);
	int sign;
	U32 ax, fl, frac;

	if (EXP_OF(a) == 0xFF)
		return x;
	sign = SIGN_OF(a);
	ax = a & ABS_MASK;
	fl = sf_bits(sf_floor(sf_frombits(ax)));
	frac = sf_sub(ax, fl);               /* ax - floor(ax), in [0,1) */
	if (sf_cmp(frac, HALF_F) >= 0)       /* >= 0.5 -> round up */
		fl = sf_add(fl, ONE_F);
	return sf_frombits(sign ? (fl | SIGN_BIT) : fl);
}

/* Round to nearest, ties to even (C nearbyint() under default rounding). */
float sf_nearbyint(float x)
{
	U32 a = sf_bits(x);
	int sign, c;
	U32 ax, fl, frac;

	if (EXP_OF(a) == 0xFF)
		return x;
	sign = SIGN_OF(a);
	ax = a & ABS_MASK;
	fl = sf_bits(sf_floor(sf_frombits(ax)));
	frac = sf_sub(ax, fl);
	c = sf_cmp(frac, HALF_F);
	if (c > 0) {
		fl = sf_add(fl, ONE_F);          /* > 0.5 -> up */
	} else if (c == 0) {
		if (sf_to_int(fl) & 1)           /* tie -> round to even */
			fl = sf_add(fl, ONE_F);
	}
	return sf_frombits(sign ? (fl | SIGN_BIT) : fl);
}

/* fmod(x, y): IEEE remainder of x/y with the sign of x, exact.
 * Reduces |x| by exponent-aligned subtraction of |y| (scale |y| up by 2^k by
 * adding k to its exponent field; each step is an exact binary32 subtract). */
float sf_fmod(float x, float y)
{
	U32 bx = sf_bits(x), by = sf_bits(y);
	U32 ax = bx & ABS_MASK, ay = by & ABS_MASK;
	int sign = SIGN_OF(bx);
	U32 r, yk;
	int k;

	if (EXP_OF(bx) == 0xFF || ay == 0)   /* x inf/nan, or y == 0 -> nan */
		return sf_frombits(QNAN);
	if (EXP_OF(ay) == 0xFF)              /* y inf -> x (x finite here) */
		return x;
	if (sf_cmp(ax, ay) < 0)              /* |x| < |y| -> x unchanged */
		return x;

	r = ax;
	while (sf_cmp(r, ay) >= 0) {
		k = EXP_OF(r) - EXP_OF(ay);      /* >= 0 since r >= ay */
		yk = ay + ((U32)k << 23);        /* ay * 2^k */
		if (sf_cmp(yk, r) > 0)           /* overshot: back off one binade */
			yk = ay + ((U32)(k - 1) << 23);
		r = sf_sub(r, yk);
	}
	return sf_frombits(sign ? (r | SIGN_BIT) : r);
}

/* ======================================================================
 * Transcendental soft-libm: exp2/log2 (cores), exp/log (derived), and powf.
 *
 * Built on the exact sf_add/sf_sub/sf_mul/sf_div primitives (U32 bit
 * patterns), evaluated with Horner's method.  Accuracy target is single
 * precision (a few ulps): a Taylor series for 2^r over r in [-0.5,0.5] and
 * the atanh series for log over a sqrt2-centred mantissa.
 *
 * Of these, only powf is referenced by the curated MicroPython core
 * (objfloat `**`, parsenum exponents, round(x,n)).  exp2f/log2f/expf/logf
 * ride along for completeness and are gc-section-stripped from the MP image
 * if unused.  powf is computed as 2^(y*log2(x)) with integer-exponent and
 * negative-base handling.
 * ====================================================================== */

/* 2^r Taylor coefficients (ln2)^k/k! as binary32 bit patterns. */
#define EXP2_C0   ((U32)0x3F800000)   /* 1.0 */
#define EXP2_C1   ((U32)0x3F317218)   /* ln2 */
#define EXP2_C2   ((U32)0x3E75FDF0)   /* ln2^2/2 */
#define EXP2_C3   ((U32)0x3D635847)   /* ln2^3/6 */
#define EXP2_C4   ((U32)0x3C1D955B)   /* ln2^4/24 */
#define EXP2_C5   ((U32)0x3AAEC3FF)   /* ln2^5/120 */
#define EXP2_C6   ((U32)0x39218489)   /* ln2^6/720 */
#define EXP2_C7   ((U32)0x377FE5FE)   /* ln2^7/5040 */

#define LN2_F     ((U32)0x3F317218)   /* 0.69314718 */
#define INVLN2_F  ((U32)0x3FB8AA3B)   /* 1.44269504 = log2(e) */
#define SQRT2_F   ((U32)0x3FB504F3)   /* 1.41421356 */

/* log atanh bracket coefficients 1, 1/3, 1/5, 1/7, 1/9. */
#define LOG_C0    ((U32)0x3F800000)
#define LOG_C1    ((U32)0x3EAAAAAB)
#define LOG_C2    ((U32)0x3E4CCCCD)
#define LOG_C3    ((U32)0x3E124925)
#define LOG_C4    ((U32)0x3DE38E39)

#define TWO_F     ((U32)0x40000000)   /* 2.0 */
#define NINF      ((U32)0xFF800000)   /* -inf */

/* a * 2^n via the exponent field; clamps to signed inf / signed zero.
 * `a` must be finite and normal (the only callers pass such values). */
static U32 sf_scalbn(U32 a, int n)
{
	int e;
	if (EXP_OF(a) == 0xFF || (a & ABS_MASK) == 0)
		return a;                         /* inf / nan / zero unchanged */
	e = EXP_OF(a) + n;
	if (e >= 0xFF)
		return sf_inf(SIGN_OF(a));
	if (e <= 0)
		return (U32)SIGN_OF(a) << 31;     /* flush to signed zero */
	return (a & ~(EXP_MASK << 23)) | ((U32)e << 23);
}

/* 2^r for r (bit pattern) in [-0.5, 0.5], Taylor via Horner. */
static U32 sf_exp2_frac(U32 r)
{
	U32 p = EXP2_C7;
	p = sf_add(EXP2_C6, sf_mul(r, p));
	p = sf_add(EXP2_C5, sf_mul(r, p));
	p = sf_add(EXP2_C4, sf_mul(r, p));
	p = sf_add(EXP2_C3, sf_mul(r, p));
	p = sf_add(EXP2_C2, sf_mul(r, p));
	p = sf_add(EXP2_C1, sf_mul(r, p));
	p = sf_add(EXP2_C0, sf_mul(r, p));
	return p;
}

/* 2^x on a bit pattern. */
static U32 ieee_exp2(U32 a)
{
	int sign = SIGN_OF(a);
	U32 fn, r, g;
	int n;

	if (sf_is_nan(a))
		return QNAN;
	if (EXP_OF(a) == 0xFF)                    /* +/-inf */
		return sign ? 0 : a;              /* 2^-inf = 0, 2^+inf = +inf */
	if (!sign && sf_cmp(a, (U32)0x43000000) >= 0)        /* x >= 128 */
		return sf_inf(0);
	if (sign && sf_cmp(a & ABS_MASK, (U32)0x43160000) >= 0) /* x <= -150 */
		return 0;

	/* n = nearest int to x; r = x - n in [-0.5, 0.5] */
	fn = sf_bits(sf_round(sf_frombits(a)));
	n  = (int)sf_to_int(fn);
	r  = sf_sub(a, fn);
	g  = sf_exp2_frac(r);                     /* 2^r in [~0.707, 1.414] */
	return sf_scalbn(g, n);
}

/* log2(x) on a bit pattern. */
static U32 ieee_log2(U32 a)
{
	int sign = SIGN_OF(a);
	int e;
	U32 m, s, s2, p, logm;

	if (sf_is_nan(a))
		return QNAN;
	if (sign && (a & ABS_MASK) != 0)          /* x < 0 */
		return QNAN;
	if ((a & ABS_MASK) == 0)                  /* +/-0 -> -inf */
		return NINF;
	if (EXP_OF(a) == 0xFF)                     /* +inf -> +inf */
		return a;

	/* x = 2^e * m, m in [1,2); recentre m to [sqrt(1/2), sqrt(2)). */
	e = EXP_OF(a) - 127;
	m = (a & MANT_MASK) | ((U32)127 << 23);
	if (sf_cmp(m, SQRT2_F) >= 0) {
		m = sf_scalbn(m, -1);
		e++;
	}
	/* s = (m-1)/(m+1); log(m) = 2*s*(1 + s2/3 + s2^2/5 + ...). */
	s  = sf_div(sf_sub(m, ONE_F), sf_add(m, ONE_F));
	s2 = sf_mul(s, s);
	p = LOG_C4;
	p = sf_add(LOG_C3, sf_mul(s2, p));
	p = sf_add(LOG_C2, sf_mul(s2, p));
	p = sf_add(LOG_C1, sf_mul(s2, p));
	p = sf_add(LOG_C0, sf_mul(s2, p));
	logm = sf_mul(sf_mul(TWO_F, s), p);       /* natural log(m) */
	logm = sf_mul(logm, INVLN2_F);            /* -> log2(m) */
	return sf_add(sf_from_int(e), logm);
}

/* Parity of `a` as an integer: -1 = not an integer, 0 = even, 1 = odd. */
static int sf_int_parity(U32 a)
{
	int e = EXP_OF(a) - 127;
	if (EXP_OF(a) == 0xFF)
		return -1;                        /* inf/nan: not a finite integer */
	if ((a & ABS_MASK) == 0)
		return 0;                         /* 0 is even */
	if (e < 0)
		return -1;                        /* 0 < |x| < 1 */
	if (e >= 24)
		return 0;                         /* |x| >= 2^24: integral and even */
	if (FRAC_OF(a) & (MANT_MASK >> e))
		return -1;                        /* has fractional bits */
	return (int)(((FRAC_OF(a) | IMPLICIT) >> (23 - e)) & 1);
}

float sf_exp2f(float x) { return sf_frombits(ieee_exp2(sf_bits(x))); }
float sf_log2f(float x) { return sf_frombits(ieee_log2(sf_bits(x))); }

/* e^x = 2^(x*log2(e)); ln(x) = log2(x)*ln2. */
float sf_expf(float x) { return sf_frombits(ieee_exp2(sf_mul(sf_bits(x), INVLN2_F))); }
float sf_logf(float x) { return sf_frombits(sf_mul(ieee_log2(sf_bits(x)), LN2_F)); }

/* x^y = 2^(y * log2(x)), with integer-exponent and negative-base handling. */
float sf_powf(float x, float y)
{
	U32 ax = sf_bits(x), ay = sf_bits(y);
	int xsign = SIGN_OF(ax);
	int parity;
	U32 mag;

	if ((ay & ABS_MASK) == 0)                 /* x^0 = 1 (incl nan, per C) */
		return sf_frombits(ONE_F);
	if (ax == ONE_F)                          /* 1^y = 1 */
		return sf_frombits(ONE_F);
	if (sf_is_nan(ax) || sf_is_nan(ay))
		return sf_frombits(QNAN);

	parity = sf_int_parity(ay);

	if ((ax & ABS_MASK) == 0) {               /* x == 0 */
		if (SIGN_OF(ay))                  /* 0^negative = +inf */
			return sf_frombits(sf_inf(0));
		return sf_frombits(0);            /* 0^positive = +0 */
	}

	/* Exact integer-exponent fast path (binary exponentiation).  Keeps
	 * x^n exact for the common cases the exp2/log2 round-trip would blur
	 * (1eN parsing, round(x,n), integer powers) and gets the sign of a
	 * negative base right for free.  Bounded to avoid runaway iteration;
	 * |y| > 64 falls through to the general path (overflows anyway). */
	if (parity >= 0) {
		int ye = (int)sf_to_int(ay & ABS_MASK);
		if (ye <= 64) {
			U32 base = ax;
			U32 acc = ONE_F;
			while (ye) {
				if (ye & 1)
					acc = sf_mul(acc, base);
				base = sf_mul(base, base);
				ye >>= 1;
			}
			if (SIGN_OF(ay))         /* negative exponent -> reciprocal */
				acc = sf_div(ONE_F, acc);
			return sf_frombits(acc);
		}
	}

	if (xsign) {                              /* x < 0, non-integer exponent */
		if (parity < 0)
			return sf_frombits(QNAN);
		mag = ieee_exp2(sf_mul(ay, ieee_log2(ax & ABS_MASK)));
		if (parity == 1)                  /* odd power keeps the sign */
			mag |= SIGN_BIT;
		return sf_frombits(mag);
	}
	return sf_frombits(ieee_exp2(sf_mul(ay, ieee_log2(ax))));   /* x > 0 */
}

/* ======================================================================
 * sqrt + trig/inverse-trig + frexp/ldexp/modf (for the MicroPython `math`
 * module, §5b).
 *
 * sqrt is the fdlibm bitwise restoring algorithm — one result bit per
 * iteration over 32-bit integers only, CORRECTLY ROUNDED (nearest-even).
 *
 * sin/cos use the Cephes single-precision scheme: octant reduction with a
 * 3-part pi/4 split (DP1 has 8 significand bits, so y*DP1 is an EXACT
 * binary32 product for y < 2^16 — accuracy degrades gradually for
 * |x| > ~51471, which we accept and document) + minimax kernels on
 * [-pi/4, pi/4].  tan = sin/cos (one extra rounding).  atan is the Cephes
 * two-threshold reduction (tan pi/8 / tan 3pi/8) + 4-term minimax; asin and
 * acos derive from atan + sqrt via identities that avoid the 1-x^2
 * cancellation (asin uses (1-x)(1+x); acos uses 2*atan(sqrt((1-x)/(1+x))),
 * exact at both endpoints).  Accuracy target, like the exp2/log2 family
 * above, is a few ulps — NOT correctly rounded (except sqrt).
 * ====================================================================== */

#define FOPI_F     ((U32)0x3FA2F983)   /* 4/pi */
/* pi/4 split into 8-bit chunks: y*DPn_F is an EXACT binary32 product for
 * every y < 2^16 (8+16 = 24 significand bits), so the reduction chain only
 * rounds at the final DP4 term.  (The classic Cephes 3-part split has 11
 * bits in its second term — exact only to y < 2^13, which showed up as
 * 8-ulp cos errors near |x| ~ 28000 in the host harness.) */
#define DP1_F      ((U32)0x3F490000)   /* 0.78515625        (8 bits) */
#define DP2_F      ((U32)0x397D0000)   /* 2.41279602e-4     (8 bits) */
#define DP3_F      ((U32)0x352A0000)   /* 6.33299351e-7     (7 bits) */
#define DP4_F      ((U32)0x30085A30)   /* 4.96046759e-10 (full, rounds) */
#define PI_F       ((U32)0x40490FDB)
#define PIO2_F     ((U32)0x3FC90FDB)
#define PIO4_F     ((U32)0x3F490FDB)
#define PI3O4_F    ((U32)0x4016CBE4)   /* 3*pi/4 */

/* sin kernel coefficients (Cephes): x + x*z*((S2*z + S1)*z + S0), z = x^2 */
#define SINC0      ((U32)0xBE2AAAA3)   /* -1.6666654611e-1 */
#define SINC1      ((U32)0x3C08839E)   /*  8.3321608736e-3 */
#define SINC2      ((U32)0xB94CA1F9)   /* -1.9515295891e-4 */

/* cos kernel: 1 - z/2 + z^2*((C2*z + C1)*z + C0) */
#define COSC0      ((U32)0x3D2AAAA5)   /*  4.166664568298827e-2 */
#define COSC1      ((U32)0xBAB6061A)   /* -1.388731625493765e-3 */
#define COSC2      ((U32)0x37CCF5CE)   /*  2.443315711809948e-5 */

/* atan reduction thresholds + kernel coefficients (Cephes) */
#define TAN3PI8_F  ((U32)0x401A827A)   /* 2.414213562373095 = tan(3pi/8) */
#define TANPI8_F   ((U32)0x3ED413CD)   /* 0.4142135623730950 = tan(pi/8) */
#define ATC0       ((U32)0x3DA4F0D1)   /*  8.05374449538e-2 */
#define ATC1       ((U32)0xBE0E1B85)   /* -1.38776856032e-1 */
#define ATC2       ((U32)0x3E4C925F)   /*  1.99777106478e-1 */
#define ATC3       ((U32)0xBEAAAA2A)   /* -3.33329491539e-1 */

int sf_isfinite(float x) { return EXP_OF(sf_bits(x)) != 0xFF; }

/* Correctly-rounded sqrt (fdlibm e_sqrtf shape, U32-only). */
float sf_sqrt(float xf)
{
	U32 a = sf_bits(xf);
	U32 ix, q, s, r, t;
	int m;

	if (sf_is_nan(a))
		return sf_frombits(QNAN);
	if ((a & ABS_MASK) == 0)
		return xf;                        /* +-0 -> itself */
	if (SIGN_OF(a))
		return sf_frombits(QNAN);         /* x < 0 */
	if (EXP_OF(a) == 0xFF)
		return xf;                        /* +inf */
	if (EXP_OF(a) == 0)
		return sf_frombits(0);            /* subnormal: house flush-to-zero */

	m  = EXP_OF(a) - 127;
	ix = FRAC_OF(a) | IMPLICIT;               /* 1.f at bits [23:0] */
	if (m & 1)
		ix += ix;                         /* odd exponent: double mantissa */
	m >>= 1;                                  /* floor(m/2); m may be negative */

	/* One result bit per iteration; remainder stays < 2^27 so no overflow. */
	ix += ix;
	q = 0; s = 0; r = (U32)0x01000000;
	while (r != 0) {
		t = s + r;
		if (t <= ix) {
			s = t + r;
			ix -= t;
			q += r;
		}
		ix += ix;
		r >>= 1;
	}
	if (ix != 0)
		q += (q & 1);                     /* round to nearest (even) */
	ix = (q >> 1) + ((U32)0x3F000000);
	ix += ((U32)(S32)m << 23);
	return sf_frombits(ix);
}

/* Octant reduction: |x| -> r in [-pi/4, pi/4] and octant j in 0..7.
 * ax = |x| bit pattern, finite. */
static U32 sf_trig_reduce(U32 ax, int *octant)
{
	U32 y, r;
	S32 j;

	j = sf_to_int(sf_mul(ax, FOPI_F));        /* truncate; ax >= 0 */
	y = sf_from_int(j);
	if (j & 1) {                              /* round j up to even */
		j++;
		y = sf_add(y, ONE_F);
	}
	*octant = (int)(j & 7);
	r = sf_sub(ax, sf_mul(y, DP1_F));         /* exact products for y < 2^16 */
	r = sf_sub(r, sf_mul(y, DP2_F));
	r = sf_sub(r, sf_mul(y, DP3_F));
	r = sf_sub(r, sf_mul(y, DP4_F));
	return r;
}

/* sin kernel on the reduced argument (bit pattern), z = r^2. */
static U32 sf_sin_kern(U32 r, U32 z)
{
	U32 p = sf_add(SINC1, sf_mul(z, SINC2));
	p = sf_add(SINC0, sf_mul(z, p));
	return sf_add(r, sf_mul(sf_mul(r, z), p));
}

/* cos kernel, z = r^2. */
static U32 sf_cos_kern(U32 z)
{
	U32 p = sf_add(COSC1, sf_mul(z, COSC2));
	p = sf_add(COSC0, sf_mul(z, p));
	p = sf_add(ONE_F, sf_mul(sf_mul(z, z), p));   /* 1 + z^2*poly */
	return sf_sub(p, sf_mul(HALF_F, z));          /* - z/2 */
}

float sf_sin(float xf)
{
	U32 a = sf_bits(xf);
	U32 ax = a & ABS_MASK, r, z, p;
	int sign = SIGN_OF(a), j;

	if (EXP_OF(ax) == 0xFF)
		return sf_frombits(QNAN);         /* sin(inf/nan) = nan */
	if (ax == 0)
		return xf;                        /* sin(+-0) = +-0 */
	r = sf_trig_reduce(ax, &j);
	if (j > 3) {
		sign ^= 1;
		j -= 4;
	}
	z = sf_mul(r, r);
	p = (j == 1 || j == 2) ? sf_cos_kern(z) : sf_sin_kern(r, z);
	if (sign)
		p ^= SIGN_BIT;
	return sf_frombits(p);
}

float sf_cos(float xf)
{
	U32 a = sf_bits(xf);
	U32 ax = a & ABS_MASK, r, z, p;
	int sign = 0, j;

	if (EXP_OF(ax) == 0xFF)
		return sf_frombits(QNAN);
	r = sf_trig_reduce(ax, &j);
	if (j > 3) {
		sign ^= 1;
		j -= 4;
	}
	if (j > 1)
		sign ^= 1;
	z = sf_mul(r, r);
	p = (j == 1 || j == 2) ? sf_sin_kern(r, z) : sf_cos_kern(z);
	if (sign)
		p ^= SIGN_BIT;
	return sf_frombits(p);
}

float sf_tan(float xf)
{
	U32 a = sf_bits(xf);
	if (EXP_OF(a) == 0xFF)
		return sf_frombits(QNAN);         /* tan(inf/nan) = nan */
	return sf_frombits(sf_div(sf_bits(sf_sin(xf)), sf_bits(sf_cos(xf))));
}

float sf_atan(float xf)
{
	U32 a = sf_bits(xf);
	U32 x = a & ABS_MASK, y, z, p;
	int sign = SIGN_OF(a);

	if (sf_is_nan(a))
		return sf_frombits(QNAN);
	if (x == 0)
		return xf;                        /* atan(+-0) = +-0 */
	if (sf_cmp(x, TAN3PI8_F) > 0) {           /* incl. atan(inf) -> pi/2 */
		y = PIO2_F;
		x = sf_div(ONE_F, x) ^ SIGN_BIT;  /* -(1/x) */
	} else if (sf_cmp(x, TANPI8_F) > 0) {
		y = PIO4_F;
		x = sf_div(sf_sub(x, ONE_F), sf_add(x, ONE_F));
	} else {
		y = 0;
	}
	z = sf_mul(x, x);
	p = sf_add(ATC1, sf_mul(z, ATC0));
	p = sf_add(ATC2, sf_mul(z, p));
	p = sf_add(ATC3, sf_mul(z, p));
	p = sf_add(sf_mul(sf_mul(z, x), p), x);   /* z*x*poly + x */
	y = sf_add(y, p);
	if (sign)
		y ^= SIGN_BIT;
	return sf_frombits(y);
}

/* IEEE-conformant atan2 (matches CPython on zero/inf combinations). */
float sf_atan2(float yf, float xf)
{
	U32 ay = sf_bits(yf), ax = sf_bits(xf);
	int sy = SIGN_OF(ay), sx = SIGN_OF(ax);
	U32 r;

	if (sf_is_nan(ay) || sf_is_nan(ax))
		return sf_frombits(QNAN);
	if (EXP_OF(ax) == 0xFF) {                 /* x = +-inf */
		if (EXP_OF(ay) == 0xFF)
			r = sx ? PI3O4_F : PIO4_F;
		else
			r = sx ? PI_F : 0;
	} else if (EXP_OF(ay) == 0xFF) {          /* y = +-inf, x finite */
		r = PIO2_F;
	} else if ((ay & ABS_MASK) == 0) {        /* y = +-0 */
		r = sx ? PI_F : 0;
	} else if ((ax & ABS_MASK) == 0) {        /* x = +-0, y nonzero */
		r = PIO2_F;
	} else {
		r = sf_bits(sf_atan(sf_frombits(sf_div(ay, ax))));
		if (sx)                           /* x < 0: shift into (pi/2, pi] */
			r = sy ? sf_sub(r, PI_F) : sf_add(r, PI_F);
		return sf_frombits(r);            /* sign already correct */
	}
	if (sy)
		r ^= SIGN_BIT;                    /* special cases take y's sign */
	return sf_frombits(r);
}

float sf_asin(float xf)
{
	U32 a = sf_bits(xf);
	U32 x = a & ABS_MASK, t, r;
	int sign = SIGN_OF(a);

	if (sf_is_nan(a))
		return sf_frombits(QNAN);
	if (sf_cmp(x, ONE_F) > 0)
		return sf_frombits(QNAN);         /* |x| > 1: domain error */
	if (x == 0)
		return xf;                        /* asin(+-0) = +-0 */
	if (x == ONE_F) {
		r = PIO2_F;
	} else {
		/* asin(x) = atan(x / sqrt((1-x)(1+x))) — the factored form
		 * avoids the 1 - x^2 cancellation near |x| = 1. */
		t = sf_mul(sf_sub(ONE_F, x), sf_add(ONE_F, x));
		t = sf_bits(sf_sqrt(sf_frombits(t)));
		r = sf_bits(sf_atan(sf_frombits(sf_div(x, t))));
	}
	if (sign)
		r ^= SIGN_BIT;
	return sf_frombits(r);
}

float sf_acos(float xf)
{
	U32 a = sf_bits(xf);
	U32 t;

	if (sf_is_nan(a))
		return sf_frombits(QNAN);
	if (sf_cmp(a & ABS_MASK, ONE_F) > 0)
		return sf_frombits(QNAN);
	/* acos(x) = 2*atan(sqrt((1-x)/(1+x))): exact at x=1 (-> +0) and
	 * x=-1 (1-x)/(1+x) = 2/+0 = +inf, atan(inf) = pi/2 -> pi. */
	t = sf_div(sf_sub(ONE_F, a), sf_add(ONE_F, a));
	t = sf_bits(sf_sqrt(sf_frombits(t)));
	t = sf_bits(sf_atan(sf_frombits(t)));
	return sf_frombits(sf_mul(TWO_F, t));
}

/* frexp: x = m * 2^e with m in [0.5, 1).  Zero/inf/nan: *e = 0, return x. */
float sf_frexp(float xf, int *e)
{
	U32 a = sf_bits(xf);

	*e = 0;
	if (EXP_OF(a) == 0xFF || (a & ABS_MASK) == 0)
		return xf;
	if (EXP_OF(a) == 0)                       /* subnormal: house flush */
		return sf_frombits(a & SIGN_BIT);
	*e = EXP_OF(a) - 126;
	return sf_frombits((a & ~(EXP_MASK << 23)) | ((U32)126 << 23));
}

/* ldexp: x * 2^n with overflow -> signed inf, underflow -> signed zero. */
float sf_ldexp(float xf, int n)
{
	U32 a = sf_bits(xf);

	if (EXP_OF(a) == 0xFF || (a & ABS_MASK) == 0)
		return xf;
	if (EXP_OF(a) == 0)
		return sf_frombits(a & SIGN_BIT);
	if (n > 280)                              /* clamp so EXP_OF + n fits int */
		n = 280;
	if (n < -280)
		n = -280;
	return sf_frombits(sf_scalbn(a, n));
}

/* modf: *iptr = integral part (trunc), returns the fractional part; both
 * carry x's sign (modf(-2.0) = (-0.0, -2.0), as CPython prints). */
float sf_modf(float xf, float *iptr)
{
	U32 a = sf_bits(xf);
	U32 t, frac;

	if (sf_is_nan(a)) {
		*iptr = xf;
		return xf;
	}
	if (EXP_OF(a) == 0xFF) {                  /* +-inf: frac = +-0 */
		*iptr = xf;
		return sf_frombits(a & SIGN_BIT);
	}
	t = sf_bits(sf_trunc(xf));
	*iptr = sf_frombits(t);
	frac = sf_sub(a, t);
	if ((frac & ABS_MASK) == 0)               /* keep x's sign on a zero frac */
		frac = a & SIGN_BIT;
	return sf_frombits(frac);
}

#ifndef SF_HOST
/* Real bare-libm-named functions.  MicroPython's modmath.c passes these BY
 * FUNCTION POINTER (math_generic_1(x, sqrtf) — a function-like macro does
 * not expand without a following '('), so the <math.h> macro mappings are
 * not enough: the SYMBOLS must exist.  Host builds omit them (they would
 * collide with libm, which the host ulp-harness compares against). */
float sqrtf(float x)           { return sf_sqrt(x); }
float sinf(float x)            { return sf_sin(x); }
float cosf(float x)            { return sf_cos(x); }
float tanf(float x)            { return sf_tan(x); }
float asinf(float x)           { return sf_asin(x); }
float acosf(float x)           { return sf_acos(x); }
float atanf(float x)           { return sf_atan(x); }
float atan2f(float y, float x) { return sf_atan2(y, x); }
float expf(float x)            { return sf_expf(x); }
float powf(float x, float y)   { return sf_powf(x, y); }
float fmodf(float x, float y)  { return sf_fmod(x, y); }
#endif
