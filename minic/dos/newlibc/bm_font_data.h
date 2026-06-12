/*******************************************************************************
 * font_data.h - Victor 9000 Font Data
 *******************************************************************************
 * Copyright (c) 2025 Victor 9000 newlibc Project
 *
 * Font glyphs extracted from myfreedos victor_font.bin for bare-metal display.
 *
 * CRITICAL: Victor 9000 has NO character ROM. Fonts must be loaded into RAM
 * at 0x00C00 before text can be displayed!
 ******************************************************************************/

#ifndef BM_FONT_DATA_H
#define BM_FONT_DATA_H

#include <stdint.h>

/*******************************************************************************
 * Font Format - Victor 9000 Native Format
 ******************************************************************************/

/* Victor 9000 font format: 32 bytes per glyph (16 scanlines x 2 bytes)
 * This matches the hardware format expected by the CRTC.
 * Font address calculation: glyph_pointer * 32
 */

#define VICTOR_FONT_SIZE    8192    /* 256 glyphs * 32 bytes each */
#define BYTES_PER_GLYPH     32      /* 16 scanlines * 2 bytes */

/*******************************************************************************
 * Font Data Array
 ******************************************************************************/

/* Complete Victor 9000 font (256 glyphs, 32 bytes each)
 * Source: myfreedos/boot/victor/victor_font.bin
 * This is the WORKING font from myfreedos that displays correctly.
 */
extern const uint8_t victor_font[VICTOR_FONT_SIZE];

/*******************************************************************************
 * Memory Layout
 ******************************************************************************/

/* Victor 9000 expects fonts to be loaded at this address
 * FONT_SEG = 0x00C0 -> Linear address 0x0C00
 * The fonts are in Victor native format (32 bytes/char) and can be
 * copied directly to RAM.
 */
#define FONT_RAM_ADDR       0x0C00  /* Font segment base address */

#endif /* BM_FONT_DATA_H */
