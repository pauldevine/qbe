/*
 * timer_bm.c -- bare-metal Victor 9000 timer-interrupt test (§6d,
 * Phase-6 step 4).
 *
 * The first INTERRUPT-DRIVEN program from this toolchain on the bare
 * machine: installs the compiler-emitted timer ISR
 * (__attribute__((interrupt)) → QBE `interrupt` linkage → ES-safe
 * iret ABI) at vector 0x42, programs 8253 channel 2 for 100 Hz,
 * unmasks IR2 on the memory-mapped PIC, and verifies live ticks.
 *
 * Output is deterministic booleans/ranges only: MAME models the
 * channel-2 input clock at 125 KHz (vs the documented 100 KHz), so
 * absolute tick counts vs wall time vary, but tick ACCOUNTING is
 * exact.  Every phase prints before it runs — a 5 MHz 8088 makes
 * silence unreadable.
 */

#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"

/* Bounded spin that doesn't touch the timer: returns nonzero if ticks
 * moved off `start` within `budget` outer iterations.  One outer
 * iteration is ~6 ms on the 5 MHz 8088 — comfortably under the ~8 ms
 * tick period, so budget≈150 spans 100+ tick opportunities. */
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
    unsigned long t0, t1, elapsed;
    int fails;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal timer/ISR test (qbe/minic)\n");

    bm_puts("phase 1: install timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + PIC IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: sti\n");
    bm_interrupts_enable();

    bm_puts("phase 4: ticks advance: ");
    t0 = bm_timer_get_ticks();
    if (wait_tick_change(t0, 2000)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 5: delay(500ms) elapsed in [50..80]: ");
    t0 = bm_timer_get_ticks();
    bm_timer_delay_ms(500);
    t1 = bm_timer_get_ticks();
    elapsed = t1 - t0;
    if (elapsed >= 50 && elapsed <= 80) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu(elapsed);
        bm_puts(")\n");
        fails++;
    }

    /* ~150 more live ISR entries; any push/pop or iret imbalance in the
     * compiler-emitted ABI walks SP off the world well before this
     * returns. */
    bm_puts("phase 6: 1500ms under live interrupts: ");
    bm_timer_delay_ms(1500);
    bm_puts("survived\n");

    bm_puts("phase 7: cli freezes ticks: ");
    bm_interrupts_disable();
    t0 = bm_timer_get_ticks();
    if (!wait_tick_change(t0, 150)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: bare-metal timer ISR checks completed.\n");
    else
        bm_puts("FAIL: bare-metal timer ISR checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
