/* bm_tty.h -- bare-metal Victor 9000 cooked console (§6g). */
#ifndef BM_TTY_H
#define BM_TTY_H

/* The machine's own console: bm_keyboard input + bm_display output,
 * with every byte mirrored to the serial console so a headless harness
 * sees exactly what the screen shows.
 *
 * Call bm_tty_init() AFTER bm_interrupts_init() (the keyboard side
 * installs its IR6 ISR) and BEFORE bm_interrupts_enable() -- the same
 * window as bm_keyboard_init(), which it calls. */
void bm_tty_init(void);

/* Output: display + serial.  '\n' handling is per-device (the display
 * clears to end of line and wraps; the serial side sends CRLF). */
void bm_tty_putc(char c);
int bm_tty_write(const char *buf, unsigned int count);

/* Cooked input, the newlibc console_dev_read contract: blocking
 * keyboard reads with echo; Backspace/DEL rub out the previous byte
 * ("\b \b" to both devices); keyboard Return (CR) is exposed as '\n';
 * the read ends at '\n' or when count bytes are buffered.  Returns the
 * number of bytes stored. */
int bm_tty_read(char *buf, unsigned int count);

/* One cooked byte (blocking, echoed, CR->LF; no rubout editing --
 * a single-byte read has nothing to rub out). */
int bm_tty_getc(void);

#endif
