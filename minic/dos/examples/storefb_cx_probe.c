/*
 * storefb_cx_probe.c — exercises the implicit CX clobber in the narrow
 * far-store handlers (Ostorefb / Ostorefh / Ostorefw).
 *
 * Pre-fix gap: each of the three narrow Ostoref* handlers stages the
 * value to be written through CX (via `mov cl, reg8` or `mov cx, reg16`)
 * as a scratch register, but never tells rega CX is clobbered.  Any
 * live SSA temp rega placed in CX is silently overwritten by the
 * store sequence.
 *
 * Surfaced by stevie compact's filetonext: the loop-counter-adjacent
 * local `nextra` lived in CX across the `*screenp = c` at @l52 every
 * loop iteration, and `mov cl, bl` (where BL was c.34) clobbered the
 * low byte of CX, propagating garbage through the @l52→@l9 phi-edge
 * back into the next iteration's nextra.  Tracker
 * [[qbe-rega-phi-slot-leak]] — see the (mm) section in CLAUDE.md.
 *
 * Fix mirrors the BX/ES save bracket already in place: add push cx /
 * pop cx around each storef{b,h,w} handler body in i8086/emit.c.
 *
 * Probe strategy: write to a far buffer in a loop while keeping an
 * integer accumulator live across each storef* call.  If CX gets
 * clobbered by the store sequence, the accumulator will diverge from
 * the expected sum.  Three variants exercise b/h/w widths separately.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/storefb_cx_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/storefb_cx_probe/storefb_cx_probe.exe) \
 *              minic/dos/tests/storefb_cx_probe.golden.txt
 */

#include <stdio.h>

static unsigned char  g_b[8];
static unsigned short g_h[8];
static unsigned short g_w[8];

int main(void)
{
	int i;
	int sum_b;
	int sum_h;
	int sum_w;
	unsigned char val_b;
	unsigned short val_h;
	unsigned short val_w;

	/* Test 1: Ostorefb in a loop with live accumulator.  The accumulator
	 * `sum_b` must survive the storefb; if CX is clobbered, sum_b
	 * diverges. */
	sum_b = 0;
	for (i = 0; i < 8; i = i + 1) {
		val_b = (unsigned char)(i + 1);     /* 1..8 */
		g_b[i] = val_b;                       /* storefb */
		sum_b = sum_b + i;                    /* needs to survive storefb */
	}
	/* Expected: sum_b = 0+1+2+...+7 = 28 */
	printf("byte_sum=%d\r\n", sum_b);

	/* Test 2: Ostorefh — same shape but storing shorts. */
	sum_h = 0;
	for (i = 0; i < 8; i = i + 1) {
		val_h = (unsigned short)((i + 1) * 100);
		g_h[i] = val_h;                       /* storefh */
		sum_h = sum_h + i;
	}
	printf("short_sum=%d\r\n", sum_h);

	/* Test 3: Ostorefw — word stores. */
	sum_w = 0;
	for (i = 0; i < 8; i = i + 1) {
		val_w = (unsigned short)((i + 1) * 1000);
		g_w[i] = val_w;                       /* storefw */
		sum_w = sum_w + i;
	}
	printf("word_sum=%d\r\n", sum_w);

	/* Test 4: read the buffers back to confirm the stores landed
	 * correctly (independent of CX clobber). */
	printf("buf_b 1=%d 4=%d 8=%d\r\n",
	    (int)(unsigned char)g_b[0],
	    (int)(unsigned char)g_b[3],
	    (int)(unsigned char)g_b[7]);
	printf("buf_h 100=%d 400=%d 800=%d\r\n",
	    (int)(unsigned short)g_h[0],
	    (int)(unsigned short)g_h[3],
	    (int)(unsigned short)g_h[7]);
	printf("buf_w 1000=%d 4000=%d 8000=%d\r\n",
	    (int)(unsigned short)g_w[0],
	    (int)(unsigned short)g_w[3],
	    (int)(unsigned short)g_w[7]);

	return 0;
}
