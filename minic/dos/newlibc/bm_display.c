/*
 * bm_display.c -- bare-metal Victor 9000 text display driver (§6e,
 * Phase-6 step 4b).
 *
 * A minic-dialect port of newlibc's drivers/display.c.  All the
 * validated hardware facts are preserved:
 *   - the Victor has NO character ROM: the 8 KB font must be copied to
 *     RAM at 0000:0C00 BEFORE the CRTC can display anything;
 *   - VRAM cells are 16-bit words at F000:0000 holding glyph POINTERS,
 *     not ASCII -- glyph_ptr = char + 0x60 with the font at 0x0C00
 *     (the CRTC fetches glyph_ptr * 32);
 *   - the 6845 CRTC at E800:0000 (address) / E800:0001 (data) takes the
 *     16-register 80x25 table from the boot ROM, with a settle delay
 *     between address and data writes;
 *   - VIA2 brightness/contrast (E800:0040 = 0x54, E800:0042 = 0xFF)
 *     must be set or the screen stays dark even with the CRTC running.
 *
 * The original's write_crtc_reg/read_crtc_reg inline asm (push es / mov
 * es / es: byte store) is exactly what a minic volatile-far MMIO store
 * compiles to, so the port is pure C.
 */

#include <stdint.h>
#include <stddef.h>
#include "v9k_hw.h"
#include "bm_font_data.h"
#include "bm_display.h"

#define TAB_WIDTH            8
#define DEFAULT_ATTR         0x00
#define GLYPH_OFFSET         0x60   /* glyph_ptr = char + 0x60 (font @ 0xC00) */

/* CRTC register values for 80x25 text mode, from the Victor boot ROM
 * (BT1BASE.ASM reset_table). */
static const uint8_t crtc_init_values[16] = {
    92,     /* R0: Horizontal Total */
    80,     /* R1: Horizontal Displayed */
    81,     /* R2: Horizontal Sync Position */
    0xCF,   /* R3: Sync Width */
    25,     /* R4: Vertical Total */
    6,      /* R5: Vertical Total Adjust */
    25,     /* R6: Vertical Displayed */
    25,     /* R7: Vertical Sync Position */
    3,      /* R8: Interlace Mode */
    14,     /* R9: Max Scan Line */
    0x60,   /* R10: Cursor Start */
    0x0F,   /* R11: Cursor End */
    0,      /* R12: Start Address (H) */
    0,      /* R13: Start Address (L) */
    0,      /* R14: Cursor Address (H) */
    0       /* R15: Cursor Address (L) */
};

static volatile uint16_t __far *video_ram;   /* 16-bit words, not bytes */

/* The boot ROM waits ~100us between CRTC register writes. */
static void crtc_delay(void) {
    volatile uint16_t i;

    for (i = 0; i < 200; i++)
        ;
}

static void write_crtc_reg(uint8_t reg, uint8_t value) {
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, CRTC_ADDR_REG, reg);
    crtc_delay();
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, CRTC_DATA_REG, value);
}

static uint8_t read_crtc_reg(uint8_t reg) {
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, CRTC_ADDR_REG, reg);
    crtc_delay();
    return HW_READ_BYTE(PHASE2_DEV_SEGMENT, CRTC_DATA_REG);
}

static uint16_t get_crtc_cursor(void) {
    uint8_t high = read_crtc_reg(14);
    uint8_t low = read_crtc_reg(15);

    return (uint16_t)(((uint16_t)high << 8) | low);
}

static void set_crtc_cursor(uint16_t pos) {
    write_crtc_reg(14, (uint8_t)(pos >> 8));
    write_crtc_reg(15, (uint8_t)(pos & 0xFF));
}

/* The load-bearing Victor fact: a screen word is (attr << 8) | glyph
 * POINTER, where glyph_ptr = char + 0x60 for the 0x0C00 font base. */
static uint16_t make_screen_word(char c, uint8_t attr) {
    uint8_t glyph_ptr = (uint8_t)((uint8_t)c + GLYPH_OFFSET);

    return (uint16_t)(((uint16_t)attr << 8) | glyph_ptr);
}

static void crtc_init(void) {
    uint8_t i;

    for (i = 0; i < 16; i++)
        write_crtc_reg(i, crtc_init_values[i]);
}

/* No character ROM: copy the native-format font (256 glyphs x 32
 * bytes) to physical 0000:0C00. */
static void display_load_fonts(void) {
    volatile uint8_t __far *dest;
    const uint8_t *src;
    uint16_t i;

    dest = (volatile uint8_t __far *)MK_FP(0x0000, FONT_RAM_ADDR);
    src = victor_font;
    for (i = 0; i < VICTOR_FONT_SIZE; i++)
        dest[i] = src[i];
}

