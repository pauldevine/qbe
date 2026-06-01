/*
 * uchar_widen_probe.c — widening an UNSIGNED char (uint8_t) with bit 7 set to a
 * wider integer must ZERO-extend, not sign-extend.
 *
 * The bug ([[minic-uchar-widen-extsb]]): minic emitted `extsb` for every
 * char->int widening regardless of signedness.  A byte already sits zero-extended
 * in a `w` temp (loadub / loadfb both clear the high byte), so sign-extending its
 * low byte corrupts any unsigned value with bit 7 set: 0x8D (141) became 0xFF8D
 * (-115).  Model-independent (near loadub + extsb is wrong too); the canonical
 * victim was MicroPython's get_rule_arg() reading a uint8_t offset table — a 0x8D
 * offset became 0xFF8D, indexing wild and looping the parser forever.  Fix: emit
 * `extub` (zero-extend) when the source char is unsigned.
 *
 * Uses sprintf+INT 21h directly (this builds as a .COM via the tiny path is not
 * needed — it runs as a normal .EXE under any model).
 */

#include <stdio.h>

static unsigned char tab[4] = { 0x8d, 0x90, 0x01, 0xff };

int main(void)
{
	unsigned char uc = 0x8d;
	signed char   sc = (signed char)0x8d;   /* = -115 */
	int wu, ws, idx;

	wu = uc;                 /* unsigned char -> int : zero-extend => 141 */
	ws = sc;                 /* signed char   -> int : sign-extend => -115 */

	if (wu == 141)   printf("uc ok\r\n");    else printf("uc FAIL %d\r\n", wu);
	if (ws == -115)  printf("sc ok\r\n");    else printf("sc FAIL %d\r\n", ws);

	/* Use an unsigned-char value (bit 7 set) as an array index after widening. */
	idx = tab[0];            /* 141 */
	if (idx == 0x8d) printf("idx ok\r\n");   else printf("idx FAIL %d\r\n", idx);

	/* Sum the table as ints: 0x8d+0x90+0x01+0xff = 0x21d = 541. */
	{
		int i, sum = 0;
		for (i = 0; i < 4; i++)
			sum += tab[i];
		if (sum == 541) printf("sum ok\r\n");  else printf("sum FAIL %d\r\n", sum);
	}

	/* Comparison promotion: an unsigned char > 127 must compare as its
	 * positive value, not a negative sign-extended one. */
	if (uc > 100)    printf("cmp ok\r\n");   else printf("cmp FAIL\r\n");

	return 0;
}
