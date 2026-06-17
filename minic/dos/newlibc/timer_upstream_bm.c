/*
 * timer_upstream_bm.c -- bare-metal Victor 9000 timer test driving the
 * UPSTREAM newlibc drivers/timer.c (§8l, Phase-6 step 4 follow-up).
 *
 * Where timer_bm.c (§6d) exercises the hand-mirrored bm_timer.c port, this
 * links and runs newlibc's OWN drivers/timer.c -- the file the §8k gas->nasm
 * in-place port made minic-compilable (its intel_dev_write_byte now forks
 * `#if defined(__MINIC__)` to HW_WRITE_BYTE).  §8k proved that file COMPILES;
 * this proves it RUNS on the bare machine, the real Phase-6 end-state where
 * newlibc's own drivers replace the bm_*.c mirrors.
 *
 * The interrupt plumbing here is the generic toolchain feature, not a driver:
 * the local timer_isr is the compiler-emitted ES-safe iret ABI
 * (__attribute__((interrupt)) -> QBE `interrupt` linkage -> the i8086
 * backend's prologue/epilogue, §6d), and it routes each IR2 tick to the
 * UPSTREAM timer_tick_handler() so the upstream tick_counter is what
 * timer_get_ticks()/timer_delay_ms() read.  The 8259 re-init is the same
 * mandatory bm_pic_init() every bare-metal sti needs (the ROM leaves IRQs
 * unmasked on stale vectors).  NOTHING from bm_timer.c is linked.
 *
 * Output is deterministic booleans/ranges only (MAME models the channel-2
 * input clock at 125 KHz vs the documented 100 KHz, so absolute counts vs
 * wall time vary, but tick ACCOUNTING is exact).  Every phase prints before
 * it runs -- a 5 MHz 8088 makes silence unreadable.
 */

#include <stdint.h>
#include "bm_console.h"
#include "bm_pic.h"
#include "timer.h"      /* UPSTREAM: timer_init/get_ticks/delay_ms/get_frequency/tick_handler */
#include "v9k_hw.h"     /* INT_TIMER, IRQ_TIMER, INTEL_DEV_SEGMENT, PIC_COMMAND_PORT, HW_WRITE_BYTE */

/* Near offset of the (single) code frame, for the small-model IVT install. */
extern unsigned qbe_get_cs(void);

/* Timer ISR (IR2, INT 0x42): route the tick to the UPSTREAM handler, send
 * the specific EOI.  The compiler-emitted prologue already saved ES, set
 * DS/ES=DGROUP and saved every register; the EOI clears the in-service bit. */
void __far __attribute__((interrupt)) timer_isr(void) {
    timer_tick_handler();
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_TIMER));
}

/* Model-agnostic IVT install, mirroring bm_interrupts.c's bm_install_isr:
 * far-code models carry seg:off in the pointer; near-code models a bare
 * offset whose segment is qbe_get_cs().  Call with interrupts disabled. */
static void install_isr(unsigned char int_num, void (*fn)(void)) {
    volatile uint16_t __far *ivt;
    uint32_t lin;
    uint16_t seg, off;

    lin = (uint32_t)fn;
    off = (uint16_t)lin;
    seg = (uint16_t)(lin >> 16);
    if (seg == 0)
        seg = (uint16_t)qbe_get_cs();

    ivt = (volatile uint16_t __far *)
        ((((uint32_t)0) << 16) | ((uint16_t)(int_num * 4)));
    ivt[0] = off;
    ivt[1] = seg;
}

/* Bounded spin that doesn't touch the timer: returns nonzero if ticks moved
 * off `start` within `budget` outer iterations (the timer_bm pattern). */
static int wait_tick_change(unsigned long start, unsigned int budget) {
    unsigned int i, j;
    volatile unsigned int sink;

    for (i = 0; i < budget; i++) {
        for (j = 0; j < 2000; j++)
            sink = j;
        if (timer_get_ticks() != start)
            return 1;
    }
    return 0;
}

int main(void) {
    unsigned long t0, t1, elapsed;
    int fails;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal UPSTREAM timer.c test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + install timer ISR (INT 0x42)\n");
    bm_pic_init();
    install_isr(INT_TIMER, timer_isr);

    bm_puts("phase 2: upstream timer_init() (8253 ch2 100 Hz + IR2 unmask)\n");
    timer_init();

    bm_puts("phase 3: upstream timer_get_frequency()==100: ");
    if (timer_get_frequency() == 100UL) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 4: sti\n");
    __asm__ volatile ("sti");

    bm_puts("phase 5: ticks advance: ");
    t0 = timer_get_ticks();
    if (wait_tick_change(t0, 2000)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 6: delay(500ms) elapsed in [50..80]: ");
    t0 = timer_get_ticks();
    timer_delay_ms(500);
    t1 = timer_get_ticks();
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
     * compiler-emitted ABI walks SP off the world well before this returns. */
    bm_puts("phase 7: 1500ms under live interrupts: ");
    timer_delay_ms(1500);
    bm_puts("survived\n");

    bm_puts("phase 8: cli freezes ticks: ");
    __asm__ volatile ("cli");
    t0 = timer_get_ticks();
    if (!wait_tick_change(t0, 150)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: upstream timer.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream timer.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
