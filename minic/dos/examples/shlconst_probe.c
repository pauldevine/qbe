/*
 * shlconst_probe.c -- regression gate for the i8086 Kw shift dropping a
 * CONSTANT (or slot) value operand.
 *
 * The Kw shift handler in i8086/emit.c only materialized arg[0] (the value
 * being shifted) into the destination register when arg[0] was an RTmp the
 * register allocator had already placed there.  When arg[0] was a constant
 * (RCon) or a stack slot (RSlot) it was NEVER loaded, so the shift operated
 * on whatever the destination register happened to hold -- which, for
 * `CONST << count`, was the freshly-computed COUNT.  So `1 << count` became
 * `count << count`, and for count == 0 that is `0 << 0 == 0`.
 *
 * Canonical victim: gc_alloc()'s ATB head-mark in py/gc.c,
 *     gc_alloc_table_start[b / 4] |= (AT_HEAD << (2 * (b & 3)))
 * i.e. `1 << (2*(b&3))`.  For a block whose index is divisible by 4 the
 * shift amount is 0, so the mark became `|= 0` -- a no-op.  The block was
 * never recorded as used, so the NEXT gc_alloc() handed out the same live
 * block and m_new0() zero-filled it, silently corrupting the object that
 * had just been allocated there.  (In the MicroPython port this zeroed a
 * scope's parse-tree-root pointer, derailing mp_compile.)
 *
 * Fix: emit_shift_val() materializes the value operand (RTmp/RCon/RSlot,
 * including CAddr) into the shift register, emitted after the count is
 * secured into CL so a count that aliases the destination is read first.
 *
 * Without the fix `atb0`/`atb1` come out as garbage (the divisible-by-4
 * blocks lose their bit and the others set the wrong bit); with it they are
 * 0x55 each.  Inputs are globals so nothing folds to a compile-time const.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/shlconst_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/shlconst_probe/shlconst_probe.exe \
 *             | diff - minic/dos/tests/shlconst_probe.golden.txt
 * Wired into tools/test-dos.sh RUNTIME_TESTS (medium + compact).
 */

unsigned char atb[2];
int gblk[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
int gn = 3;

int main(void) {
    int i;
    /* The gc_alloc ATB head-mark pattern: set the 2-bit field for each of
     * 8 blocks to AT_HEAD (1).  atb[0] covers blocks 0..3, atb[1] 4..7;
     * each correct field is 0b01, so each byte must end up 0x55 = 85. */
    atb[0] = 0;
    atb[1] = 0;
    for (i = 0; i < 8; i++) {
        atb[gblk[i] / 4] |= (1 << (2 * (gblk[i] & 3)));
    }
    /* direct CONST << variable count, with the count in a register */
    printf("atb0=%u atb1=%u s0=%u s1=%u s2=%u\n",
        (unsigned)atb[0], (unsigned)atb[1],
        1u << (gn - 3), 1u << gn, 2u << gn);
    return 0;
}
