/* float_arg_coerce_probe.c — call-argument int<->float conversion (§4x).
 *
 * C11 6.5.2.2p7: an argument to a prototyped parameter is converted as if by
 * assignment.  minic's coerce_arg used to bail on any float involvement
 * ("a real conversion, not a width fix"), so an INTEGER argument to a FLOAT
 * parameter passed its raw word into the binary32 slot — the callee read a
 * denormal (~1e-44).  MicroPython surface: py/parsenum.c mp_decimal_exp calls
 * powf(5, -dec_exp) with two ints; sf_powf saw powf(eps, eps) ~= 1.0 and the
 * decimal-exponent scaling of EVERY float literal became a silent no-op
 * (1.5 parsed as 7.5 = mantissa 15 with the exponent adjust but no /5).
 *
 * Pins: int Con / int var / negative int / long arg -> float param (swtof,
 * sltof); float arg -> int / long param (stosi, dtosi); both argument
 * positions mixed; the exact parsenum powf shape; int->int regression.
 * Values printed as IEEE-754 bit patterns via a union — bit-exact, no float
 * printf needed.
 */
#include <stdio.h>
#include <math.h>

typedef unsigned long u32;

typedef union {
    float f;
    u32 i;
} cvt;

static u32 bits(float f)
{
    cvt c;
    c.f = f;
    return c.i;
}

float fid(float x)
{
    return x;
}

float fadd2(float a, float b)
{
    return a + b;
}

int iid(int x)
{
    return x;
}

long lid(long x)
{
    return x;
}

int isum(int a, int b)
{
    return a + b;
}

/* The parsenum.c mp_decimal_exp shape: scale by 2^d via the exponent field,
 * then divide by 5^-d — powf is called with two INT arguments. */
static float decimal_exp(float num, int dec_exp)
{
    cvt res;
    u32 e;
    res.f = num;
    e = (res.i >> 23) & 255;
    e += dec_exp;
    res.i = (res.i & 0x807FFFFFUL) | ((e & 255) << 23);
    if (dec_exp < 0)
        res.f /= powf(5, -dec_exp);
    else
        res.f *= powf(5, dec_exp);
    return res.f;
}

int main(void)
{
    int n = 7;
    int neg = -3;
    long m = 100000L;

    printf("con %lx\n", bits(fid(5)));        /* 5.0f      40a00000 */
    printf("var %lx\n", bits(fid(n)));        /* 7.0f      40e00000 */
    printf("neg %lx\n", bits(fid(neg)));      /* -3.0f     c0400000 */
    printf("lng %lx\n", bits(fid(m)));        /* 100000.0f 47c35000 */
    printf("mix1 %lx\n", bits(fadd2(1, 0.5f)));  /* 1.5f   3fc00000 */
    printf("mix2 %lx\n", bits(fadd2(0.25f, 3))); /* 3.25f  40500000 */
    printf("f2i %d\n", iid(3.75f));           /* 3 (trunc toward zero) */
    printf("f2l %ld\n", lid(100000.0f));      /* 100000 */
    printf("pow1 %lx\n", bits(powf(5, 1)));   /* 5.0f      40a00000 */
    printf("pow2 %lx\n", bits(powf(10, 2)));  /* 100.0f    42c80000 */
    printf("dexp1 %lx\n", bits(decimal_exp(15.0f, -1))); /* 1.5f  3fc00000 */
    printf("dexp2 %lx\n", bits(decimal_exp(1.0f, 3)));   /* 1000.0f 447a0000 */
    printf("ii %d\n", isum(2, 3));            /* 5 */
    printf("DONE\n");
    return 0;
}
