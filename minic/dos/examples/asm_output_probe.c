/* asm_output_probe.c — reduced gate for GCC extended-asm OUTPUT operands
 * ("=r"/"=m"), the §6a triage's lone genuine codegen bug (the qbe-stage
 * failures in newlibc phase3 drivers display/keyboard/pic/sasi).
 *
 * minic lowers an output operand bound to a local — `"=r"(x)` / `"=m"(x)` —
 * to the slot's memory `[bp-%x]` substituted into the opaque asm template,
 * but emits NO IR store of the slot.  QBE's promote pass therefore sees the
 * slot read (the readback after the asm) with no store and ABORTS:
 *   qbe: ... slot %x is read but never stored to
 * even though the asm writes that memory at runtime.  The fix leaves such a
 * slot in memory instead of aborting (mem.c promote, gated on the function
 * having inline asm).
 *
 * This probe uses NASM-valid Intel-syntax mnemonics (the real drivers use
 * gas/AT&T syntax that needs separate per-target porting), so it compiles
 * end-to-end and runs — exercising exactly the output-operand path that
 * aborted.  Bug-loud: on the unfixed compiler the build dies at the QBE
 * stage (no .exe), so the run produces no output and the golden diff fails.
 */

extern int printf(const char *, ...);

/* "=r": write an immediate into the output slot. */
static int out_r(void) {
    int r;
    __asm__ volatile (
        "mov word %0, 0x1234"
        : "=r"(r)
    );
    return r;
}

/* "=m": the sasi.c constraint — memory output. */
static int out_m(void) {
    int m;
    __asm__ volatile (
        "mov word %0, 0x5678"
        : "=m"(m)
    );
    return m;
}

/* "=r" output + "r" input round trip: read the input slot, double it,
 * write the output slot (operands are numbered outputs-first, %0=out,
 * %1=in). */
static int dbl(int x) {
    int r;
    __asm__ volatile (
        "mov ax, %1\n\t"
        "add ax, ax\n\t"
        "mov %0, ax"
        : "=r"(r)
        : "r"(x)
        : "ax"
    );
    return r;
}

int main(void) {
    int a, b, c, d;

    a = out_r();
    b = out_m();
    c = dbl(21);
    d = dbl(a);                 /* 0x1234 * 2 = 0x2468 */

    printf("out_r=%d\n", a);    /* 4660  */
    printf("out_m=%d\n", b);    /* 22136 */
    printf("dbl21=%d\n", c);    /* 42    */
    printf("dblr=%d\n", d);     /* 9320  */

    if (a == 0x1234 && b == 0x5678 && c == 42 && d == 0x2468)
        printf("PASS\n");
    else
        printf("FAIL\n");
    return 0;
}
