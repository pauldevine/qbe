/* bm_console.h -- bare-metal Victor 9000 polled serial console (§6c). */
#ifndef BM_CONSOLE_H
#define BM_CONSOLE_H

void bm_board_init(void);
void bm_console_init(void);
void bm_putc(char c);
void bm_puts(const char *s);
void bm_putu(unsigned long v);
void bm_puthex(unsigned int v);

#endif
