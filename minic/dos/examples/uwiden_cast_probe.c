/*
 * uwiden_cast_probe.c — minic: widening an UNSIGNED 16-bit value to a 32-bit
 * type must ZERO-extend (`extuw`), not sign-extend (`extsw`).
 *
 * The integer-cast handler in minic.y emitted `extsw` for EVERY w->l widening,
 * ignoring the source signedness.  For a signed source that is correct, but for
 * an unsigned source (`(uint32_t)(size_t)`) C requires the source VALUE to be
 * preserved, i.e. zero-extension.  Sign-extending an unsigned 0xFFFF gives
 * 0xFFFFFFFF instead of 0x0000FFFF, corrupting the high word.
 *
 * Canonical victim: MicroPython's MP_OBJ_FUN_MAKE_SIG packs an argument-count
 * signature as  ((uint32_t)n_args_max << 1) | ((uint32_t)n_args_min << 17) | kw
 * with n_args_max == 0xFFFF (== MP_OBJ_FUN_ARGS_MAX, an unsigned size_t).  A
 * sign-extended max set the high bits, so the decoder read n_args_min as 0x7FFF
 * (= sig >> 17) instead of 0; mp_arg_check_num then saw "0 args given, 32767
 * required", raised TypeError, whose construction re-ran mp_arg_check_num the
 * same way -> infinite recursion -> stack blowout.  This hung EVERY `raise`
 * (and so every try/except) on the real Victor.
 *
 * The widening is w->l in EVERY i8086 model (`long` is 4 bytes throughout), so
 * this is gated medium + compact + large; all are bug-loud without the fix.
 * Values flow through u16()/u32() so they are not constant-folded.
 */
#include <stdio.h>

static unsigned int u16(unsigned int v) { return v; }

int main(void)
{
	unsigned int us;
	unsigned long w, sig, lo, hi;

	/* (1) plain widening cast: unsigned 0xFFFF -> 0x0000FFFF, not 0xFFFFFFFF */
	us = u16(0xFFFFu);
	w = (unsigned long)us;
	if (w == 0xFFFFUL) printf("zext ok\r\n"); else printf("zext FAIL\r\n");

	/* (2) shift after the cast: (u32)0xFFFF << 1 == 0x1FFFE, not 0xFFFFFFFE */
	us = u16(0xFFFFu);
	w = (unsigned long)us << 1;
	if (w == 0x1FFFEUL) printf("shl ok\r\n"); else printf("shl FAIL\r\n");

	/* (3) MP_OBJ_FUN_MAKE_SIG packing + decode: max=0xFFFF, min=0 */
	us = u16(0xFFFFu);
	sig = ((unsigned long)us << 1) | ((unsigned long)0u << 17);
	hi = sig >> 17;             /* the n_args_min field -> must be 0  */
	lo = (sig >> 1) & 0xFFFFUL; /* the n_args_max field -> must be 0xFFFF */
	if (hi == 0UL) printf("min ok\r\n"); else printf("min FAIL\r\n");
	if (lo == 0xFFFFUL) printf("max ok\r\n"); else printf("max FAIL\r\n");

	return 0;
}
