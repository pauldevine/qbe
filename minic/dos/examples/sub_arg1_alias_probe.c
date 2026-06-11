/* sub_arg1_alias_probe.c — Osub Kw two-address rescue when the destination
 * register aliases arg[1] ([[i8086-two-addr-arg1-alias]] non-commutative
 * sibling, §4t).
 *
 * emit.c's generic two-address synthesis ("sub %=, %1") can't swap operands
 * for a non-commutative op, so when rega gives the result the same register
 * as arg[1] it "rescues" arg[1] through a scratch.  The scratch was
 * HARDCODED to BX: when the destination itself was BX the save was a no-op
 * `mov bx, bx`, the dst-mov clobbered the operand, and the trailing
 * `pop bx` discarded the result — `right_pad -= p` compiled to a no-op and
 * MicroPython's mp_print_strn right-pad loop ("%-5d" % 7) spun forever.
 * A second hole: when arg[0] was BX, `mov bx, <arg1>` clobbered arg0
 * before it was read.
 *
 * The pad_out() loop below recreates the mp_print_strn shape: a loop-carried
 * counter decremented by a clamped chunk across an indirect call, under
 * enough register pressure that the sub's operands and result live in
 * registers.  The guard bounds the loop so a regression prints wrong sums
 * instead of hanging the gate.  CAVEAT (same as shift_count_spill_probe):
 * the trigger is register-allocation-dependent, so a green probe is
 * necessary-not-sufficient — the real guard is the scratch-selection logic
 * in emit.c plus the Victor-side %-format run.
 */
#include <stdio.h>

typedef int (*emit_fn)(int);

static int g_emitted;
static int g_calls;

static int emit_n(int n) {
    g_emitted += n;
    g_calls++;
    return n;
}

static emit_fn ep;

/* mp_print_strn right-pad shape: total/right_pad/pad_size/p all live across
 * an indirect call; right_pad -= p is the aliased sub. */
int pad_out(int right_pad, int pad_size) {
    int total = 0;
    int guard = 0;
    while (right_pad > 0) {
        int p = right_pad;
        if (p > pad_size) {
            p = pad_size;
        }
        ep(p);
        total += p;
        right_pad -= p;
        guard++;
        if (guard > 40) {
            return -1;
        }
    }
    return total;
}

/* same shape with two extra live loop-carried values, shifting rega's
 * choice of the sub's destination register across variants. */
int pad_out2(int right_pad, int pad_size) {
    int total = 0;
    int guard = 0;
    int mix = 0;
    int acc = 0;
    while (right_pad > 0) {
        int p = right_pad;
        if (p > pad_size) {
            p = pad_size;
        }
        ep(p);
        total += p;
        right_pad -= p;
        mix ^= right_pad;
        acc += mix + guard;
        guard++;
        if (guard > 40) {
            return -1;
        }
    }
    return total * 1000 + acc * 10 + mix;
}

/* x = a - x with the result flowing back into x's register. */
int sub_back(int a, int x) {
    x = a - x;
    return x + g_calls;
}

/* chain of subs under pressure so several different regs host a dest. */
int sub_chain(int a, int b, int c, int d) {
    int u = a - b;
    int v = c - u;
    int w = d - v;
    int z = u - w;
    return z * 100 + v;
}

int main(void) {
    int t;

    ep = emit_n;
    g_emitted = 0;
    g_calls = 0;
    t = pad_out(9, 4);
    /* chunks 4,4,1 */
    printf("ok1 %d\n", t == 9 && g_emitted == 9 && g_calls == 3);

    g_emitted = 0;
    g_calls = 0;
    t = pad_out(4, 16);
    printf("ok2 %d\n", t == 4 && g_emitted == 4 && g_calls == 1);

    g_emitted = 0;
    g_calls = 0;
    t = pad_out(0, 4);
    printf("ok3 %d\n", t == 0 && g_calls == 0);

    g_calls = 0;
    printf("ok4 %d\n", sub_back(10, 3) == 7);
    printf("ok5 %d\n", sub_chain(50, 8, 30, 100) == -7012);

    /* pad_out2 is the variant whose sub dest rega places in BX — the
     * bug-loud case (compiled to a no-op sub before the fix). */
    g_emitted = 0;
    g_calls = 0;
    t = pad_out2(9, 4);
    printf("ok6 %d\n", t == 9164 && g_emitted == 9 && g_calls == 3);

    g_emitted = 0;
    g_calls = 0;
    t = pad_out2(4, 16);
    printf("ok7 %d\n", t == 4000 && g_emitted == 4 && g_calls == 1);

    g_emitted = 0;
    g_calls = 0;
    t = pad_out2(20, 7);
    printf("ok8 %d\n", t == 20391 && g_emitted == 20 && g_calls == 3);

    printf("done\n");
    return 0;
}
