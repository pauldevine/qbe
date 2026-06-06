/*
 * caddr_div_probe.c — exercises Kl Odiv/Orem with a CAddr (symbol) RCon
 * argument under far-data models (compact/large/huge).
 *
 * Companion to caddr_arith_probe.c, caddr_logop_probe.c, caddr_cmp_probe.c.
 * Odiv/Orem Kl in i8086/emit.c lower to a cdecl call to
 * `_qbe_div32u`/`_qbe_div32s`/`_qbe_rem32u`/`_qbe_rem32s` via
 * `emit_push_long`.  After QBE's copy/inst-sel pass folds
 * `%t1 =l copy $g; %t2 =l urem %t1, 7` into `urem $g, 7`, emit_push_long
 * sees an RCon CAddr for arg0.  Pre-fix it read `bits.i` directly and
 * pushed `0, 0` for both halves (because a bare CAddr has bits.i==0 and
 * the segment lives in the NASM `seg sym` relocation), so the helper
 * computed `0 / k` instead of `(unsigned long)&g / k`.
 *
 * Fixed by giving the RCon branch of emit_push_long a CAddr case that
 * pushes `seg sym` (high) then `sym+addend` (low) via CX so omf_link's
 * BASE-SEGMENT and OFFSET fixups land at link time.  Same shape as the
 * cmp32_high/low CAddr fix in session (cc).
 *
 * Strategy: cross-check `(unsigned long)&g_long / N` (which folds to a
 * Kl udiv with RCon CAddr at the emit layer) against the same division
 * routed through a function call that returns the address — QBE treats
 * the call's return as opaque, so the dividend lives in a slot at emit
 * time and goes through the already-portable RSlot push path.  Pre-fix
 * the two values disagree (CAddr path pushes `0,0` → result 0).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/caddr_div_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/caddr_div_probe/caddr_div_probe.exe) \
 *              minic/dos/tests/caddr_div_probe.golden.txt
 */

#include <stdio.h>

static unsigned long g_long;

/* Opaque-to-QBE wrapper: forces the address to flow through a call
 * boundary so the caller sees an RSlot at the division site instead of
 * an inlined RCon CAddr. */
unsigned long addr_of_g(void) { return (unsigned long)&g_long; }

int main(void)
{
	unsigned long ref;
	unsigned long via_addr;
	unsigned long via_call;

	g_long = 0;  /* anchor symbol; value unread */

	ref = addr_of_g();

	/* `(unsigned long)&g_long / 7` — QBE folds copy chain into
	 * `udiv $g_long, 7`, so emit_push_long sees RCon CAddr for arg0. */
	via_addr = ((unsigned long)&g_long) / 7UL;
	via_call = ref / 7UL;
	if (via_addr == via_call) printf("udiv_7 ok\r\n");
	else                      printf("udiv_7 FAIL\r\n");

	via_addr = ((unsigned long)&g_long) % 7UL;
	via_call = ref % 7UL;
	if (via_addr == via_call) printf("urem_7 ok\r\n");
	else                      printf("urem_7 FAIL\r\n");

	/* Non-power-of-2 odd divisor — defeats any shr-style fold. */
	via_addr = ((unsigned long)&g_long) / 13UL;
	via_call = ref / 13UL;
	if (via_addr == via_call) printf("udiv_13 ok\r\n");
	else                      printf("udiv_13 FAIL\r\n");

	via_addr = ((unsigned long)&g_long) % 13UL;
	via_call = ref % 13UL;
	if (via_addr == via_call) printf("urem_13 ok\r\n");
	else                      printf("urem_13 FAIL\r\n");

	/* Divisor that exceeds 16 bits so the high word of arg1 matters too
	 * — pins both arg0 (CAddr) and arg1 (large CBits) push paths. */
	via_addr = ((unsigned long)&g_long) / 100000UL;
	via_call = ref / 100000UL;
	if (via_addr == via_call) printf("udiv_100k ok\r\n");
	else                      printf("udiv_100k FAIL\r\n");

	via_addr = ((unsigned long)&g_long) % 100000UL;
	via_call = ref % 100000UL;
	if (via_addr == via_call) printf("urem_100k ok\r\n");
	else                      printf("urem_100k FAIL\r\n");

	return 0;
}
