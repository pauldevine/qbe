/* bm_display.h -- bare-metal Victor 9000 text display driver (§6e). */
#ifndef BM_DISPLAY_H
#define BM_DISPLAY_H

#include <stdint.h>

#define DISPLAY_COLS         80
#define DISPLAY_ROWS         25

/* Victor 9000 attributes (upper byte of the screen word). */
#define ATTR_NORMAL          0x00   /* bright normal video (LOWINT clear) */
#define ATTR_LOW             0x40   /* low intensity */
#define ATTR_REVERSE         0x80   /* bright reversed video */
#define ATTR_UNDERLINE       0x20   /* bright underline */

void bm_display_init(void);
void bm_display_clear(void);
void bm_display_putc(char c);
void bm_display_puts(const char *str);
void bm_display_putc_at(uint8_t row, uint8_t col, char c, uint8_t attr);
void bm_display_set_cursor(uint8_t row, uint8_t col);
void bm_display_get_cursor(uint8_t *row, uint8_t *col);
void bm_display_scroll(void);

/* Self-check hooks for the bare-metal battery: raw VRAM cell readback
 * and CRTC register readback let a test verify the screen contents over
 * the serial console without a host-side screen dump. */
uint16_t bm_display_read_cell(uint16_t pos);
uint8_t bm_display_read_crtc(uint8_t reg);
uint16_t bm_display_screen_word(char c, uint8_t attr);

#endif
