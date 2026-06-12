/*
 * keyboard_bm.c -- bare-metal Victor 9000 keyboard ISR test (§6e,
 * Phase-6 step 4b).
 *
 * Two compiler-emitted ISRs live at once: the 100 Hz timer on IR2 and
 * the keyboard on IR6.  The harness (run-victor-baremetal.sh with
 * V9K_KEYPOST=v9k) types "v9k" through MAME's natural keyboard a few
 * seconds in; every keystroke then travels VIA CS2 shift register ->
 * KBINT (IR6) -> compiler-emitted ISR -> ring buffer -> cooked ASCII.
 * There is NO polling path in the port -- if the chars arrive at all,
 * they arrived through the interrupt ABI.
 *
 * Every phase prints before it runs (5 MHz 8088 rule).  Output is
 * deterministic: fixed text plus the received characters.
 */

#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"
#include "bm_keyboard.h"

#define EXPECT_LEN 3

int main(void) {
    char got[EXPECT_LEN + 1];
    int ngot, c, fails;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal keyboard/ISR test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: keyboard VIA init + ISR (INT 0x46) + IR6 unmask\n");
    bm_keyboard_init();

    bm_puts("phase 4: sti\n");
    bm_interrupts_enable();

    bm_puts("phase 5: no key pending at start: ");
    c = bm_keyboard_getc_nonblock();
    if (c < 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    /* The harness types after ~3 s; allow up to ~15 s of ticks. */
    bm_puts("phase 6: waiting for 3 typed chars: ");
    ngot = 0;
    t0 = bm_timer_get_ticks();
    while (ngot < EXPECT_LEN &&
           (bm_timer_get_ticks() - t0) < 1900) {
        c = bm_keyboard_getc_nonblock();
        if (c >= 0) {
            got[ngot] = (char)c;
            ngot++;
        }
    }
    got[ngot] = 0;
    if (ngot == EXPECT_LEN) {
        bm_puts("got \"");
        bm_puts(got);
        bm_puts("\"\n");
    } else {
        bm_puts("TIMEOUT after ");
        bm_putu((unsigned long)ngot);
        bm_puts(" char(s) \"");
        bm_puts(got);
        bm_puts("\"\n");
        fails++;
    }

    bm_puts("phase 7: chars match \"v9k\": ");
    if (ngot == 3 && got[0] == 'v' && got[1] == '9' && got[2] == 'k') {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    /* No polling path exists, but make the interrupt evidence loud. */
    bm_puts("phase 8: keyboard ISR entered: ");
    if (bm_keyboard_isr_count() > 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: no ring-buffer overruns: ");
    if (bm_keyboard_overruns() == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 10: timer still ticking alongside IR6: ");
    t0 = bm_timer_get_ticks();
    bm_timer_delay_ms(200);
    if (bm_timer_get_ticks() != t0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: bare-metal keyboard ISR checks completed.\n");
    else
        bm_puts("FAIL: bare-metal keyboard ISR checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
