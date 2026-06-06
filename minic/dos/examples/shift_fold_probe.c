/*
 * shift_fold_probe.c — QBE constant-folding of 32-bit (Kl) right shifts.
 *
 * Pins the fold.c fix: foldint() was called with w = (cls == Kl), i.e.
 * "fold as a 64-bit op".  On i8086 Kl is 32-bit (`long` / far ptr = 4
 * bytes), so a Kl shift must fold with 32-bit semantics.  With the old
 * 64-bit fold:
 *   - (long)0x80000000 >> 1  (arithmetic): 0x80000000 is a positive 64-bit
 *     value, so sar gave 0x40000000 instead of the 32-bit-correct
 *     0xC0000000 (sign bit is bit 31, not bit 63).
 *   - (unsigned long)0x80000000 >> 1 (logical): a sign-extended operand
 *     leaked high bits, giving 0xC0000000 instead of 0x40000000.
 *
 * This broke MicroPython's MP_SMALL_INT_MAX = ~((mp_int_t)MSBIT_HIGH >> 1),
 * which came out negative, so the small-int overflow check
 * `lhs > (MP_SMALL_INT_MAX >> n)` falsely tripped on EVERY `1 << n` —
 * t_int aborted the whole feature probe with a spurious OverflowError.
 *
 * Model-independent (Kl is 32-bit on every i8086 model); gated medium +
 * compact to cover near- and far-data.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/shift_fold_probe.c
 */

#include <stdio.h>

int main(void)
{
	/* Drive values through locals so the shift folds as a runtime Kl op
	 * (QBE copy-propagates the constant into the fold). */
	long sa = (long)0x80000000;          /* -2147483648 */
	unsigned long ua = 0x80000000UL;
	long one = 1;

	/* Arithmetic right shift of a 32-bit-negative value. */
	if ((sa >> 1) == (long)0xC0000000)
		printf("sar ok\r\n");
	else
		printf("sar FAIL %ld\r\n", sa >> 1);

	/* Logical right shift of 0x80000000. */
	if ((ua >> 1) == 0x40000000UL)
		printf("shr ok\r\n");
	else
		printf("shr FAIL %lu\r\n", ua >> 1);

	/* The exact MicroPython small-int constants (REPR_A, 32-bit word). */
	{
		unsigned long msbit = one << 31;          /* 0x80000000 */
		long smin = ((long)msbit) >> 1;           /* want 0xC0000000 */
		long smax = ~smin;                        /* want 0x3FFFFFFF */
		if (smin == (long)0xC0000000 && smax == 0x3FFFFFFFL)
			printf("smaxmin ok\r\n");
		else
			printf("smaxmin FAIL %ld %ld\r\n", smin, smax);

		/* The overflow check that `1 << n` runs: must NOT trip for 1<<10. */
		long n = 10;
		int ovf = (n >= 32 || one > (smax >> n) || one < (smin >> n));
		if (!ovf && (one << n) == 1024L)
			printf("noovf ok\r\n");
		else
			printf("noovf FAIL ovf=%d val=%ld\r\n", ovf, one << n);
	}

	return 0;
}
