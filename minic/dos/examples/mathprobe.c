/*
 * mathprobe.c -- long / 32-bit integer runtime gate.
 *
 * `long_math.c` size-budgets the tiny/.COM build path (which runs the
 * Kl divmod32 libstub helpers) but never actually executes the
 * program.  This probe runs the same Kl-heavy code under DOSBox and
 * diffs the output, catching runtime regressions in:
 *
 *   - 32-bit signed/unsigned multiplication via the i8086 backend
 *     (Kl Omul, kl_stage_arg).  Both operands fit in 16 bits but the
 *     product needs DX:AX.  See [[i8086-kl-add-sub-mul-r1-alias]].
 *   - 32-bit signed/unsigned division/remainder via libstub
 *     `_qbe_div32{s,u}` / `_qbe_rem32{s,u}` (see
 *     [[i8086-div32-helper]]).
 *   - Sign-extension of `int` -> `long` (e.g. `int n = -1; long l = n;`).
 *   - 32-bit zero/positive values in `%lx` that exercise both halves
 *     of DX:AX (e.g. 0x12345678 has non-zero high word).
 *   - `printf` `%ld` / `%lu` / `%lx` ABI: minic must push 4 bytes per
 *     long vararg, libstub `_sprintf` must consume 4 bytes via the
 *     `l` flag.  See [[minic-long-vararg-truncated]].
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/mathprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/mathprobe/mathprobe.exe \
 *             | diff - minic/dos/tests/mathprobe.golden.txt
 *
 * Validation pattern: each printf line carries exactly one long-typed
 * vararg, matching the single-int-return shape used elsewhere.
 */

#include <stdio.h>

static char buf[48];

int main()
{
	long a;
	long b;
	long mul;
	long sdiv;
	long srem;
	long sext;
	unsigned long ua;
	unsigned long ub;
	unsigned long umul;
	unsigned long udiv;
	unsigned long urem;
	int ni;

	/* === 32-bit signed multiplication ===
	 * 1000 * 1000 = 1_000_000 (>16 bits product). */
	a = 1000L;
	b = 1000L;
	mul = a * b;
	sprintf(buf, "smul=%ld (want 1000000)\r\n", mul);
	printf("%s", buf);

	/* Negative * negative = positive across overflow. */
	a = -1234L;
	b = -5678L;
	mul = a * b;                          /* 7006652 */
	sprintf(buf, "smul_neg=%ld (want 7006652)\r\n", mul);
	printf("%s", buf);

	/* Negative * positive = negative. */
	a = -3000L;
	b = 5000L;
	mul = a * b;                          /* -15000000 */
	sprintf(buf, "smul_mix=%ld (want -15000000)\r\n", mul);
	printf("%s", buf);

	/* === 32-bit unsigned multiplication === */
	ua = 50000UL;
	ub = 50000UL;
	umul = ua * ub;                       /* 2_500_000_000 — high bit set */
	sprintf(buf, "umul=%lu (want 2500000000)\r\n", umul);
	printf("%s", buf);

	/* === 32-bit signed div / rem ===
	 * trunc-toward-zero quotient, remainder has sign of dividend. */
	a = 1000000L;
	b = 3L;
	sdiv = a / b;                         /* 333333 */
	srem = a % b;                         /* 1 */
	sprintf(buf, "sdiv=%ld (want 333333)\r\n", sdiv);
	printf("%s", buf);
	sprintf(buf, "srem=%ld (want 1)\r\n", srem);
	printf("%s", buf);

	a = -100000L;
	b = 7L;
	sdiv = a / b;                         /* -14285 */
	srem = a % b;                         /* -5 */
	sprintf(buf, "sdiv_neg=%ld (want -14285)\r\n", sdiv);
	printf("%s", buf);
	sprintf(buf, "srem_neg=%ld (want -5)\r\n", srem);
	printf("%s", buf);

	/* === 32-bit unsigned div / rem === */
	ua = 4000000000UL;
	ub = 7UL;
	udiv = ua / ub;                       /* 571428571 */
	urem = ua % ub;                       /* 3 */
	sprintf(buf, "udiv=%lu (want 571428571)\r\n", udiv);
	printf("%s", buf);
	sprintf(buf, "urem=%lu (want 3)\r\n", urem);
	printf("%s", buf);

	/* === Sign-extension int -> long ===
	 * The codegen must emit cbw/cwd (or equivalent) to fill the upper
	 * 16 bits of DX from sign of AX. */
	ni = -1;
	sext = ni;                            /* expect 0xFFFFFFFF = -1 long */
	sprintf(buf, "sext_neg=%ld (want -1)\r\n", sext);
	printf("%s", buf);
	sprintf(buf, "sext_neg_hex=%lx (want ffffffff)\r\n", sext);
	printf("%s", buf);

	ni = -32768;                          /* int min on 16-bit target */
	sext = ni;
	sprintf(buf, "sext_min=%ld (want -32768)\r\n", sext);
	printf("%s", buf);

	ni = 32767;                           /* int max */
	sext = ni;
	sprintf(buf, "sext_max=%ld (want 32767)\r\n", sext);
	printf("%s", buf);

	/* === %lx with both halves of DX:AX non-zero ===
	 * 0x12345678 = high word 0x1234, low word 0x5678. */
	ua = 0x12345678UL;
	sprintf(buf, "hex_full=%lx (want 12345678)\r\n", ua);
	printf("%s", buf);

	ua = 0x00005678UL;                    /* high zero, low nonzero */
	sprintf(buf, "hex_lo=%lx (want 5678)\r\n", ua);
	printf("%s", buf);

	ua = 0x12340000UL;                    /* high nonzero, low zero */
	sprintf(buf, "hex_hi=%lx (want 12340000)\r\n", ua);
	printf("%s", buf);

	/* === Add / sub across 16-bit boundary === */
	a = 70000L;
	b = 40000L;
	sprintf(buf, "sadd=%ld (want 110000)\r\n", a + b);
	printf("%s", buf);
	sprintf(buf, "ssub=%ld (want 30000)\r\n", a - b);
	printf("%s", buf);

	/* Subtract that crosses zero. */
	a = 5L;
	b = 100000L;
	sprintf(buf, "ssub_neg=%ld (want -99995)\r\n", a - b);
	printf("%s", buf);

	/* === Cast / mix int and long === */
	ni = 1000;
	a = (long)ni * 3000L;                 /* 3_000_000 */
	sprintf(buf, "cast_mul=%ld (want 3000000)\r\n", a);
	printf("%s", buf);

	return 0;
}
