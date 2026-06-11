/* float_cmp_cx_probe.c — soft-float compare/convert result clobbered when
 * rega places it in CX (§4x).
 *
 * The i8086 Ocmps (soft-float compare) and Ostosi/Ostoui handlers lower to a
 * `call far _sf_cmp` / `_sf_to_int` at EMIT time and bracket the caller-save
 * scratch with push/pop.  AX and DX were skipped when they were the
 * destination, but CX was pushed/popped UNCONDITIONALLY — so a compare result
 * rega assigned to CX was stored by store_ax_to and then immediately
 * overwritten by `pop cx` with the stale pre-compare value.
 *
 * MicroPython surface (real Victor): objfloat.c MP_BINARY_OP_MODULO's
 * sign-fix `(lhs < 0) != (rhs < 0)` read garbage and fired on positive
 * operands (7.5 % 2.0 -> 3.5), and MP_UNARY_OP_BOOL's `val != 0` made
 * bool(0.0) -> True.  This probe replicates that modulo dance verbatim.
 *
 * CAVEAT (the §4r/§4t lesson): the trigger is rega-dependent — a green probe
 * is necessary-not-sufficient; the real guard is the dst_in_cx skip in
 * i8086/emit.c plus the MicroPython float run on Victor.  Verified bug-loud
 * against the unfixed emit at commit time (m1 printed 40600000 = 3.5).
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

/* objfloat.c MP_BINARY_OP_MODULO, verbatim shape */
float mod_dance(float lhs, float rhs)
{
    lhs = fmodf(lhs, rhs);
    if (lhs == 0.0f) {
        lhs = copysignf(0.0f, rhs);
    } else {
        if ((lhs < 0.0f) != (rhs < 0.0f)) {
            lhs += rhs;
        }
    }
    return lhs;
}

int main(void)
{
    float z = fid(0.0f);
    float h = fid(0.5f);

    printf("m1 %lx\n", bits(mod_dance(fid(7.5f), fid(2.0f))));   /* 1.5f  3fc00000 */
    printf("m2 %lx\n", bits(mod_dance(fid(-7.5f), fid(2.0f))));  /* 0.5f  3f000000 */
    printf("m3 %lx\n", bits(mod_dance(fid(7.5f), fid(-2.0f))));  /* -0.5f bf000000 */
    printf("m4 %lx\n", bits(mod_dance(fid(4.0f), fid(2.0f))));   /* 0.0f  0 */
    printf("m5 %lx\n", bits(mod_dance(fid(4.0f), fid(-2.0f))));  /* -0.0f 80000000 */
    /* the bool(0.0) shape: compare result feeding a value use */
    printf("b0 %d %d\n", z != 0, h != 0);                        /* 0 1 */
    /* float->int conversion result under the same bracket */
    printf("c0 %d\n", (int)fid(3.75f) * 100 + (int)fid(-2.5f));  /* 298 */
    printf("DONE\n");
    return 0;
}
