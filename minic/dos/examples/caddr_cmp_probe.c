/*
 * caddr_cmp_probe.c — exercises Kl comparisons against a CAddr (symbol)
 * RCon under far-data models (compact/large/huge).
 *
 * Companion to caddr_arith_probe.c (Oadd/Osub Kl) and caddr_logop_probe.c
 * (Oand/Oor/Oxor Kl).  The 32-bit comparison handlers
 * (Oceql/Ocnel/Ocsltl/Ocslel/Ocsgtl/Ocsgel/Ocultl/Oculel/Ocugtl/Ocugel)
 * route through cmp32_high/cmp32_low in i8086/emit.c.  Both used to read
 * `fn->con[r.val].bits.i` directly for RCon, which for a CAddr is just
 * the addend; the segment word comes from the NASM `seg sym` relocation.
 *
 * So pre-fix, `if (kl_var == (unsigned long)&g)` lowered to
 *     cmp dx, 0          ; (addend >> 16) is 0 — segment dropped!
 *     cmp ax, addend
 * and the comparison was wrong whenever DGROUP's paragraph != 0 (i.e.
 * always under real MZ load).
 *
 * Fixed by routing the CAddr branch through `cmp ax, sym+addend` /
 * `cmp dx, seg sym` so omf_link supplies both fixups.
 *
 * Strategy: helper functions take a Kl arg and compare it against the
 * inlined CAddr `&g_long`.  The arg gets loaded via load32_dxax (slot
 * → DX:AX); the RCon CAddr goes through cmp32_high/low — the
 * under-test path.  Calling helpers with values derived from
 * `(unsigned long)&g_long` ± deltas pins the segment-word handling.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/caddr_cmp_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/caddr_cmp_probe/caddr_cmp_probe.exe) \
 *              minic/dos/tests/caddr_cmp_probe.golden.txt
 */

#include <stdio.h>

static unsigned long g_long;

/* Inlined `(unsigned long)&g_long` stays as `$g_long` (CAddr Kl) all the
 * way through to emit.c, where it becomes r1 of the Oc*l compare. */
int eq_sym (unsigned long x) { return x == (unsigned long)&g_long; }
int ne_sym (unsigned long x) { return x != (unsigned long)&g_long; }
int ltu_sym(unsigned long x) { return x <  (unsigned long)&g_long; }
int leu_sym(unsigned long x) { return x <= (unsigned long)&g_long; }
int gtu_sym(unsigned long x) { return x >  (unsigned long)&g_long; }
int geu_sym(unsigned long x) { return x >= (unsigned long)&g_long; }

int main(void)
{
	unsigned long k;
	unsigned long k_lo;
	unsigned long k_hi;

	g_long = 0;  /* anchor symbol; value unread */

	k    = (unsigned long)&g_long;
	k_lo = k - 1UL;
	k_hi = k + 1UL;

	/* Equality with the symbol.  k_lo / k_hi differ in just the low
	 * word, so a buggy `cmp dx, 0` makes eq_sym(k) return 0 (DX==seg,
	 * not 0) — the killer test for the CAddr-drop bug. */
	if ( eq_sym(k))    printf("eq_sym ok\r\n");
	else               printf("eq_sym FAIL\r\n");
	if (!eq_sym(k_hi)) printf("eq_sym_hi ok\r\n");
	else               printf("eq_sym_hi FAIL\r\n");
	if (!eq_sym(k_lo)) printf("eq_sym_lo ok\r\n");
	else               printf("eq_sym_lo FAIL\r\n");

	if (!ne_sym(k))    printf("ne_sym ok\r\n");
	else               printf("ne_sym FAIL\r\n");
	if ( ne_sym(k_hi)) printf("ne_sym_hi ok\r\n");
	else               printf("ne_sym_hi FAIL\r\n");

	/* Unsigned ordering — exercises Ocultl/Oculel/Ocugtl/Ocugel.  The
	 * segment word is part of the magnitude, so k_lo < k < k_hi must
	 * hold; under the bug, DX got compared against 0 every time, so
	 * for any non-zero DGROUP paragraph, ltu_sym(anything) would
	 * always return 0 (because cmp dx, 0 sets NB/A). */
	if ( ltu_sym(k_lo)) printf("ltu_sym ok\r\n");
	else                printf("ltu_sym FAIL\r\n");
	if (!ltu_sym(k))    printf("ltu_eq ok\r\n");
	else                printf("ltu_eq FAIL\r\n");
	if (!ltu_sym(k_hi)) printf("ltu_hi ok\r\n");
	else                printf("ltu_hi FAIL\r\n");

	if ( leu_sym(k_lo)) printf("leu_sym ok\r\n");
	else                printf("leu_sym FAIL\r\n");
	if ( leu_sym(k))    printf("leu_eq ok\r\n");
	else                printf("leu_eq FAIL\r\n");
	if (!leu_sym(k_hi)) printf("leu_hi ok\r\n");
	else                printf("leu_hi FAIL\r\n");

	if ( gtu_sym(k_hi)) printf("gtu_sym ok\r\n");
	else                printf("gtu_sym FAIL\r\n");
	if (!gtu_sym(k))    printf("gtu_eq ok\r\n");
	else                printf("gtu_eq FAIL\r\n");
	if (!gtu_sym(k_lo)) printf("gtu_lo ok\r\n");
	else                printf("gtu_lo FAIL\r\n");

	if ( geu_sym(k_hi)) printf("geu_sym ok\r\n");
	else                printf("geu_sym FAIL\r\n");
	if ( geu_sym(k))    printf("geu_eq ok\r\n");
	else                printf("geu_eq FAIL\r\n");
	if (!geu_sym(k_lo)) printf("geu_lo ok\r\n");
	else                printf("geu_lo FAIL\r\n");

	return 0;
}
