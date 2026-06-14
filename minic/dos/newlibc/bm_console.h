/* bm_console.h -- bare-metal Victor 9000 polled serial console (§6c). */
#ifndef BM_CONSOLE_H
#define BM_CONSOLE_H

void bm_board_init(void);
void bm_console_init(void);
void bm_putc(char c);
void bm_puts(const char *s);
void bm_putu(unsigned long v);
void bm_puthex(unsigned int v);

/* newlibc raw-serial console API on 7201 channel A (§7i, serial_loopback_test:
 * polled TX/RX for a hardware TXD->RXD loopback; aliased to console_* in
 * bm_shim.c). */
void bm_console_putc(char c);
int bm_console_getc(void);
int bm_console_getc_nonblock(void);
int bm_console_rx_ready(void);

#endif