void bm_display_init(void) {
    /* 1: fonts first -- the CRTC renders from RAM. */
    display_load_fonts();

    /* 2: VIA brightness/contrast, or the screen stays dark. */
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, 0x0040, 0x54);
    HW_WRITE_BYTE(PHASE2_DEV_SEGMENT, 0x0042, 0xFF);

    /* 3: program the CRTC for 80x25. */
    crtc_init();

    /* 4: VRAM base, then a clean screen. */
    video_ram = (volatile uint16_t __far *)
        MK_FP(VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE);
    bm_display_clear();
}

void bm_display_clear(void) {
    uint16_t i;
    uint16_t blank_word = make_screen_word(' ', DEFAULT_ATTR);

    for (i = 0; i < DISPLAY_COLS * DISPLAY_ROWS; i++)
        video_ram[i] = blank_word;
    set_crtc_cursor(0);
}

void bm_display_set_cursor(uint8_t row, uint8_t col) {
    uint16_t pos;

    if (row < DISPLAY_ROWS && col < DISPLAY_COLS) {
        pos = (uint16_t)(row * DISPLAY_COLS + col);
        set_crtc_cursor(pos);
    }
}

void bm_display_get_cursor(uint8_t *row, uint8_t *col) {
    uint16_t pos = get_crtc_cursor();

    if (row)
        *row = (uint8_t)(pos / DISPLAY_COLS);
    if (col)
        *col = (uint8_t)(pos % DISPLAY_COLS);
}

void bm_display_scroll(void) {
    uint16_t i;
    uint16_t blank_word = make_screen_word(' ', DEFAULT_ATTR);

    for (i = 0; i < (DISPLAY_ROWS - 1) * DISPLAY_COLS; i++)
        video_ram[i] = video_ram[i + DISPLAY_COLS];
    for (i = (DISPLAY_ROWS - 1) * DISPLAY_COLS;
         i < DISPLAY_ROWS * DISPLAY_COLS; i++)
        video_ram[i] = blank_word;
}

void bm_display_putc(char c) {
    uint16_t pos;
    int col;

    switch (c) {
    case '\n':
        pos = get_crtc_cursor();
        col = pos % DISPLAY_COLS;
        while (col < DISPLAY_COLS) {
            video_ram[pos] = make_screen_word(' ', DEFAULT_ATTR);
            pos++;
            col++;
        }
        if (pos >= DISPLAY_COLS * DISPLAY_ROWS) {
            bm_display_scroll();
            pos = (DISPLAY_ROWS - 1) * DISPLAY_COLS;
        }
        set_crtc_cursor(pos);
        break;

    case '\r':
        pos = get_crtc_cursor();
        pos = (uint16_t)((pos / DISPLAY_COLS) * DISPLAY_COLS);
        set_crtc_cursor(pos);
        break;

    case '\t':
        pos = get_crtc_cursor();
        col = pos % DISPLAY_COLS;
        col = (col + TAB_WIDTH) & ~(TAB_WIDTH - 1);
        if (col >= DISPLAY_COLS) {
            pos = (uint16_t)(pos + (DISPLAY_COLS - (pos % DISPLAY_COLS)));
        } else {
            pos = (uint16_t)((pos / DISPLAY_COLS) * DISPLAY_COLS + col);
        }
        if (pos >= DISPLAY_COLS * DISPLAY_ROWS) {
            bm_display_scroll();
            pos = (DISPLAY_ROWS - 1) * DISPLAY_COLS;
        }
        set_crtc_cursor(pos);
        break;

    case '\b':
        pos = get_crtc_cursor();
        if (pos > 0) {
            pos--;
            video_ram[pos] = make_screen_word(' ', DEFAULT_ATTR);
            set_crtc_cursor(pos);
        }
        break;

    default:
        if (c >= 32) {
            pos = get_crtc_cursor();
            video_ram[pos] = make_screen_word(c, DEFAULT_ATTR);
            pos++;
            if (pos >= DISPLAY_COLS * DISPLAY_ROWS) {
                bm_display_scroll();
                pos = (DISPLAY_ROWS - 1) * DISPLAY_COLS;
            }
            set_crtc_cursor(pos);
        }
        break;
    }
}

void bm_display_puts(const char *str) {
    if (str == NULL)
        return;
    while (*str) {
        bm_display_putc(*str);
        str++;
    }
}

void bm_display_putc_at(uint8_t row, uint8_t col, char c, uint8_t attr) {
    uint16_t word_offset;

    if (row < DISPLAY_ROWS && col < DISPLAY_COLS) {
        word_offset = (uint16_t)(row * DISPLAY_COLS + col);
        video_ram[word_offset] = make_screen_word(c, attr);
    }
}

/* Battery self-check hooks. */
uint16_t bm_display_read_cell(uint16_t pos) {
    if (pos >= DISPLAY_COLS * DISPLAY_ROWS)
        return 0xFFFF;
    return video_ram[pos];
}

uint8_t bm_display_read_crtc(uint8_t reg) {
    return read_crtc_reg(reg);
}

uint16_t bm_display_screen_word(char c, uint8_t attr) {
    return make_screen_word(c, attr);
}
