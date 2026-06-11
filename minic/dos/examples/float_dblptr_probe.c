/* float_dblptr_probe.c — the FLOAT-flag / FAR-flag type-encoding collision
 * at two pointer levels (§5c, the §5b sibling).
 *
 * minic encodes a type as a word of 3-bit KIND levels (IDIR/FUNC shift the
 * whole word up 3) with qualifier flags at fixed high positions.  FLOAT was
 * bit 18 and FAR bit 24: two encoding shifts (float ** = IDIR(IDIR(...)),
 * or IDIR(FUNC(...)) for a float-returning fn ptr) land FLOAT exactly on
 * FAR, and DREF strips ~FAR — so `**pp` on a `float **` lost the FLOAT bit
 * AND read the pointer itself as far: under medium the deref emitted
 * loadl + loadfw + a bogus swtof (16-bit int load converted to float);
 * under compact the inner deref lost its float-ness the same way.  §5b
 * fixed only the fn-ptr RETURN case via the fpproto rett side table.
 *
 * The fix relocates FAR 24 -> 26 and QVOLATILE 25 -> 27, so FLOAT survives
 * two levels clean.  Costs traded (all surveyed unconsumed 2026-06-11):
 * far-data nested-far depth drops to ONE level (the innermost FAR of a
 * T*** overflows bit 32); `unsigned T ***` (UNSIGNED bit 17 + 9 = 26) and
 * `float ***` (18 + 9 = 27) are the residual collision classes.  SHORT***
 * (16 + 9 = 25, previously polluted QVOLATILE) becomes clean.
 *
 * Bug-loud: pre-fix minic emits a misdecoded deref chain — f1 prints a
 * swtof'd garbage pattern instead of 3fc00000, and the **pp store corrupts
 * the pointer instead of the pointee.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/float_dblptr_probe.c --softfloat
 * Verify: tools/run-dos-exe.sh build/examples/float_dblptr_probe/float_dblptr_probe.exe \
 *             | diff - minic/dos/tests/float_dblptr_probe.golden.txt
 */
#include <stdio.h>

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

static float g = 1.5f;
static float *gp;

/* identity through a call so QBE cannot fold the pointer chain away */
static float **ppid(float **pp)
{
    return pp;
}

/* float* returned through a FUNCTION POINTER: IDIR(FUNC(IDIR(float))) puts
 * FLOAT three shifts up; the fpproto rett side table (§5b) must keep the
 * pointee float across the indirect call regardless of the bit layout. */
static float *getgp(void)
{
    return gp;
}

/* double-deref READ + WRITE through a float** PARAMETER (the param type
 * crosses the declarator and call-arg coercion paths too) */
static u32 read_dd(float **pp)
{
    return bits(**pp);
}

static void write_dd(float **pp, float v)
{
    **pp = v;
}

int main(void)
{
    float x;
    float *p;
    float **pp;
    float *(*fp)(void);
    u32 r;

    /* f1: read through float** — pre-fix this loads 16 bits and swtofs */
    x = 1.5f;
    p = &x;
    pp = &p;
    r = bits(**pp);
    printf("f1=%lx (want 3fc00000)\r\n", r);

    /* f2: write through float** then read the underlying object */
    **pp = 2.5f;
    printf("f2=%lx (want 40200000)\r\n", bits(x));

    /* f3: the chain opaqued through a call (no folding) */
    r = bits(**ppid(pp));
    printf("f3=%lx (want 40200000)\r\n", r);

    /* f4/f5: float** as a parameter, read and write */
    x = -4.0f;
    printf("f4=%lx (want c0800000)\r\n", read_dd(pp));
    write_dd(pp, 0.0625f);
    printf("f5=%lx (want 3d800000)\r\n", bits(x));

    /* f6: float* through a fn POINTER (rett side table), then deref */
    gp = &g;
    fp = getgp;
    r = bits(*fp());
    printf("f6=%lx (want 3fc00000)\r\n", r);
    *fp() = 6.5f;
    printf("f7=%lx (want 40d00000)\r\n", bits(g));

    /* s1/s2: short** ride-along — pins SHORT two encoding levels up plus
     * the ONE supported level of nested far pointer under far-data.
     * Deliberately NOT short***: under compact/large/huge a third pointer
     * level pushes the innermost FAR bit past bit 31 (the documented §5c
     * trade — the old layout had two nested-far levels, the new one buys
     * back the float** miscompile instead).  No in-tree consumer uses
     * T*** under far-data (surveyed 2026-06-11). */
    {
        short sv;
        short *sp;
        short **spp;
        sv = -123;
        sp = &sv;
        spp = &sp;
        printf("s1=%d (want -123)\r\n", (int)**spp);
        **spp = 321;
        printf("s2=%d (want 321)\r\n", (int)sv);
    }

    return 0;
}
