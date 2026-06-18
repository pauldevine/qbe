/*
 * asm_clobber_probe.c — bug-loud gate for the inline-asm register-clobber
 * fix (the §8z QBE regalloc fix).
 *
 * THE BUG: minic parsed an inline asm's clobber list ("ax","bx",...) but
 * emitted it only as a `# clobbers:` comment, so QBE's register allocator
 * was free to keep a live C value in a register the asm declared clobbered.
 * In particular BX/SI/DI are callee-saved here (so they survive C calls and
 * are the allocator's preferred home for a value live across calls), yet an
 * asm — e.g. a DOS `int 0x21` — that lists "bx" in its clobbers would trash
 * the value with the allocator none the wiser.  Surfaced first by newlibc's
 * test_es_preservation / test_ss_preservation on real Victor hardware (a
 * `cmp` against a DOS-clobbered BX printed a spurious "FAIL").
 *
 * THE PROBE: compute several values just before an inline asm that writes
 * junk into every GP register AND declares all of them clobbered, then use
 * the values after the asm.  Enough values are live across the asm to force
 * the allocator onto the callee-saved BX/SI/DI.  On the UNFIXED compiler the
 * post-asm reads see the junk; on the FIXED compiler the declared clobbers
 * keep the values off those registers (spilled / steered elsewhere), so the
 * sums are exact.  Model-independent integer golden.
 */
#include <stdio.h>

/* A real (non-inlined) call so the results are runtime temps the allocator
 * likes to keep in callee-saved registers across the following code. */
static int triple(int x) { return x * 3 + 1; }

int main(void)
{
	volatile int seed = 7;     /* defeat constant folding of the inputs */
	int a, b, c, d, e;

	a = triple(seed);          /* 22 */
	b = triple(seed + 1);      /* 25 */
	c = triple(seed + 2);      /* 28 */
	d = triple(seed + 3);      /* 31 */
	e = triple(seed + 4);      /* 34 */

	/* Trash every GP register and DECLARE it clobbered.  Without the fix,
	 * QBE keeps some of a..e in BX/SI/DI across this and reads junk after. */
	__asm__ volatile(
		"mov ax, 0x1111\n\t"
		"mov bx, 0x2222\n\t"
		"mov cx, 0x3333\n\t"
		"mov dx, 0x4444\n\t"
		"mov si, 0x5555\n\t"
		"mov di, 0x6666"
		: : : "ax", "bx", "cx", "dx", "si", "di", "memory");

	printf("a=%d b=%d c=%d d=%d e=%d\n", a, b, c, d, e);
	printf("sum=%d\n", a + b + c + d + e);   /* want 140 */
	printf("asm_clobber_probe done\n");
	return 0;
}
