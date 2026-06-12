/*
 * serial_bm.c -- bare-metal Victor 9000 serial RX ISR test (§6e,
 * Phase-6 step 4b).
 *
 * The harness (run-victor-baremetal.sh with V9K_SERIAL_IN=<file>)
 * attaches a byte stream to serial port B a few seconds in; the bytes
 * arrive through the 7201 channel-B RX interrupt (IR1) and the
 * compiler-emitted ISR into a ring buffer.  Console output stays on
 * polled channel A, so this runs THREE live interrupt sources through
 * the compiler ABI at once: timer (IR2), serial RX (IR1), and the
 * 7201's pending-interrupt handshake.
 *
 * Expected stream: "victor" (6 bytes).  Every phase prints before it
 * runs (5 MHz 8088 rule).
 */

#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"
#include "bm_serial.h"

#define EXPECT_LEN 6

int main(void) {
    char got[EXPECT_LEN + 1];
    int ngot, c, fails;
    unsigned long t0;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal serial RX/ISR test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: 7201 ch B 9600 + RX ISR (INT 0x41) + IR1 unmask\n");
    bm_serial_init();

    bm_puts("phase 4: sti\n");
    bm_interrupts_enable();

    bm_puts("phase 5: no RX byte pending at start: ");
    c = bm_serial_getc_nonblock();
    if (c < 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    /* The harness attaches the stream after ~3 s; allow ~15 s. */
    bm_puts("phase 6: waiting for 6 streamed bytes: ");
    ngot = 0;
    t0 = bm_timer_get_ticks();
    while (ngot < EXPECT_LEN &&
           (bm_timer_get_ticks() - t0) < 1900) {
        c = bm_serial_getc_nonblock();
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
        bm_puts(" byte(s) \"");
        bm_puts(got);
        bm_puts("\"\n");
        fails++;
    }

    bm_puts("phase 7: bytes match \"victor\": ");
    if (ngot == 6 &&
        got[0] == 'v' && got[1] == 'i' && got[2] == 'c' &&
        got[3] == 't' && got[4] == 'o' && got[5] == 'r') {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: serial ISR entered: ");
    if (bm_serial_isr_count() > 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: no ring-buffer overruns: ");
    if (bm_serial_overruns() == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 10: timer still ticking alongside IR1: ");
    t0 = bm_timer_get_ticks();
    bm_timer_delay_ms(200);
    if (bm_timer_get_ticks() != t0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: bare-metal serial RX ISR checks completed.\n");
    else
        bm_puts("FAIL: bare-metal serial RX ISR checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
