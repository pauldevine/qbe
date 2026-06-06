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
