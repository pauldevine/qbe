/*
 * crtc_bm.c -- bare-metal Victor 9000 CRTC test (§6f, Phase-6 step 4c).
 *
 * A minic-dialect port of newlibc's tests/crtc_test.c, adapted to what
 * a 6845 actually lets you verify: config registers (R0..R13) are
 * WRITE-ONLY, so unlike the original's best-effort register dump this
 * test asserts only the cursor pair R14/R15 -- the one readback path
 * the chip supports -- and proves the rest of the bring-up through its
 * effects (screen RAM accessible, pattern intact).
 *
 * Coverage distinct from display_bm: screen words are written through
 * this test's OWN far pointer (not the driver's putc path) and read
 * back through the driver hook, and the cursor checks are raw R14/R15
 * byte values for known positions rather than the driver's decoded
 * row/col.  Results over serial; every phase prints first (5 MHz rule).
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_console.h"
#include "bm_display.h"

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

int main(void) {
    volatile uint16_t __far *video_ram;
    uint16_t expected;
    int i, errors;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal CRTC test (qbe/minic)\n");

    bm_puts("phase 1: display init (CRTC bring-up + font + clear)\n");
    bm_display_init();

    /* Raw VRAM word writes through our own far pointer: A..Z on row 0,
     * 0..9 on row 1, in the Victor (attr<<8)|glyph_ptr encoding. */
    bm_puts("phase 2: write A..Z + 0..9 screen words via far pointer\n");
    video_ram = (volatile uint16_t __far *)
        MK_FP(VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE);
    for (i = 0; i < 26; i++)
        video_ram[i] = bm_display_screen_word((char)('A' + i), ATTR_NORMAL);
    for (i = 0; i < 10; i++)
        video_ram[DISPLAY_COLS + i] =
            bm_display_screen_word((char)('0' + i), ATTR_NORMAL);

    bm_puts("phase 3: read back pattern through driver hook: ");
    errors = 0;
    for (i = 0; i < 26; i++) {
        expected = bm_display_screen_word((char)('A' + i), ATTR_NORMAL);
        if (bm_display_read_cell((uint16_t)i) != expected)
            errors++;
    }
    for (i = 0; i < 10; i++) {
        expected = bm_display_screen_word((char)('0' + i), ATTR_NORMAL);
        if (bm_display_read_cell((uint16_t)(DISPLAY_COLS + i)) != expected)
            errors++;
    }
    if (errors == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu((unsigned long)errors);
        bm_puts(" bad)\n");
        fails++;
    }

    /* Cursor position (12,34) -> word address 12*80+34 = 994 = 0x03E2.
     * R14/R15 are the only 6845 registers with readback. */
    bm_puts("phase 4: cursor (12,34) reads back R14=0x03 R15=0xe2: ");
    bm_display_set_cursor(12, 34);
    if (bm_display_read_crtc(14) == 0x03 &&
        bm_display_read_crtc(15) == 0xE2) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_puthex(bm_display_read_crtc(14));
        bm_puts(" ");
        bm_puthex(bm_display_read_crtc(15));
        bm_puts(")\n");
        fails++;
    }

    bm_display_set_cursor(0, 0);
    check("phase 5: cursor home reads back R14=R15=0: ",
          bm_display_read_crtc(14) == 0 && bm_display_read_crtc(15) == 0);

    if (fails == 0)
        bm_puts("PASS: bare-metal CRTC checks completed.\n");
    else
        bm_puts("FAIL: bare-metal CRTC checks failed.\n");
    bm_puts("__V9END__\n");
    return 0;
}
