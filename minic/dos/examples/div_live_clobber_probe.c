/* div_live_clobber_probe.c — Kw div/rem AX/DX liveness brackets (§4y).
 *
 * The 16-bit Odiv/Orem/Oudiv/Ourem handlers expand to `mov ax, dividend;
 * cwd|xor dx,dx; idiv|div r` — AX and DX are both implicitly clobbered
 * (dividend staging, sign/zero extension, quotient+remainder), and rega
 * does not model it.  A live temp parked in AX or DX across the division
 * was silently destroyed — the §1h found-not-fixed "two divisions feeding
 * one call corrupt the first result" bug.  The §4y emit-bracket audit
 * found 21 such sites in the MicroPython image (mp_format_mantissa,
 * mp_map_lookup, mp_lexer_to_next, ...).  Fixed with liveness-gated,
 * dest-skipped push/pop brackets, the same discipline as the Kl helper-
 * call path.
 *
 * CAVEAT (the §4r/§4t/§4x lesson): the trigger is rega-dependent — a green
 * probe is necessary-not-sufficient; the real guard is the bracket in
 * i8086/emit.c plus tools/run-emit-audit.sh staying clean.
 */
#include <stdio.h>

int iid(int x)
{
    return x;
}

unsigned uid(unsigned x)
{
    return x;
}

int main(void)
{
    int a = iid(12345);
    int b = iid(7);
    unsigned ua = uid(54321u);
    unsigned ub = uid(9u);

    /* two divisions feeding one call: the first result waits in a register
     * while the second division executes */
    printf("s %d %d\n", a / b, a % b);          /* 1763 4 */
    printf("u %u %u\n", ua / ub, ua % ub);      /* 6035 6 */
    printf("3 %d %d %d\n", a / 100, a % 100, a / 1000);  /* 123 45 12 */

    /* digit-extraction loop (the mp_format_mantissa shape): loop-carried
     * values live across repeated div+rem in one expression */
    {
        char buf[8];
        int v = iid(31416);
        int w = 0;
        int n = 0;
        while (v) {
            buf[n] = (char)('0' + v % 10);
            n = n + 1;
            v = v / 10;
            w = w + n;
        }
        printf("dig ");
        while (n) {
            n = n - 1;
            putchar(buf[n]);
        }
        printf(" w %d\n", w);                   /* dig 31416 w 15 */
    }

    /* quotient live across a second unsigned division */
    {
        unsigned q = ua / ub;                   /* 6035 */
        unsigned r = ua % ub;                   /* 6 */
        unsigned q2 = q / r;                    /* 1005 */
        printf("q %u %u %u\n", q, r, q2);
    }

    /* register-pressure case: enough simultaneously-live values across the
     * division that rega must park one in DX (and one in AX) */
    {
        int p = iid(11);
        int q = iid(22);
        int r = iid(33);
        int s = iid(44);
        int t = iid(55);
        int z = a / b;                          /* 1763, p..t live across */
        int y = a % b;                          /* 4 */
        printf("p %d %d %d %d %d %d %d\n",
            p + z, q - z, r + z, s - z, t + z, z, y);
        /* 1774 -1741 1796 -1719 1818 1763 4 */
    }

    printf("DONE\n");
    return 0;
}
