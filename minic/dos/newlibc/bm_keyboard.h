/* bm_keyboard.h -- bare-metal Victor 9000 keyboard driver (§6e). */
#ifndef BM_KEYBOARD_H
#define BM_KEYBOARD_H

/* Interrupt-driven keyboard via VIA CS2 + the dedicated KBINT line on
 * IR6.  Call bm_keyboard_init() AFTER bm_interrupts_init() (PIC must be
 * re-initialized first) and BEFORE bm_interrupts_enable(); it programs
 * the VIA, installs the IR6 ISR, and unmasks IR6. */
void bm_keyboard_init(void);

/* Cooked (ASCII) reads.  -1 when no key.  The Shift/RPT(Ctrl)/Alt
 * subsets and the MAME S88-Return compatibility path mirror newlibc's
 * MAME-validated map. */
int bm_keyboard_getc(void);
int bm_keyboard_getc_nonblock(void);
int bm_keyboard_hit(void);

/* Raw event diagnostic: bit 7 = key close/open, bits 0-6 = zero-based
 * physical key.  -1 when no event. */
int bm_keyboard_get_raw_event_nonblock(void);

/* Battery diagnostics: ISR entries and ring-buffer overruns. */
unsigned int bm_keyboard_isr_count(void);
unsigned int bm_keyboard_overruns(void);

#endif
