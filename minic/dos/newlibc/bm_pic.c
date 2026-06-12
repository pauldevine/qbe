/*
 * bm_pic.c -- bare-metal Victor 9000 8259A PIC driver (§6d, Phase-6
 * step 4).
 *
 * A minic-dialect port of newlibc's drivers/pic.c.  The PIC is
 * memory-mapped at E000:0000 (command) / E000:0001 (mask) — the Victor
 * has NO I/O ports.  Vector base is 0x40 (IR2 timer → INT 0x42), and
 * the wiring is NOT IBM-PC: keyboard=IR6, serial=IR1, timer=IR2,
 * vertical sync=IR7.
 *
 * The full re-init matters on the bare-metal loader path: the boot ROM
 * leaves its own mask state and in-service bits behind, with ROM
 * handlers (e.g. IR7 vertical sync, every frame) whose RAM workspace
 * our image may have overwritten.  A bare `sti` without masking
 * everything first wild-jumps within milliseconds — re-init, mask all,
 * then unmask only what we own.
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_pic.h"

/* 8259A needs a moment between ICW writes; two short jumps is the
 * classical idiom (no registers, no labels). */
static void pic_delay(void) {
    __asm__ volatile ("jmp short $+2");
    __asm__ volatile ("jmp short $+2");
}

static void pic_cmd(uint8_t v) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT, v);
}

static void pic_data(uint8_t v) {
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_MASK_PORT, v);
}

static uint8_t pic_read_mask(void) {
    return HW_READ_BYTE(INTEL_DEV_SEGMENT, PIC_MASK_PORT);
}

void bm_pic_init(void) {
    uint8_t irq;

    /* ICW1 0x17: init, ICW4 needed, single PIC, ADI=4, edge triggered
     * (the Victor ROM / MS-DOS TOD value; IBM-PC cascade 0x11 is wrong
     * here).  ICW3 is SKIPPED — single PIC. */
    pic_cmd(0x17);
    pic_delay();
    pic_data(PIC_INT_BASE);          /* ICW2: vectors 0x40..0x47 */
    pic_delay();
    pic_data(0x01);                  /* ICW4: 8086 mode, normal EOI */
    pic_delay();

    /* Clear any in-service bits the boot ROM left behind. */
    for (irq = 0; irq < 8; irq++) {
        pic_cmd((uint8_t)(0x60 | irq));   /* specific EOI */
        pic_delay();
    }

    /* Mask everything; owners unmask their own IRQ. */
    pic_data(0xFF);
    pic_delay();
}

void bm_pic_unmask(unsigned char irq) {
    uint8_t mask;

    if (irq > 7)
        return;
    mask = pic_read_mask();
    mask = mask & (uint8_t)~(1 << irq);
    pic_data(mask);
}

void bm_pic_mask(unsigned char irq) {
    uint8_t mask;

    if (irq > 7)
        return;
    mask = pic_read_mask();
    mask = mask | (uint8_t)(1 << irq);
    pic_data(mask);
}

unsigned char bm_pic_get_mask(void) {
    return pic_read_mask();
}

void bm_pic_set_mask(unsigned char mask) {
    pic_data(mask);
}
