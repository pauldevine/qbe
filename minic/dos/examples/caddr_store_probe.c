/*
 * caddr_store_probe.c — exercises the implicit AX/DX clobber in the
 * narrow far-store handlers (Ostorefb / Ostorefh / Ostorefw) when the
 * destination address is an RCon CAddr (a far store to a constant
 * global symbol address, e.g. `arr[CONST] = v`).
 *
 * Pre-fix gap ([[minic-far-data-segment]] bug 1): under a far-data
 * model (compact/large/huge), `arr[CONST] = v` folds `&arr[CONST]` to
 * a single CAddr RCon, so the store lowers to `storefw v, $arr+off`.
 * The Ostoref{b,h,w} handlers route that RCon dest through
 * load_farptr_con, which stages the segment via `mov ax, seg sym` —
 * clobbering AX.  rega does NOT model that clobber, so a live SSA temp
 * rega placed in AX across the store (canonically the function's
 * return value, which is hinted to AX) is silently destroyed.
 *
 * Repro shape (the killer): compute the return value FIRST, then do a
 * constant-index far store, then return.  Pre-fix the function returns
 * `seg arr` (garbage) instead of the computed value.
 *
 * The fix wraps the narrow store handlers with kl_save_axdx /
 * kl_restore_axdx, the same AX/DX save bracket Oloadf* and Ostorefl
 * already use.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/caddr_store_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/caddr_store_probe/caddr_store_probe.exe) \
 *              minic/dos/tests/caddr_store_probe.golden.txt
 */

#include <stdio.h>

static unsigned       g_w[4];
static unsigned char  g_b[4];

/* Return value (AX) computed before a word store to a constant index.
 * Pre-fix: the `g_w[0] = 7` storefw's `mov ax, seg g_w` overwrites the
 * AX-resident return temp, so this returns garbage. */
unsigned store_w_then_ret(unsigned x)
{
	unsigned r = x + 100;
	g_w[0] = 7;
	return r;
}

/* Same, but a byte store (Ostorefb path, CL staging + AX scratch). */
unsigned store_b_then_ret(unsigned x)
{
	unsigned r = x + 200;
	g_b[0] = 9;
	return r;
}

/* Several constant-index stores between compute and return — stresses
 * the save bracket across multiple load_farptr_con AX clobbers. */
unsigned store_many_then_ret(unsigned x)
{
	unsigned r = x + 300;
	g_w[1] = 11;
	g_w[2] = 22;
	g_b[1] = 33;
	g_b[2] = 44;
	return r;
}

int main(void)
{
	unsigned a, b, c;

	g_w[0] = 0; g_w[1] = 0; g_w[2] = 0; g_w[3] = 0;
	g_b[0] = 0; g_b[1] = 0; g_b[2] = 0; g_b[3] = 0;

	a = store_w_then_ret(5);     /* expect 105 */
	b = store_b_then_ret(5);     /* expect 205 */
	c = store_many_then_ret(5);  /* expect 305 */

	if (a == 105) printf("ret_w ok\r\n");    else printf("ret_w FAIL %u\r\n", a);
	if (b == 205) printf("ret_b ok\r\n");    else printf("ret_b FAIL %u\r\n", b);
	if (c == 305) printf("ret_many ok\r\n"); else printf("ret_many FAIL %u\r\n", c);

	/* And confirm the stores actually landed (the store must still
	 * write through correctly, not just preserve AX). */
	if (g_w[0] == 7)  printf("w0 ok\r\n");  else printf("w0 FAIL %u\r\n", g_w[0]);
	if (g_b[0] == 9)  printf("b0 ok\r\n");  else printf("b0 FAIL %u\r\n", (unsigned)g_b[0]);
	if (g_w[1] == 11) printf("w1 ok\r\n");  else printf("w1 FAIL %u\r\n", g_w[1]);
	if (g_w[2] == 22) printf("w2 ok\r\n");  else printf("w2 FAIL %u\r\n", g_w[2]);
	if (g_b[1] == 33) printf("b1 ok\r\n");  else printf("b1 FAIL %u\r\n", (unsigned)g_b[1]);
	if (g_b[2] == 44) printf("b2 ok\r\n");  else printf("b2 FAIL %u\r\n", (unsigned)g_b[2]);

	return 0;
}
