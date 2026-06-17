/*
 * display_upstream_bm.c -- bare-metal Victor 9000 display test driving the
 * UPSTREAM newlibc drivers/display.c (§8m, the §8l pattern applied to a
 * second driver).
 *
 * Where display_bm.c (§6e) exercises the hand-mirrored bm_display.c port,
 * this links and runs newlibc's OWN drivers/display.c (+ drivers/font_data.c)
 * -- the file the §8k gas->nasm in-place port made minic-compilable (its
 * write_crtc_reg/read_crtc_reg now fork `#if defined(__MINIC__)` to
 * HW_WRITE_BYTE/HW_READ_BYTE).  §8k proved that file COMPILES; this proves it
 * RUNS on the bare machine -- the Phase-6 end-state where newlibc's own
 * drivers replace the bm_*.c mirrors.
 *
 * No interrupts here (the display driver is pure polled MMIO): the test
 * drives the upstream display_init/clear/puts/putc/putc_at/set_cursor/
 * get_cursor/scroll API and verifies every effect by reading it back from
 * the machine WITHOUT a host-side screen dump -- VRAM words through a far
 * pointer to F000:0000, the font from its 0000:0C00 RAM home, the cursor via
 * the upstream display_get_cursor (which reads CRTC R14/R15, the only
 * registers a real 6845 lets you read back) -- and reports over serial.
 * NOTHING from bm_display.c / bm_font_data.c is linked.
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "v9k_hw.h"        /* MK_FP, VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE */
#include "bm_console.h"    /* serial console: bm_puts */
#include "display.h"       /* UPSTREAM: display_* API + DISPLAY_COLS, ATTR_* */
#include "font_data.h"     /* UPSTREAM: victor_font, BYTES_PER_GLYPH, FONT_RAM_ADDR */

#define GLYPH_OFFSET 0x60  /* glyph_ptr = char + 0x60 (font @ 0x0C00) */

static int fails;

/* A screen word is (attr << 8) | glyph_ptr, glyph_ptr = char + 0x60 -- the
 * same encoding the upstream make_screen_word uses (display.c). */
static uint16_t screen_word(char c, uint8_t attr) {
    uint8_t glyph_ptr = (uint8_t)((uint8_t)c + GLYPH_OFFSET);
    return (uint16_t)(((uint16_t)attr << 8) | glyph_ptr);
}

/* Raw VRAM cell readback: the 16-bit word at F000:0000 + pos. */
static uint16_t read_cell(uint16_t pos) {
    volatile uint16_t __far *vram =
        (volatile uint16_t __far *)MK_FP(VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE);
    return vram[pos];
}

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
    return read_cell(pos) == screen_word(c, attr);
}

static int cursor_is(uint8_t row, uint8_t col) {
    uint8_t r, c;

    display_get_cursor(&r, &c);
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
    bm_puts("Victor 9000 bare-metal UPSTREAM display.c test (qbe/minic)\n");

    bm_puts("phase 1: upstream display_init (font + CRTC + clear)\n");
    display_init();

    check("phase 2: cursor homed after clear: ", cursor_is(0, 0));

    check("phase 3: VRAM blank word is glyph-ptr 0x80: ",
          read_cell(0) == 0x0080 &&
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
    display_puts("Hello, Victor 9000!");
    if (row_matches(0, "Hello, Victor 9000!") && cursor_is(0, 19)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    display_putc_at(24, 79, 'Z', ATTR_REVERSE);
    check("phase 6: putc_at honors position + attribute: ",
          read_cell(24 * DISPLAY_COLS + 79)
              == screen_word('Z', ATTR_REVERSE));

    bm_puts("phase 7: newline blanks rest of row, moves cursor: ");
    display_putc('\n');
    if (cell_is(19, ' ', 0) && cell_is(79, ' ', 0) && cursor_is(1, 0)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 8: tab + backspace cursor movement: ");
    display_putc('\t');
    ok = cursor_is(1, 8);
    display_putc('\b');
    if (ok && cursor_is(1, 7) && cell_is(1 * DISPLAY_COLS + 7, ' ', 0)) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO\n");
        fails++;
    }

    bm_puts("phase 9: scroll from bottom row: ");
    display_set_cursor(24, 0);
    display_puts("bottom line");
    display_putc('\n');
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
        bm_puts("PASS: upstream display.c bare-metal checks completed.\n");
    else
        bm_puts("FAIL: upstream display.c bare-metal checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
