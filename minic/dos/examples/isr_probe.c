/* isr_probe.c — reduced gate for __attribute__((interrupt)) (§6d).
 *
 * Exercises the full ISR ABI the i8086 backend emits for QBE `interrupt`
 * linkage: a handler installed on software INT 0xF1 must
 *   1. run with DS=ES=DGROUP (it writes file-scope globals and a far
 *      pointer — both would address the wrong segment if the prologue
 *      didn't establish DGROUP from the CS-local selector word);
 *   2. survive a callee (caller-save clobbers inside the ISR body) and
 *      32-bit math (libstub _qbe_div32u — AX/DX/CX heavy);
 *   3. restore every register and the interrupted ES (static-memory ES
 *      save) before iret;
 *   4. keep the stack balanced — fired 1000 times in a tight loop, any
 *      push/pop or iret imbalance walks SP off the frame and dies loudly.
 *
 * The pre-fix toolchain (asm-"iret"-in-body) fails this probe at build
 * or at first trigger: the iret fired with the frame still up.
 *
 * Vector 0xF1 is in the user range — neither DOS nor DOSBox owns it.
 * The old vector is saved and restored for DOS-hosted politeness.
 */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;

#define MK_FP(seg, off) \
    ((void __far *)((((uint32_t)(seg)) << 16) | ((uint16_t)(off))))

extern int printf(const char *, ...);
extern unsigned qbe_get_cs(void);

volatile uint16_t isr_count;
volatile uint16_t isr_mix;

/* Callee for the ISR: forces caller-save clobbers and a libstub
 * 32-bit divide inside the handler. */
static uint16_t mix(uint16_t a) {
    uint32_t wide;
    wide = (uint32_t)a * 7UL + 100000UL;
    return (uint16_t)(wide / 3UL) + (uint16_t)(wide % 5UL);
}

void __far __attribute__((interrupt)) probe_isr(void) {
    volatile uint8_t __far *ica;
    isr_count = isr_count + 1;                 /* near data: needs DS=DGROUP */
    ica = (volatile uint8_t __far *)MK_FP(0x0040, 0x00F0);
    *ica = (uint8_t)isr_count;                 /* far op: ES bracket in ISR */
    isr_mix = mix(isr_count);                  /* callee + 32-bit divide */
}

typedef void (*isr_fn_t)(void);

int main(void) {
    volatile uint16_t __far *ivt;
    volatile uint8_t __far *ica;
    uint16_t old_off, old_seg, seg, off, i, sum;
    isr_fn_t fp;
    uint32_t lin;

    ivt = (volatile uint16_t __far *)MK_FP(0, 0xF1 * 4);
    ica = (volatile uint8_t __far *)MK_FP(0x0040, 0x00F0);

    /* Handler address: far-code models carry seg:off in the pointer;
     * near-code models a bare offset (high word 0) — segment is CS. */
    fp = probe_isr;
    lin = (uint32_t)fp;
    off = (uint16_t)lin;
    seg = (uint16_t)(lin >> 16);
    if (seg == 0)
        seg = (uint16_t)qbe_get_cs();
    printf("install: have seg %d off %d\n", seg != 0, off != 0);

    old_off = ivt[0];
    old_seg = ivt[1];
    ivt[0] = off;
    ivt[1] = seg;

    isr_count = 0;
    *ica = 0;

    __asm__("int 0xf1");
    printf("first: count=%u ica=%u mix=%u\n",
           isr_count, (unsigned)*ica, isr_mix);

    /* Live values across triggers: a register leak in the ISR ABI
     * corrupts sum or the loop counter. */
    sum = 0;
    for (i = 1; i <= 5; i++) {
        sum = sum + i * 3;
        __asm__("int 0xf1");
        sum = sum ^ isr_count;
    }
    printf("five: count=%u ica=%u sum=%u\n",
           isr_count, (unsigned)*ica, sum);

    /* Stack-balance hammer. */
    for (i = 0; i < 1000; i++)
        __asm__("int 0xf1");
    printf("hammer: count=%u ica=%u\n", isr_count, (unsigned)*ica);

    ivt[0] = old_off;
    ivt[1] = old_seg;

    if (isr_count == 1006 && *ica == (uint8_t)1006)
        printf("PASS\n");
    else
        printf("FAIL count=%u\n", isr_count);
    return 0;
}
