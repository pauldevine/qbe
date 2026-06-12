/*
 * interrupt_bm.c -- bare-metal Victor 9000 interrupt-stability test
 * (§6f, Phase-6 step 4c).
 *
 * A minic-dialect port of newlibc's tests/interrupt_test.c, and the
 * battery's first CROSS-DRIVER stress: the display driver runs with
 * the timer ISR live the whole time.  Every display operation is a
 * far MMIO sequence (ES loads against VRAM/CRTC), so an ISR ABI that
 * mishandled ES or any register would corrupt either the screen or
 * the interrupted code within a few scroll cycles.  The display init
 * itself -- an 8 KB far font copy + 16 CRTC register writes -- runs
 * under live interrupts on purpose.
 *
 * Deterministic booleans/ranges only (MAME clocks 8253 ch2 at 125 kHz
 * vs the documented 100 kHz); every phase prints first (5 MHz rule).
 */

#include <stdint.h>
#include "bm_console.h"
#include "bm_display.h"
#include "bm_timer.h"
#include "bm_interrupts.h"

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

static int row_matches(uint8_t row, const char *s) {
    uint16_t pos;
    int i;

    pos = (uint16_t)(row * DISPLAY_COLS);
    for (i = 0; s[i] != 0; i++) {
        if (bm_display_read_cell((uint16_t)(pos + i))
            != bm_display_screen_word(s[i], 0))
            return 0;
    }
    return 1;
}

int main(void) {
    unsigned long t0, t1, elapsed;
    uint8_t row, col;
    int i;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal interrupt-stability test (qbe/minic)\n");

    bm_puts("phase 1: timer ISR + 8253 + PIC + sti\n");
    bm_interrupts_init();
    bm_timer_init();
    bm_interrupts_enable();

    bm_puts("phase 2: display init UNDER live interrupts\n");
    t0 = bm_timer_get_ticks();
    bm_display_init();
    t1 = bm_timer_get_ticks();

    check("phase 3: ticks advanced during font copy + CRTC setup: ",
          t1 > t0);

    bm_display_get_cursor(&row, &col);
    check("phase 4: display intact after init (blank + homed): ",
          bm_display_read_cell(0) == bm_display_screen_word(' ', 0) &&
          row == 0 && col == 0);

    /* 60 full lines through the putc path = 35+ scrolls (2000-word
     * VRAM block moves), all racing the ISR. */
    bm_puts("phase 5: 60 scrolling lines under live interrupts: ");
    t0 = bm_timer_get_ticks();
    for (i = 0; i < 60; i++) {
        bm_display_puts("interrupts and display together");
        bm_display_putc('\n');
    }
    bm_display_puts("last line survives");
    bm_display_get_cursor(&row, &col);
    if (row_matches(24, "last line survives") && row == 24 && col == 18) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    t1 = bm_timer_get_ticks();
    check("phase 6: ISR stayed live through the stress: ", t1 > t0);

    bm_puts("phase 7: delay(500ms) elapsed in [50..80]: ");
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

    check("phase 8: screen content still intact after delay: ",
          row_matches(24, "last line survives"));

    if (fails == 0)
        bm_puts("PASS: bare-metal interrupt-stability checks completed.\n");
    else
        bm_puts("FAIL: bare-metal interrupt-stability checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
