/*
 * caddr_logop_probe.c — exercises Oand/Oor/Oxor Kl on CAddr (symbol)
 * with a runtime operand, under far-data models (compact/large/huge).
 *
 * Companion to caddr_arith_probe.c, which covered Oadd/Osub Kl.  The
 * Kl bitwise-op handlers (i8086/emit.c around `case Oand`/`Oor`/`Oxor`)
 * read `fn->con[r1.val].bits.i` for RCon r1 without checking for CAddr.
 * `bits.i` for a CAddr holds only the addend; the segment word comes
 * from the NASM `seg sym` relocation.  So `x | (unsigned long)&g`
 * would silently OR with `addend | 0`, dropping the segment bits.
 *
 * Fixed by routing all three Kl bitwise-RCon paths through
 * `emit32_logop_axdx_con`, which emits `<op> ax, sym+addend` and
 * `<op> dx, seg sym` so omf_link supplies both fixups.
 *
 * Pattern: the "reference" value is `x op (unsigned long)&g_long`
 * computed via a Kl local (goes through load32_dxax, which already
 * routes RCon → load32_axdx_con); the "under-test" value is the same
 * expression inlined in a function so it lowers to `Oor/Oand/Oxor Kl
 * %x, $g_long` with a CAddr r1.  Equality proves both code paths
 * produce the same 32-bit value, even though the segment of &g_long
 * is only known at link time.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/caddr_logop_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/caddr_logop_probe/caddr_logop_probe.exe) \
 *              minic/dos/tests/caddr_logop_probe.golden.txt
 */

#include <stdio.h>

/* Two globals so the segment selector is the same (DGROUP) but the
 * offset differs — both fixups must be carried. */
static unsigned long g_long;
static unsigned long g_long2;

/* Each helper hits a different Kl bitwise op with CAddr r1.  The
 * runtime x defeats QBE const-prop; the inline `&g_long` stays as
 * `$g_long` (CAddr Kl) through to emit.c. */
unsigned long or_with_sym (unsigned long x) { return x | (unsigned long)&g_long;  }
unsigned long xor_with_sym(unsigned long x) { return x ^ (unsigned long)&g_long;  }
unsigned long and_with_sym(unsigned long x) { return x & (unsigned long)&g_long;  }

/* Second symbol — confirms the relocation isn't aliasing to the
 * first one (e.g. if the helper accidentally captured `seg g_long`
 * literally instead of `seg <whatever sym was passed>`). */
unsigned long xor_with_sym2(unsigned long x) { return x ^ (unsigned long)&g_long2; }

int main(void)
{
	unsigned long k;
	unsigned long k2;
	unsigned long x;
	unsigned long got;
	unsigned long want;

	g_long  = 0;  /* not read — just here to anchor the symbol */
	g_long2 = 0;

	k  = (unsigned long)&g_long;
	k2 = (unsigned long)&g_long2;
	x  = 0x12345678UL;

	/* OR */
	got  = or_with_sym(x);
	want = x | k;
	if (got == want)
		printf("or_sym ok\r\n");
	else
		printf("or_sym FAIL got=%lx want=%lx\r\n", got, want);

	/* XOR */
	got  = xor_with_sym(x);
	want = x ^ k;
	if (got == want)
		printf("xor_sym ok\r\n");
	else
		printf("xor_sym FAIL got=%lx want=%lx\r\n", got, want);

	/* AND */
	got  = and_with_sym(x);
	want = x & k;
	if (got == want)
		printf("and_sym ok\r\n");
	else
		printf("and_sym FAIL got=%lx want=%lx\r\n", got, want);

	/* XOR with different symbol — distinct seg fixup */
	got  = xor_with_sym2(x);
	want = x ^ k2;
	if (got == want)
		printf("xor_sym2 ok\r\n");
	else
		printf("xor_sym2 FAIL got=%lx want=%lx\r\n", got, want);

	/* Round-trip identity: (x ^ k) ^ k == x.  Even if both helpers
	 * had the same buggy emit, this would still fail under the bug
	 * because the helper-XOR drops bits the local-XOR keeps. */
	got  = xor_with_sym(x) ^ k;
	if (got == x)
		printf("xor_roundtrip ok\r\n");
	else
		printf("xor_roundtrip FAIL got=%lx want=%lx\r\n", got, x);

	return 0;
}
