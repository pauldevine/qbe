/*
 * pic_bm.c -- bare-metal Victor 9000 8259A PIC test (§6f, Phase-6
 * step 4c).
 *
 * A minic-dialect port of newlibc's tests/pic_test.c: IMR read/write
 * checks on an unused expansion IRQ (IR5) while the timer interrupt is
 * LIVE on IR2, then the stronger check the original only implied --
 * masking IR2 must actually freeze ticks and unmasking must resume
 * them, proving the mask register gates delivery rather than just
 * holding bits.  Continuous ticks across the whole run are the EOI
 * test: a broken EOI yields exactly one tick then silence.
 *
 * Mask values printed are deterministic: after bm_interrupts_init +
 * bm_timer_init the IMR is 0xFB (all masked, IR2 open).  Results over
 * serial; every phase prints first (5 MHz rule).
 */

#include <stdint.h>
#include "bm_console.h"
#include "bm_pic.h"
#include "bm_timer.h"
#include "bm_interrupts.h"

#define IR5_BIT 0x20
#define IR2_BIT 0x04

static int fails;

static void check(const char *label, int ok) {
    bm_puts(label);
    if (ok) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }
}

/* Bounded spin that doesn't touch the timer: returns nonzero if ticks
 * moved off `start` within `budget` outer iterations (~6 ms each on
 * the 5 MHz 8088; the tick period is ~8 ms). */
static int wait_tick_change(unsigned long start, unsigned int budget) {
    unsigned int i, j;
    volatile unsigned int sink;

    for (i = 0; i < budget; i++) {
        for (j = 0; j < 2000; j++)
            sink = j;
        if (bm_timer_get_ticks() != start)
            return 1;
    }
    return 0;
}

int main(void) {
    unsigned char saved, mask;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal PIC test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR + 8253 + sti\n");
    bm_interrupts_init();
    bm_timer_init();
    bm_interrupts_enable();

    bm_puts("phase 2: IMR after init is 0xfb (only IR2 open): ");
    saved = bm_pic_get_mask();
    if (saved == 0xFB) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(saved);
        bm_puts(")\n");
        fails++;
    }

    check("phase 3: timer ticks advance: ",
          wait_tick_change(bm_timer_get_ticks(), 2000));

    bm_puts("phase 4: mask IR5 (unused expansion) sets bit: ");
    bm_pic_mask(5);
    mask = bm_pic_get_mask();
    if (mask == (unsigned char)(saved | IR5_BIT)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(mask);
        bm_puts(")\n");
        fails++;
    }

    check("phase 5: timer undisturbed by IR5 masking: ",
          wait_tick_change(bm_timer_get_ticks(), 2000));

    bm_puts("phase 6: unmask IR5 clears bit: ");
    bm_pic_unmask(5);
    mask = bm_pic_get_mask();
    if (mask == (unsigned char)(saved & (unsigned char)~IR5_BIT)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(mask);
        bm_puts(")\n");
        fails++;
    }

    bm_pic_set_mask(saved);
    check("phase 7: whole-mask restore reads back: ",
          bm_pic_get_mask() == saved);

    /* The mask must GATE delivery, not just store bits: masking the
     * live timer IRQ freezes ticks (interrupts still globally enabled),
     * unmasking resumes them. */
    bm_puts("phase 8: masking IR2 freezes ticks: ");
    bm_pic_mask(2);
    t0 = bm_timer_get_ticks();
    if (!wait_tick_change(t0, 150)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_pic_unmask(2);
    check("phase 9: unmasking IR2 resumes ticks: ",
          wait_tick_change(bm_timer_get_ticks(), 2000));

    /* ~250 ms more of live interrupts; continuous ticks across the run
     * are the implicit EOI check (broken EOI = one tick, then hang). */
    check("phase 10: EOI verified by continuous ticks: ",
          wait_tick_change(bm_timer_get_ticks(), 2000));

    if (fails == 0)
        bm_puts("PASS: bare-metal PIC checks completed.\n");
    else
        bm_puts("FAIL: bare-metal PIC checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
