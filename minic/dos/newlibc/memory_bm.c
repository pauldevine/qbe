/*
 * memory_bm.c -- bare-metal Victor 9000 memory write test (§6f,
 * Phase-6 step 4c).
 *
 * A minic-dialect port of newlibc's tests/memory_test.c: byte-pattern
 * writes to the two display memories -- font RAM at physical 0000:0C00
 * (no character ROM; the CRTC fetches glyphs from here) and screen RAM
 * at F000:0000 -- followed by full readback verification.  Distinct
 * patterns (i vs 0xFF-i) in the two regions also catch segment
 * aliasing: if the far stores landed in the same place, one pattern
 * would have destroyed the other.
 *
 * Unlike the original (which reported on the display it had just
 * scribbled over), results go out the polled serial console.  Every
 * phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_console.h"

#define FONT_RAM_OFF   0x0C00
#define TEST_BYTES     256

int main(void) {
    volatile uint8_t __far *font_ram;
    volatile uint8_t __far *screen_ram;
    uint16_t i;
    int font_errors, screen_errors;

    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal memory test (qbe/minic)\n");

    font_ram = (volatile uint8_t __far *)MK_FP(0x0000, FONT_RAM_OFF);
    screen_ram = (volatile uint8_t __far *)
        MK_FP(VIDEO_RAM_SEGMENT, VIDEO_RAM_BASE);

    bm_puts("phase 1: write 256-byte pattern to font RAM 0000:0C00\n");
    for (i = 0; i < TEST_BYTES; i++)
        font_ram[i] = (uint8_t)i;

    bm_puts("phase 2: write inverted pattern to screen RAM F000:0000\n");
    for (i = 0; i < TEST_BYTES; i++)
        screen_ram[i] = (uint8_t)(0xFF - i);

    bm_puts("phase 3: font RAM readback: ");
    font_errors = 0;
    for (i = 0; i < TEST_BYTES; i++) {
        if (font_ram[i] != (uint8_t)i)
            font_errors++;
    }
    if (font_errors == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu((unsigned long)font_errors);
        bm_puts(" bad)\n");
    }

    bm_puts("phase 4: screen RAM readback: ");
    screen_errors = 0;
    for (i = 0; i < TEST_BYTES; i++) {
        if (screen_ram[i] != (uint8_t)(0xFF - i))
            screen_errors++;
    }
    if (screen_errors == 0) {
        bm_puts("yes\n");
    } else {
        bm_puts("NO (");
        bm_putu((unsigned long)screen_errors);
        bm_puts(" bad)\n");
    }

    if (font_errors == 0 && screen_errors == 0)
        bm_puts("PASS: font and screen RAM byte writes verified.\n");
    else
        bm_puts("FAIL: memory write/readback mismatch.\n");
    bm_puts("__V9END__\n");
    return 0;
}
