/*
 * sigencode_probe.c -- replicates MicroPython's MP_BC_PRELUDE_SIG_ENCODE
 * (py/bc.h) to pin a Kw shift / shift-assign codegen bug found on the real
 * Victor: encoding a MODULE prelude (n_state=2, n_pos_args=0, all else 0)
 * should emit a SINGLE byte 0x08, but the i8086 far-data backend emitted
 * 0x82 0x81 0x00 -- the `while (S|E|F|A|K|D)` loop ran when every operand
 * was already 0, i.e. the `S >>= 4` / `(S & 0xf) << 3` lowering is wrong.
 *
 * Inputs come from globals so nothing folds to a compile-time constant.
 *
 * Build:  tools/build-example.sh --model=compact minic/dos/examples/sigencode_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/sigencode_probe/sigencode_probe.exe \
 *             | diff - minic/dos/tests/sigencode_probe.golden.txt
 */

unsigned char obuf[8];
int on;

/* mirror of the macro inputs for the failing module case -- mp_uint_t is
 * 4 bytes (Kl) under far-data, so use unsigned long to match. */
unsigned long g_S = 2;   /* n_state */
unsigned long g_E = 0;   /* n_exc_stack */
unsigned long g_F = 0;   /* scope_flags */
unsigned long g_A = 0;   /* num_pos_args */
unsigned long g_K = 0;   /* num_kwonly_args */
unsigned long g_D = 0;   /* num_def_pos_args */

void ob(unsigned char v) {
    if (on < 8) {
        obuf[on] = v;
    }
    on++;
}

int main(void) {
    unsigned long S = g_S, E = g_E, F = g_F, A = g_A, K = g_K, D = g_D;
    unsigned char z;
    unsigned z0, sa4;
    int i;

    on = 0;

    S -= 1;
    z = (S & 0xf) << 3 | (E & 1) << 2 | (A & 3);
    z0 = (unsigned)z;          /* expect 0x08 */
    S >>= 4;
    sa4 = S;                   /* expect 0 */
    E >>= 1;
    A >>= 2;

    while (S | E | F | A | K | D) {
        ob(0x80 | z);
        z = (F & 1) << 6 | (S & 3) << 4 | (K & 1) << 3
            | (A & 1) << 2 | (E & 1) << 1 | (D & 1);
        S >>= 2;
        E >>= 1;
        F >>= 1;
        A >>= 1;
        K >>= 1;
        D >>= 1;
    }
    ob(z);

    printf("z0=%u sa4=%u n=%d\n", z0, sa4, on);
    for (i = 0; i < on && i < 8; i++) {
        printf("b%d=%u\n", i, (unsigned)obuf[i]);
    }
    return 0;
}
