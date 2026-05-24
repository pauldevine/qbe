/*
 * huge_norm_probe.c -- Phase B gate for huge-model pointer normalisation.
 *
 * Phase A landed _qbe_huge_add / _qbe_huge_sub as standalone libstub
 * helpers (verified by huge_arith_probe.c via inline-asm).  Phase B
 * (this probe's reason for existing) routes minic-generated Mhuge
 * pointer arithmetic through those helpers, so a plain C `p + N`
 * under --model=huge now produces a NORMALISED (seg, off) where off
 * is in [0,16).
 *
 * Under huge, the same `(char *)0x12345000L + 7` that compactprobe_extra
 * expects to yield `12345007` instead yields `17340007`:
 *
 *     linear  = 0x1234 * 16 + 0x5000 + 7 = 0x12340 + 0x5007 = 0x17347
 *     new_seg = 0x17347 >> 4             = 0x1734
 *     new_off = 0x17347 &  0xF           = 0x7
 *     packed  = (seg << 16) | off        = 0x17340007
 *
 * This probe exercises a handful of crossings to lock in the behaviour:
 *  (1) raw constant pointer + small offset, no boundary cross
 *  (2) +offset crossing the next 16-byte paragraph (normalises the off)
 *  (3) +offset crossing the 64K segment (huge_add carries into seg)
 *  (4) -offset borrowing from segment
 *  (5) prefix ++ on a far-ptr local (exercises the prefix code path)
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/huge_norm_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/huge_norm_probe/huge_norm_probe.exe \
 *             | diff - minic/dos/tests/huge_norm_probe.golden.txt
 */

#include <stdio.h>

int
main(void)
{
	unsigned long base;
	char *p;
	char *q;

	/* (1) no-cross: 0x1234:0x5000 + 7
	 *     linear = 0x12340 + 0x5007 = 0x17347
	 *     seg = 0x1734, off = 0x0007 → packed 17340007 */
	base = 0x12345000UL;
	p = (char *)base;
	q = p + 7;
	printf("nocross=%p (want 17340007)\r\n", q);

	/* (2) paragraph crossing: 0x1000:0x0010 + 0x10
	 *     linear = 0x10000 + 0x0020 = 0x10020
	 *     seg = 0x1002, off = 0x0000 → packed 10020000 */
	base = 0x10000010UL;
	p = (char *)base;
	q = p + 0x10;
	printf("paragraph=%p (want 10020000)\r\n", q);

	/* (3) 64K cross: 0x1000:0xFFF0 + 0x20
	 *     linear = 0x10000 + 0x10010 = 0x20010
	 *     seg = 0x2001, off = 0x0000 → packed 20010000 */
	base = 0x1000FFF0UL;
	p = (char *)base;
	q = p + 0x20;
	printf("sixtyfourK=%p (want 20010000)\r\n", q);

	/* (4) negative offset borrow: 0x1000:0x0010 - 0x20
	 *     linear = 0x10010 - 0x20 = 0xFFF0
	 *     seg = 0x0FFF, off = 0x0000 → packed 0fff0000 */
	base = 0x10000010UL;
	p = (char *)base;
	q = p - 0x20;
	printf("borrow=%p (want 0fff0000)\r\n", q);

	/* (5) prefix ++ on a far-ptr local: tests the minic.y:2293
	 *     prefix-emit huge_ptr_binop call.  Start at 0x1000:0x000F,
	 *     pre-increment by 1 → linear 0x1001*16+0 = same → seg 0x1001,
	 *     off 0x0000 → packed 10010000.
	 *     Actually: 0x1000*16 + 0xF + 1 = 0x10010 → seg 0x1001, off 0x0
	 *     → packed 10010000. */
	base = 0x1000000FUL;
	p = (char *)base;
	++p;
	printf("preinc=%p (want 10010000)\r\n", p);

	return 0;
}
