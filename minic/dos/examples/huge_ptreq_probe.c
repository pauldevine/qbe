/*
 * huge_ptreq_probe.c -- gate for huge-model pointer EQUALITY comparison.
 *
 * §7u fixed the huge-model pointer RELATIONAL compare (p < q, p <= q):
 * the flat 32-bit `cultl`/`csltl` of two seg:off words is only correct when
 * BOTH operands are NORMALISED huge pointers, because then the (seg<<16)|off
 * word is monotonic in linear address.  An UNNORMALISED operand (a bare
 * symbol address whose offset can exceed 0xF) breaks that ordering, so §7u
 * routes relational compares through _qbe_huge_cmp = signed linear(p)-linear(q).
 *
 * The EQUALITY compare (p == q, p != q) carries the IDENTICAL latent gap and
 * was left unfixed in §7u because the lone live consumer (_sbrk) only does
 * `== NULL` (0:0, linear 0 — fine flat).  This probe is the synthetic-but-
 * bug-loud consumer that closes it: two pointers that denote the SAME linear
 * byte through DIFFERENT seg:off normalisations must compare EQUAL.
 *
 * Construction (mirrors huge_norm_probe): `(char *)constant` preserves the
 * raw 32-bit seg:off value UNNORMALISED, while `p + N` routes through
 * _qbe_huge_add and NORMALISES.  So:
 *
 *     unnorm = (char *)0x10000010UL   -> 0x1000:0x0010, linear 0x10010
 *     norm   = (char *)0x10010000UL   -> 0x1001:0x0000, linear 0x10010  (same!)
 *     normed = unnorm + 0 (runtime)   -> _qbe_huge_add -> 0x1001:0x0000  (same!)
 *
 * C11 6.5.9: unnorm, norm and normed all point at the same object byte, so
 * they MUST compare equal.  Under the unfixed compiler the flat `ceql` of
 * 0x10000010 vs 0x10010000 yields FALSE -> "eqconst FAIL" / "eqarith FAIL".
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/huge_ptreq_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/huge_ptreq_probe/huge_ptreq_probe.exe \
 *             | diff - minic/dos/tests/huge_ptreq_probe.golden.txt
 */

#include <stdio.h>

int
main(void)
{
	volatile int zero = 0;
	char *unnorm;
	char *norm;
	char *other;
	char *normed;
	char *nul;

	/* (1) same linear byte, different normalisations, via constant casts.
	 *     0x1000:0x0010 (unnormalised) vs 0x1001:0x0000 (normalised),
	 *     both linear 0x10010 -> MUST be ==.  Flat ceql says != (bug-loud). */
	unnorm = (char *)0x10000010UL;
	norm   = (char *)0x10010000UL;
	printf("eqconst=%s\r\n", (unnorm == norm) ? "eq" : "FAIL");
	printf("neconst=%s\r\n", (unnorm != norm) ? "FAIL" : "ne-false");

	/* (2) the realistic §7u shape: a (possibly unnormalised) base vs the
	 *     normalised pointer _qbe_huge_add returns for the SAME byte.
	 *     unnorm + 0 normalises 0x1000:0x0010 -> 0x1001:0x0000. */
	normed = unnorm + zero;
	printf("eqarith=%s\r\n", (unnorm == normed) ? "eq" : "FAIL");
	printf("nearith=%s\r\n", (unnorm != normed) ? "FAIL" : "ne-false");

	/* (3) genuinely different linear addresses MUST stay unequal (== is a
	 *     pure false-NEGATIVE risk; this guards against over-correction).
	 *     0x1000:0x0020 = linear 0x10020 != 0x10010. */
	other = (char *)0x10000020UL;
	printf("eqdiff=%s\r\n", (unnorm == other) ? "FAIL" : "ne");
	printf("nediff=%s\r\n", (unnorm != other) ? "ne" : "FAIL");

	/* (4) NULL stays correct (0:0 -> linear 0): nul == NULL true,
	 *     unnorm != NULL true. */
	nul = (char *)0L;
	printf("eqnull=%s\r\n", (nul == (char *)0L) ? "eq" : "FAIL");
	printf("nenull=%s\r\n", (unnorm != (char *)0L) ? "ne" : "FAIL");

	return 0;
}
