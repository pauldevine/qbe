/*
 * bm_crt0.c -- bare-metal C runtime entry, built by minic (§6c, Phase-6
 * step 3).
 *
 * The raw-binary image head is omf_link.py's synthesized stub (--raw-binary):
 * it runs at the load address with CS=DS=SS=0 (the MAME Lua loader's raw
 * registers), establishes SS:SP and DS=ES=DGROUP, and far-jumps here.  By
 * the time C code runs the world is already sane, and BSS needs no clearing
 * because the raw image materializes BSS as zero bytes.
 *
 * So the C-side crt0 is just: board init (serial console for now), call
 * main, halt forever.  No DOS anywhere — exit/abort-style libstub entry
 * points must not be reached on bare metal (they INT 21h).
 */

#include "bm_console.h"

extern int main(int argc, char **argv);

int start(void) {            /* OMF `_start`, the --entry symbol */
    bm_board_init();
    main(0, (char **)0);
    while (1) {
        __asm__ volatile ("hlt");
    }
    return 0;
}
