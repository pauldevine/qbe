/*
 * loadfb_alias_probe.c — exercises the implicit AX clobber in the
 * narrow far-load handlers (Oloadfb / Oloadfh / Oloadfw).
 *
 * Pre-fix gap ([[i8086-compact-loadfb-aliases-ax]]): each of the three
 * narrow Oloadf* handlers writes the loaded value into AX/AL as part
 * of the load sequence and then conditionally moves to the rega-chosen
 * dst.  rega is never told AX is clobbered, so a live SSA temp that
 * rega happened to place in AX is silently corrupted by any subsequent
 * loadf* whose dst is something else.  Two back-to-back narrow loads
 * whose results both feed the same printf line therefore alias each
 * other — the second `mov al, byte ptr es:[bx]; xor ah, ah` overwrites
 * the first's value while it's still live in AX waiting to be pushed.
 *
 * Repro reported in NEXT_SESSION_PROMPT.md (track j):
 *
 *   char dst[16];
 *   dst[0] = 'a'; dst[1] = 'b';
 *   printf("%d %d\r\n", (int)(unsigned char)dst[0], (int)(unsigned char)dst[1]);
 *   // expect "97 98"; pre-fix actual "98 98"
 *
 * Under compact / large / huge, `dst[i]` lowers to a far byte load
 * because all data accesses are far in those models.  The fix wraps
 * each narrow handler body with kl_save_axdx / kl_restore_axdx, the
 * same save bracket Oloadfl already uses.
 *
 * Probe strategy: each test consumes BOTH narrow loads on the same
 * printf line, so any pair of back-to-back narrow loads whose live
 * ranges overlap will print one value twice when the bug is live.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/loadfb_alias_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/loadfb_alias_probe/loadfb_alias_probe.exe) \
 *              minic/dos/tests/loadfb_alias_probe.golden.txt
 */

#include <stdio.h>

static unsigned char  g_b[8];
static unsigned short g_h[4];
static unsigned short g_w[4];

int main(void)
{
	/* Initialize the globals here rather than via inline initializer
	 * — minic parser rejects struct/array initializers on file-scope
	 * globals (see [[per-model-gate]] for the quirk). */
	g_b[0] = 11; g_b[1] = 22; g_b[2] = 33; g_b[3] = 44;
	g_b[4] = 55; g_b[5] = 66; g_b[6] = 77; g_b[7] = 88;
	g_h[0] = 0x1111; g_h[1] = 0x2222; g_h[2] = 0x3333; g_h[3] = 0x4444;
	g_w[0] = 0xAAAA; g_w[1] = 0xBBBB; g_w[2] = 0xCCCC; g_w[3] = 0xDDDD;

	/* Test 1: two back-to-back Oloadfb's both feed printf.  Pre-fix:
	 * "22 22" (the second loadfb's value twice); post-fix: "11 22". */
	printf("byte_pair %d %d\r\n",
	    (int)(unsigned char)g_b[0],
	    (int)(unsigned char)g_b[1]);

	/* Test 2: four loadfb's on one line — multiplies the bug surface
	 * if rega keeps any of them in AX across siblings. */
	printf("byte_quad %d %d %d %d\r\n",
	    (int)(unsigned char)g_b[2],
	    (int)(unsigned char)g_b[3],
	    (int)(unsigned char)g_b[4],
	    (int)(unsigned char)g_b[5]);

	/* Test 3: Oloadfh (short load) pair — same shape as test 1 for
	 * the 16-bit variant.  Pre-fix expected "2222 2222". */
	printf("short_pair %x %x\r\n",
	    (unsigned int)g_h[0],
	    (unsigned int)g_h[1]);

	/* Test 4: Oloadfw (word load) pair — pre-fix expected "bbbb bbbb". */
	printf("word_pair %x %x\r\n",
	    (unsigned int)g_w[0],
	    (unsigned int)g_w[1]);

	/* Test 5: mixed widths — Oloadfb then Oloadfh on same line.
	 * Forces both handlers active in the same vararg evaluation. */
	printf("mixed %d %x\r\n",
	    (int)(unsigned char)g_b[6],
	    (unsigned int)g_h[2]);

	/* Test 6: same byte twice via different indices — guards against
	 * any rega coalescing that would let the bug hide. */
	printf("same_byte %d %d\r\n",
	    (int)(unsigned char)g_b[7],
	    (int)(unsigned char)g_b[0]);

	return 0;
}
