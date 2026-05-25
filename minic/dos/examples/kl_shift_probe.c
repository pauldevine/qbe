/*
 * kl_shift_probe.c — exercises Oshl/Oshr/Osar Kl with both constant
 * and variable shift counts.
 *
 * The Kl shift handlers in i8086/emit.c use AX, DX, and CX as scratch
 * without telling rega (see [[i8086-kl-shift-clobbers-ax]]).  A live
 * temp pinned to AX/DX (e.g. across the load32_dxax) or CX (the
 * variable-shift loop counter) gets silently zeroed across the shift.
 * This probe pins behavioural correctness post-fix.
 *
 * Values arrive via function parameters typed `unsigned long` (Kl) so
 * (a) QBE const-prop cannot fold the shift, (b) we sidestep the
 * pre-existing minic `(unsigned long)unsigned_int` extsw bug.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/kl_shift_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/kl_shift_probe/kl_shift_probe.exe) \
 *              minic/dos/tests/kl_shift_probe.golden.txt
 */

#include <stdio.h>

/* Constant shift exactly 16: hits the >=16 special-case where
 * `shl dx, n-16` is skipped entirely.  Was already covered before
 * this fix but exercises the AX/DX clobber bracket. */
unsigned long shl16(unsigned long a)
{
	return a << 16;
}

/* Constant shift < 16: hits the CX-loop counter path. */
unsigned long shl8(unsigned long a)
{
	return a << 8;
}

/* Constant shift > 16: exercises emit_shift_imm (mov cl, N; shl dx, cl)
 * on 8086 — the prior `shl dx, %d` form would be rejected by NASM. */
unsigned long shl24(unsigned long a)
{
	return a << 24;
}

/* Variable shift — both operands as parameters.  Exercises the
 * variable-shift path (jcxz/loop) where r1 (count) must reach CX. */
unsigned long var_shl(unsigned long a, unsigned int n)
{
	return a << n;
}

/* Logical right shift, constant > 16 (exercises emit_shift_imm). */
unsigned long shr20(unsigned long x)
{
	return x >> 20;
}

/* Logical right shift, constant < 16. */
unsigned long shr4(unsigned long x)
{
	return x >> 4;
}

/* Variable logical right shift. */
unsigned long var_shr(unsigned long x, unsigned int n)
{
	return x >> n;
}

/* Signed `>>` MUST be arithmetic (sar): the sign bit must be
 * propagated into the vacated high bits.  Before the minic.y fix,
 * `>>` was always lowered to Oshr (logical), so a negative `long`
 * shifted right gave the wrong (zero-filled) result.  These cases
 * pin the lowering at the C-language level and incidentally
 * exercise the Osar Kl/Kw backend handlers from real C code. */

long signed_shr4(long x)
{
	return x >> 4;
}

long signed_shr20(long x)
{
	return x >> 20;
}

long var_sshr(long x, int n)
{
	return x >> n;
}

/* Osar Kw — signed int (2 bytes) shift right.  Same lowering bug at
 * the frontend; backend handler is separate from the Kl path. */
int signed_int_shr(int x)
{
	return x >> 1;
}

int main(void)
{
	unsigned long ua;
	unsigned int  n;

	ua = 0xFFFFUL;
	printf("shl16=%lx (want ffff0000)\r\n", shl16(ua));

	ua = 0x12UL;
	printf("shl8=%lx (want 1200)\r\n", shl8(ua));

	ua = 0xABUL;
	printf("shl24=%lx (want ab000000)\r\n", shl24(ua));

	ua = 0xFFFFUL;
	n  = 16;
	printf("var_shl16=%lx (want ffff0000)\r\n", var_shl(ua, n));

	ua = 0x0001UL;
	n  = 4;
	printf("var_shl4=%lx (want 10)\r\n", var_shl(ua, n));

	ua = 0xBEEFUL;
	n  = 0;
	printf("var_shl0=%lx (want beef)\r\n", var_shl(ua, n));

	ua = 0xABCDEF00UL;
	printf("shr20=%lx (want abc)\r\n", shr20(ua));

	ua = 0xABCDEF00UL;
	printf("shr4=%lx (want abcdef0)\r\n", shr4(ua));

	ua = 0xFFFF0000UL;
	n  = 16;
	printf("var_shr16=%lx (want ffff)\r\n", var_shr(ua, n));

	ua = 0xDEADBEEFUL;
	n  = 4;
	printf("var_shr4=%lx (want deadbee)\r\n", var_shr(ua, n));

	/* Signed >> cases.  Stage hex literals through `long` locals — see
	 * [[minic-long-literal-int-vararg]] — and inspect via %lx to see
	 * the bit pattern (sign-fill vs zero-fill). */
	{
		long  sa;
		int   si;

		/* -16L >> 4 = -1L: arithmetic shift fills with sign bit. */
		sa = 0xFFFFFFF0L;
		printf("sshr4=%lx (want ffffffff)\r\n", signed_shr4(sa));

		/* -8388608 >> 20 = -8 (= 0xFFFFFFF8). */
		sa = 0xFF800000L;
		printf("sshr20=%lx (want fffffff8)\r\n", signed_shr20(sa));

		/* INT32_MIN >> 4 = 0xF8000000.  Variable shift, sign-fill. */
		sa = 0x80000000L;
		n  = 4;
		printf("var_sshr4=%lx (want f8000000)\r\n", var_sshr(sa, n));

		/* Positive operand: sar and shr agree.  Confirms the new
		 * path doesn't break unsigned-shaped values. */
		sa = 0x7FFFFFFFL;
		n  = 8;
		printf("var_sshr_pos=%lx (want 7fffff)\r\n", var_sshr(sa, n));

		/* Osar Kw — signed int >>1: -8 → -4 = 0xFFFC on 16-bit int. */
		si = -8;
		printf("sint_shr=%x (want fffc)\r\n", signed_int_shr(si));
	}

	return 0;
}
