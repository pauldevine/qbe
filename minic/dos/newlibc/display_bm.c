/*
 * display_bm.c -- bare-metal Victor 9000 display driver test (§6e,
 * Phase-6 step 4b).
 *
 * Drives the minic-built display stack (font load to 0000:0C00, 6845
 * CRTC bring-up, VRAM glyph-pointer writes) and verifies it WITHOUT a
 * host-side screen dump: every effect is read back from the machine --
 * VRAM cells through a far pointer, the font from its RAM home, the
 * cursor from CRTC R14/R15 (the only registers a real 6845 lets you
 * read back) -- and reported over the serial console.
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_console.h"
#include "bm_display.h"
#include "bm_font_data.h"

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

static int cell_is(uint16_t pos, char c, uint8_t attr) {
    return bm_display_read_cell(pos) == bm_display_screen_word(c, attr);
}

static int cursor_is(uint8_t row, uint8_t col) {
    uint8_t r, c;

    bm_display_get_cursor(&r, &c);
    return r == row && c == col;
}

static int row_matches(uint8_t row, const char *s) {
    uint16_t pos;
    int i;

    pos = (uint16_t)(row * DISPLAY_COLS);
    for (i = 0; s[i] != 0; i++) {
        if (!cell_is((uint16_t)(pos + i), s[i], 0))
            return 0;
    }
    return 1;
}

int main(void) {
    int i, ok;
    volatile uint8_t __far *font_ram;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal display test (qbe/minic)\n");

    bm_puts("phase 1: display init (font + CRTC + clear)\n");
    bm_display_init();

    check("phase 2: cursor homed after clear: ", cursor_is(0, 0));

    check("phase 3: VRAM blank word is glyph-ptr 0x80: ",
          bm_display_read_cell(0) == 0x0080 &&
          cell_is(1999, ' ', 0));

    /* The font lives in RAM at 0000:0C00 (no character ROM): the 'A'
     * glyph (ptr 0x41+0x60=0xA1) must match the compiled-in table. */
    bm_puts("phase 4: font RAM matches table at glyph 'A': ");
    font_ram = (volatile uint8_t __far *)MK_FP(0x0000, FONT_RAM_ADDR);
    ok = 1;
    for (i = 0; i < BYTES_PER_GLYPH; i++) {
        if (font_ram[0xA1 * BYTES_PER_GLYPH + i]
            != victor_font[0xA1 * BYTES_PER_GLYPH + i])
            ok = 0;
    }
    if (ok) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 5: puts writes glyph pointers + advances cursor: ");
    bm_display_puts("Hello, Victor 9000!");
    if (row_matches(0, "Hello, Victor 9000!") && cursor_is(0, 19)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_display_putc_at(24, 79, 'Z', ATTR_REVERSE);
    check("phase 6: putc_at honors position + attribute: ",
          bm_display_read_cell(24 * DISPLAY_COLS + 79)
              == bm_display_screen_word('Z', ATTR_REVERSE));

    bm_puts("phase 7: newline blanks rest of row, moves cursor: ");
    bm_display_putc('\n');
    if (cell_is(19, ' ', 0) && cell_is(79, ' ', 0) && cursor_is(1, 0)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: tab + backspace cursor movement: ");
    bm_display_putc('\t');
    ok = cursor_is(1, 8);
    bm_display_putc('\b');
    if (ok && cursor_is(1, 7) && cell_is(1 * DISPLAY_COLS + 7, ' ', 0)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: scroll from bottom row: ");
    bm_display_set_cursor(24, 0);
    bm_display_puts("bottom line");
    bm_display_putc('\n');
    /* The '\n' on row 24 scrolls: "bottom line" moves to row 23, row 24
     * is blank, the row-0 banner is gone, cursor stays on row 24. */
    if (row_matches(23, "bottom line") &&
        cell_is(24 * DISPLAY_COLS, ' ', 0) &&
        !row_matches(0, "Hello") &&
        cursor_is(24, 0)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    if (fails == 0)
        bm_puts("PASS: bare-metal display checks completed.\n");
    else
        bm_puts("FAIL: bare-metal display checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
