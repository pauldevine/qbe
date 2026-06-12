/*
 * bm_timer.c -- bare-metal Victor 9000 100 Hz system timer (§6d,
 * Phase-6 step 4).
 *
 * A minic-dialect port of newlibc's drivers/timer.c, keeping every
 * validated Phase-1 hardware fact:
 *   - Channel 2 of the 8253, NOT channel 0 (channel 0 is Serial A baud);
 *   - the channel-2 input clock is 100 KHz (Victor-specific, NOT the
 *     IBM-PC 1.193 MHz) so 100 Hz needs divisor 1000;
 *   - mode 2 (rate generator), LSB then MSB;
 *   - the timer raises IR2 on the memory-mapped PIC (E000:0000/0001),
 *     vector 0x42 — no IBM-PC mapping anywhere.
 *
 * The original's intel_dev_write_byte inline asm forced individual byte
 * stores because ia16-gcc merged adjacent volatile byte stores into one
 * 16-bit store, breaking the 8253's control-then-count protocol.  minic
 * volatile-far stores are real single byte stores, so plain
 * HW_WRITE_BYTE is already correct.
 *
 * MAME models channel 2 at 125 KHz, so emulated tests run ~25% fast in
 * wall time; the programmed divisor follows the Victor docs (and MS-DOS
 * TOD.ASM).  Tick accounting is exact either way.
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_timer.h"

#define BM_TICK_HZ        100UL
#define BM_TIMER_DIVISOR  (TIMER_CHANNEL_2_CLOCK / BM_TICK_HZ)   /* 1000 */

static volatile uint32_t tick_counter;

void bm_timer_init(void) {
    uint16_t divisor;

    divisor = (uint16_t)BM_TIMER_DIVISOR;

    /* Control byte 0xB4: channel 2 (10), LSB+MSB (11), mode 2 (010),
     * binary (0).  Control word first, then the two count bytes. */
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_CONTROL, 0xB4);
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_COUNTER_2,
                  (uint8_t)(divisor & 0xFF));
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, TIMER_COUNTER_2,
                  (uint8_t)(divisor >> 8));

    tick_counter = 0;

    /* Unmask IR2 on the memory-mapped PIC (read-modify-write of the
     * mask register at E000:0001). */
    PIC_UNMASK_IRQ(IRQ_TIMER);
}

/* Called from timer_isr (bm_interrupts.c) on each IR2.  Plain C — the
 * compiler-emitted ISR prologue already established DS/ES=DGROUP and
 * saved every register, and the ISR sends the EOI. */
void bm_timer_tick_handler(void) {
    tick_counter = tick_counter + 1;
}

unsigned long bm_timer_get_ticks(void) {
    uint32_t t;

    /* 32-bit reads are not atomic on the 8086; the ISR only ever
     * increments, so read until two reads agree. */
    do {
        t = tick_counter;
    } while (t != tick_counter);
    return t;
}

void bm_timer_delay_ms(unsigned long ms) {
    uint32_t start, target;

    target = (ms * BM_TICK_HZ) / 1000UL;
    if (target == 0)
        target = 1;
    start = bm_timer_get_ticks();
    while ((bm_timer_get_ticks() - start) < target)
        ;
}
