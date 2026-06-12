/*
 * bm_tty.c -- bare-metal Victor 9000 cooked console (§6g, Phase-6
 * step 4d).
 *
 * The minic-dialect port of newlibc's console_dev_read/console_dev_write
 * pair (drivers/console.c): the layer that makes the machine its own
 * terminal.  Output goes to the bm_display text screen AND the polled
 * serial console (so the headless harness sees what the screen shows);
 * input comes from the interrupt-driven bm_keyboard with line editing --
 * Backspace/DEL rub out the previous byte, keyboard Return (CR) is
 * exposed to readers as '\n', echo is immediate.
 *
 * This is the stdio seam: a future read(0,...)/write(1,...) routes here
 * instead of libstub's DOS INT 21h calls.
 */

#include "v9k_hw.h"
#include "bm_console.h"
#include "bm_display.h"
#include "bm_keyboard.h"
#include "bm_tty.h"

#define ASCII_DEL 0x7F

/* Echo one input byte the way newlibc does: screen first, then the
 * serial mirror (bm_putc expands '\n' to CRLF itself). */
static void tty_echo(char c) {
    bm_display_putc(c);
    bm_putc(c);
}

static void tty_echo_rubout(void) {
    tty_echo('\b');
    tty_echo(' ');
    tty_echo('\b');
}

void bm_tty_init(void) {
    bm_display_init();
    bm_keyboard_init();
}

void bm_tty_putc(char c) {
    tty_echo(c);
}

int bm_tty_write(const char *buf, unsigned int count) {
    unsigned int i;

    if (buf == 0)
        return 0;
    for (i = 0; i < count; i++)
        tty_echo(buf[i]);
    return (int)count;
}

int bm_tty_read(char *buf, unsigned int count) {
    unsigned int i;
    int c;

    if (buf == 0 || count == 0)
        return 0;

    i = 0;
    while (i < count) {
        c = bm_keyboard_getc();

        if (c == '\b' || c == ASCII_DEL) {
            if (i > 0) {
                i--;
                tty_echo_rubout();
            }
            continue;
        }

        /* Keyboard Return is CR; line readers expect LF. */
        if (c == '\r')
            c = '\n';

        buf[i] = (char)c;
        i++;
        tty_echo((char)c);

        if (c == '\n')
            break;
    }
    return (int)i;
}

int bm_tty_getc(void) {
    int c;

    c = bm_keyboard_getc();
    if (c == '\r')
        c = '\n';
    tty_echo((char)c);
    return c;
}
