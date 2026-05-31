/*
 * divreg_probe.c -- regression gate for the i8086 div/mul AX:DX clobber.
 *
 * 8086 idiv/imul/div are fixed-register ops: the dividend goes in AX, the
 * DX:AX pair is the implicit dividend/result, and DX is clobbered.  The
 * i8086 backend emits them in-place (it does NOT precolor TMP(RAX)/TMP(RDX)
 * in isel the way amd64 does), so rega was unaware AX/DX are clobbered and
 * could keep a value live ACROSS such an op in AX or DX -- where the next
 * div/mul silently destroyed it.
 *
 * The canonical trigger (found in MicroPython's gc_setup_area): an OUTER
 * division whose divisor/dividend subexpression itself contains a nested
 * div or mul.  The outer dividend, computed first, must survive the inner
 * div/mul; rega put it in AX and the inner idiv zeroed it, so
 *   (24576-1) / (1 + 8/2 * (4*4)) = 24575/65 = 378  came out 0.
 *
 * Fix: T.divclob (i8086 = AX|DX) makes spill.c steer temps live across
 * div/mul/rem away from AX/DX, plus an emit.c BX-staging backstop for a
 * divisor that still lands in AX/DX under pressure.  Without the fix this
 * probe prints garbage (t1=0 etc.); with it, the values below.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/divreg_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/divreg_probe/divreg_probe.exe \
 *             | diff - minic/dos/tests/divreg_probe.golden.txt
 * Wired into tools/test-dos.sh RUNTIME_TESTS (medium).
 */

/* inputs via globals so nothing folds to a compile-time constant */
unsigned int gu_tot = 24576;
unsigned int gu_a = 8;
unsigned int gu_b = 2;
unsigned int gu_c = 4;
unsigned int gu_d = 4;
int gs_x = -1000;
int gs_y = 7;
int gs_z = 3;

int main(void) {
    /* gc_setup_area pattern: nested div(8/2) + mul in the OUTER divisor */
    unsigned int t1 = (gu_tot - 1) / (1 + (gu_a / gu_b) * (gu_c * gu_d)); /* 24575/65 = 378 */
    /* nested div in the dividend, then a mul, then divide */
    unsigned int t2 = ((gu_tot / gu_c) - 1) * 3 / (gu_a / gu_b);          /* 18429/4 = 4607 */
    /* unsigned remainder whose dividend carries a mul across the % */
    unsigned int t3 = (gu_tot * 1u + 7) % (gu_a / gu_b + 1);              /* 24583 % 5 = 3 */
    /* signed div whose divisor is a nested signed div */
    int t4 = (gs_x - 1) / (gs_y / gs_z);                                 /* -1001/2 = -500 */
    /* signed remainder with a mul in the dividend */
    int t5 = (gs_x * 2) % (gs_y - gs_z);                                 /* -2000 % 4 = 0 */
    printf("t1=%u t2=%u t3=%u t4=%d t5=%d\n", t1, t2, t3, t4, t5);
    return 0;
}
