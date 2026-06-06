/*
 * caddr_arith_probe.c — exercises Oadd/Osub Kl on CAddr (symbol)
 * with a variable (runtime) operand, under far-data models
 * (compact / large / huge).
 *
 * Latent backend bug ([[i8086-cmp-caddr-emits-zero]] family): emit.c
 * sites at lines 986/1000/1047/1060/1105/1115/1150/1173/1196/... read
 * fn->con[r].bits.i without checking if the Con type is CAddr.  For a
 * far-pointer symbol arithmetic such as `&arr[i]` where `i` is a
 * runtime variable, minic emits `Oadd Kl, $arr, %extuw(2*i)`.  The
 * RCon-r0 branch then materialises the CAddr as
 *   mov ax, low(bits.i)   ;  zero -- because addend is 0
 *   mov dx, high(bits.i)  ;  zero -- bits.i is just the addend
 * dropping the segment selector.  The resulting pointer is
 * `0000:offset_within_DGROUP`, which loads from the IVT / BIOS data
 * area instead of the global.
 *
 * compactprobe_extra.c already gates `(char *)CONST + 7` -- that path
 * folds at the parser/qbe layer into a single CBits Kl constant, so
 * it never reaches the buggy Oadd-Kl-CAddr path.  This probe pins the
 * symbol-CAddr case.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/caddr_arith_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/caddr_arith_probe/caddr_arith_probe.exe) \
 *              minic/dos/tests/caddr_arith_probe.golden.txt
 */

#include <stdio.h>

static int  arr[8];
static char str[8];

/* Runtime indirection — defeats QBE constant-propagation so that
 * `&arr[i]` stays as `Oadd Kl, $arr, %scaled_i_kl_temp`.  Without this
 * (i.e. plain `int i = 3` inline), QBE folds %t84 = $arr+const → a
 * single CAddr Con, and the bug shifts from Oadd Kl to Oloadfw/Ostorefw
 * RCon handling. */
int peek_arr(int i) { return arr[i]; }
int peek_str(int i) { return str[i]; }

int main(void)
{
	int   i;
	int  *ip;
	char *cp;
	int   v;
	char  c;

	arr[0] = 100; arr[1] = 101; arr[2] = 102; arr[3] = 103;
	arr[4] = 104; arr[5] = 105; arr[6] = 106; arr[7] = 107;
	str[0] = 'A'; str[1] = 'B'; str[2] = 'C'; str[3] = 'D';
	str[4] = 'E'; str[5] = 'F'; str[6] = 'G'; str[7] = 0;

	/* &arr[i] -- minic emits add Kl $arr, extuw(2*i) */
	i  = 3;
	ip = &arr[i];
	v  = *ip;
	printf("arr_i=%d (want 103)\r\n", v);

	/* arr + i -- same shape, possibly opposite operand order */
	i  = 5;
	ip = arr + i;
	v  = *ip;
	printf("arr_plus=%d (want 105)\r\n", v);

	/* &str[i] -- byte-sized element, no shift in the index */
	i  = 4;
	cp = &str[i];
	c  = *cp;
	printf("str_i=%d (want 69)\r\n", c);   /* 'E' = 69 */

	/* Runtime index via function param — defeats const-prop so the SSA
	 * `Oadd Kl, $arr, %scaled_i` survives to emit.c. */
	printf("peek_arr=%d (want 106)\r\n", peek_arr(6));
	printf("peek_str=%d (want 67)\r\n",  peek_str(2));  /* 'C' = 67 */

	return 0;
}
