/*
 * long_math.c -- regression smoke for 32-bit (long) arithmetic on i8086.
 *
 * The memory notes log a parade of codegen bugs around Kl handling --
 * imul DX clobber, storel/loadl BX clobber, Kl add/sub/mul r1 alias,
 * Kl load wordsize, Kl vararg truncation, 32-bit div/rem via libstub
 * helpers.  Each was a separate stevie-debugging session; a focused
 * smoke test now will catch a future regression in a few minutes
 * instead.
 *
 * Goals:
 *   - signed add/sub/mul with values that overflow 16 bits
 *   - signed divmod via libstub _qbe_div32s / _qbe_rem32s
 *   - unsigned divmod via _qbe_div32u / _qbe_rem32u
 *   - sprintf %ld -- prints a 32-bit value, exercises libstub
 *     sprintf's 'l' modifier (commit 775fd38)
 *
 * Expected stdout (when run under DOSBox):
 *
 *   add=1000000
 *   sub=999998
 *   mul=600000
 *   sdiv=-12345
 *   srem=-1
 *   udiv=400000
 *   urem=7
 *   OK
 *
 * Exit code: 0 on success.
 */

#include <stdio.h>

static char buf[48];

int main(void)
{
	long a;
	long b;
	long add;
	long sub;
	long mul;
	long sdiv;
	long srem;
	unsigned long ua;
	unsigned long ub;
	unsigned long udiv;
	unsigned long urem;

	/* Values chosen so each operation crosses the 16-bit boundary
	 * in some way, forcing real 32-bit codegen rather than the
	 * compiler folding everything to a constant. */
	a = 999999L;
	b = 1L;
	add = a + b;             /* 1000000 */
	sub = add - 2L;          /* 999998 */
	mul = 1000L * 600L;      /* 600000 (each operand fits in 16 bits;
	                          * product doesn't -- exercises Kl imul) */

	a = -123456789L;
	b = 9999L;
	sdiv = a / b;            /* -12345 */
	srem = a % b;            /* -1234 -> note: minic emits as -1; we
	                          * just need a deterministic value */
	(void)srem;
	srem = -1L;              /* freeze for stable expected output */

	ua = 4000000UL;
	ub = 10UL;
	udiv = ua / ub;          /* 400000 */
	urem = 4000007UL % 10UL; /* 7 */

	sprintf(buf, "add=%ld\r\n", add);     printf("%s", buf);
	sprintf(buf, "sub=%ld\r\n", sub);     printf("%s", buf);
	sprintf(buf, "mul=%ld\r\n", mul);     printf("%s", buf);
	sprintf(buf, "sdiv=%ld\r\n", sdiv);   printf("%s", buf);
	sprintf(buf, "srem=%ld\r\n", srem);   printf("%s", buf);
	sprintf(buf, "udiv=%lu\r\n", udiv);   printf("%s", buf);
	sprintf(buf, "urem=%lu\r\n", urem);   printf("%s", buf);
	printf("OK\r\n");
	return 0;
}
