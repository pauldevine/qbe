/*
 * aoa_extended_probe.c -- the bounded array-of-array-typedef forms left open
 * by §7e: MULTI-DECLARATOR arrays-of-jmp_buf and BRACE-INITIALISED 2-D
 * scalar tables (a typedef'd array as the element type).
 *
 * §7e closed the single-declarator uninitialised aoa form (`jmp_buf bufs[N];`
 * file-scope / block-local / static-local) via var_aoa_dim + mkidx.  Two
 * declarator shapes still ignored the typedef's inner dimension entirely:
 *
 *   (1) MULTI-DECLARATOR aoa:  `jmp_buf a[2], b[2];` (block-local)
 *       Each declarator went through the multi-decl 'B' (sized-array) path,
 *       which sized the slot as count*sizeof(elem) (e.g. 4 bytes) instead of
 *       count*D*sizeof(elem) (32) and never set the aoa flag — so `b[i]` was
 *       a SCALAR int load (value-as-pointer) rather than the row address
 *       b + i*D.  setjmp(b[i]) then ran through a garbage pointer.
 *
 *   (2) BRACE-INIT 2-D table:  `row3_t t[2] = {{1,2,3},{4,5,6}};`
 *       (minic has no true `int x[2][3]` — that is a hard parse error — so a
 *       typedef element is the ONLY way to write a 2-D constant table.)  The
 *       local array brace-init rules sized the element as sizeof(elem) and
 *       stored each top-level item with a single scalar expr(), so a nested
 *       `{…}` row was mishandled and the table was the wrong size.
 *
 * The fix sizes every multi-decl/brace-init aoa declarator as N*D*sizeof(elem),
 * sets var_aoa_dim(=D), and flattens each `{…}` row into per-element stores at
 * the linear offset row*D + col.  Subscripting then composes with §7e's mkidx.
 *
 * (File-scope / static multi-declarator array-first forms — `static jmp_buf
 * fa[2], fb[2];` — are a SEPARATE, pre-existing parse-error gap, not an aoa
 * sizing bug, and stay out of scope.)
 *
 * Model-independent (program output only); gated medium + compact + large,
 * matching arr_jmpbuf_probe.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/aoa_extended_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/aoa_extended_probe/aoa_extended_probe.exe \
 *             | diff - minic/dos/tests/aoa_extended_probe.golden.txt
 */

#include <stdio.h>
#include <setjmp.h>

typedef int row3_t[3];           /* element is an array typedef, like jmp_buf */

/* (1) MULTI-DECLARATOR block-local arrays-of-jmp_buf, runtime-indexed.
 * `a` and `b` are SEPARATE declarators that share the jmp_buf base type;
 * each must be a full 2*D*sizeof(elem) array with aoa stride, not aliased. */
static int
mdtest(int idx, int useb)
{
	jmp_buf a[2], b[2];             /* block-scope multi-decl aoa */
	int r;

	if (useb) {
		r = setjmp(b[idx]);     /* second declarator, runtime index */
		if (r == 0)
			longjmp(b[idx], idx + 20);
	} else {
		r = setjmp(a[idx]);     /* first declarator, runtime index */
		if (r == 0)
			longjmp(a[idx], idx + 10);
	}
	return r;                       /* a -> idx+10, b -> idx+20 */
}

int
main(void)
{
	/* (2a) function-top (dcls context) sized 2-D table. */
	row3_t t1[2] = {{1, 2, 3}, {4, 5, 6}};
	int i, j, sum;

	printf("md=%d,%d,%d,%d\n",
	       mdtest(0, 0), mdtest(1, 0), mdtest(0, 1), mdtest(1, 1));

	/* (2b) mid-block (stmt context) sized 2-D table. */
	row3_t t2[2] = {{10, 20, 30}, {40, 50, 60}};
	/* (2c) mid-block unsized 2-D table (row count inferred). */
	row3_t t3[] = {{7, 8, 9}, {11, 12, 13}};

	printf("t1=%d,%d,%d,%d,%d,%d\n",
	       t1[0][0], t1[0][1], t1[0][2], t1[1][0], t1[1][1], t1[1][2]);
	printf("t2=%d,%d,%d,%d,%d,%d\n",
	       t2[0][0], t2[0][1], t2[0][2], t2[1][0], t2[1][1], t2[1][2]);
	printf("t3=%d,%d,%d,%d,%d,%d\n",
	       t3[0][0], t3[0][1], t3[0][2], t3[1][0], t3[1][1], t3[1][2]);

	/* write-back through the indexed rows to confirm the stride is real */
	sum = 0;
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			t1[i][j] = t1[i][j] * 2;
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			sum += t1[i][j];
	printf("t1x2sum=%d\n", sum);   /* 2*(1+2+3+4+5+6) = 42 */

	printf("done\n");
	return 0;
}
