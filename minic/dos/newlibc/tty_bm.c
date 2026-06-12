/*
 * tty_bm.c -- bare-metal Victor 9000 cooked-console test (§6g,
 * Phase-6 step 4d).
 *
 * Exercises bm_tty, the layer that makes the machine its own terminal:
 * the harness (V9K_KEYPOST with control bytes) types "vx\b9k\nz" through
 * MAME's natural keyboard, and the cooked read must hand back "v9k\n" --
 * the 'x' rubbed out by a REAL Backspace keystroke travelling the full
 * path: VIA CS2 -> IR6 -> compiler-emitted ISR -> ring buffer -> cooked
 * ASCII -> line editor.  Every input byte echoes to BOTH the bm_display
 * screen and the serial console, so the capture shows the editing
 * sequence verbatim and the VRAM readback proves the screen ends up
 * showing the EDITED line.
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"
#include "bm_keyboard.h"
#include "bm_display.h"
#include "bm_tty.h"

#define LINE_MAX 16

/* Print a line with '\n'/'\b' made visible, for a stable golden. */
static void put_visible(const char *s, int n) {
    int i;

    bm_putc('"');
    for (i = 0; i < n; i++) {
        if (s[i] == '\n') {
            bm_puts("\\n");
        } else if (s[i] == '\b') {
            bm_puts("\\b");
        } else {
            bm_putc(s[i]);
        }
    }
    bm_putc('"');
}

static int screen_shows(uint16_t pos, const char *s) {
    while (*s) {
        if (bm_display_read_cell(pos) !=
            bm_display_screen_word(*s, ATTR_NORMAL))
            return 0;
        pos++;
        s++;
    }
    return 1;
}

int main(void) {
    char line[LINE_MAX];
    int n, c, fails;
    uint8_t row, col;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal cooked-console test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: tty init (display + keyboard, IR6)\n");
    bm_tty_init();

    bm_puts("phase 4: sti\n");
    bm_interrupts_enable();

    /* The prompt and the echo land on the screen AND in this serial
     * capture; the harness types "vx\b9k\nz" a few seconds in. */
    bm_puts("phase 5: prompt + cooked line read\n");
    bm_tty_write("v9k> ", 5);
    n = bm_tty_read(line, LINE_MAX);

    bm_puts("phase 6: got ");
    bm_putu((unsigned long)n);
    bm_puts(" byte(s) ");
    put_visible(line, n);
    bm_putc('\n');

    bm_puts("phase 7: line is \"v9k\\n\": ");
    if (n == 4 && line[0] == 'v' && line[1] == '9' &&
        line[2] == 'k' && line[3] == '\n') {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: screen shows the EDITED line \"v9k> v9k\": ");
    if (screen_shows(0, "v9k> v9k") &&
        bm_display_read_cell(8) == bm_display_screen_word(' ', ATTR_NORMAL)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: cursor wrapped to row 1 col 0: ");
    bm_display_get_cursor(&row, &col);
    if (row == 1 && col == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu((unsigned long)row);
        bm_putc(',');
        bm_putu((unsigned long)col);
        bm_puts(")\n");
        fails++;
    }

    /* One more typed char is still queued behind the line. */
    bm_puts("phase 10: single cooked getc: ");
    c = bm_tty_getc();
    bm_putc('\n');
    if (c == 'z') {
        bm_puts("got 'z'\n");
    } else {
        bm_puts("got WRONG char\n");
        fails++;
    }

    bm_puts("phase 11: keyboard ISR entered, no overruns: ");
    if (bm_keyboard_isr_count() > 0 && bm_keyboard_overruns() == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 12: timer still ticking: ");
    t0 = bm_timer_get_ticks();
    bm_timer_delay_ms(200);
    if (bm_timer_get_ticks() != t0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: bare-metal cooked-console checks completed.\n");
    else
        bm_puts("FAIL: bare-metal cooked-console checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
