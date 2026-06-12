/*
 * bm_interrupts.c -- bare-metal Victor 9000 interrupt framework (§6d,
 * Phase-6 step 4).
 *
 * A minic-dialect port of newlibc's drivers/interrupts.c.  The ISR
 * bodies are plain C: `__attribute__((interrupt))` travels as QBE
 * `interrupt` linkage and the i8086 backend emits the validated
 * ES-safe ABI itself (ES saved to static CS-local memory FIRST, every
 * register saved, DS/ES=DGROUP from a link-time selector word, iret) —
 * the SAVE_ES/RESTORE_ES macro pairs the OpenWatcom/ia16 builds need
 * are compiler-emitted here, not source.
 *
 * Victor facts preserved from the original:
 *   - timer is IR2 → vector 0x42 (PIC_INT_BASE 0x40 + 2), NOT IBM-PC
 *     IRQ0/INT 8;
 *   - EOI is SPECIFIC (0x60 | irq → 0x62 for the timer) written to the
 *     memory-mapped PIC command register at E000:0000 — no I/O ports.
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_pic.h"
#include "bm_timer.h"
#include "bm_interrupts.h"

extern unsigned qbe_get_cs(void);

/* IVT entry write.  Call with interrupts disabled; a half-written
 * vector taken live is a wild jump. */
void bm_set_vector(unsigned char int_num,
                   unsigned short seg, unsigned short off) {
    volatile uint16_t __far *ivt;

    ivt = (volatile uint16_t __far *)
        ((((uint32_t)0) << 16) | ((uint16_t)(int_num * 4)));
    ivt[0] = off;
    ivt[1] = seg;
}

/* Timer ISR (IR2, INT 0x42): count the tick, send the specific EOI. */
void __far __attribute__((interrupt)) timer_isr(void) {
    bm_timer_tick_handler();
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_TIMER));
}

/* Handler address, model-agnostic: far-code models carry seg:off in
 * the function pointer; near-code models a bare offset whose segment
 * is the (single) code frame — qbe_get_cs().  Exported so driver TUs
 * (bm_keyboard.c, bm_serial.c) can install their own ISRs. */
void bm_install_isr(unsigned char int_num, bm_isr_fn_t fn) {
    uint32_t lin;
    uint16_t seg, off;

    lin = (uint32_t)fn;
    off = (uint16_t)lin;
    seg = (uint16_t)(lin >> 16);
    if (seg == 0)
        seg = (uint16_t)qbe_get_cs();
    bm_set_vector(int_num, seg, off);
}

void bm_interrupts_init(void) {
    /* Re-init the PIC FIRST: mask everything and clear the boot ROM's
     * in-service bits.  The ROM leaves IRQs unmasked (IR7 vertical
     * sync fires every frame) with handlers whose RAM workspace this
     * image may have overwritten — a bare sti without this wild-jumps
     * within milliseconds. */
    bm_pic_init();
    bm_install_isr(INT_TIMER, timer_isr);
}

void bm_interrupts_enable(void) {
    __asm__ volatile ("sti");
}

void bm_interrupts_disable(void) {
    __asm__ volatile ("cli");
}
