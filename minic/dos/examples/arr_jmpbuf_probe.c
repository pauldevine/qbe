/*
 * arr_jmpbuf_probe.c -- array-of-jmp_buf cross-frame longjmp (§7e, the §4v
 * "jmp_buf bufs[6]" track).
 *
 * `jmp_buf` is `int[8]`, so `jmp_buf bufs[N]` is an array whose ELEMENT is
 * itself an array typedef.  minic's flat type system can't encode
 * `int (*)[8]`, and the array-declarator rules used to ignore the typedef's
 * inner dimension entirely: `bufs[N]` was sized as `int[N]` (e.g. 12 bytes
 * for N=6, not 96) and `bufs[i]` was treated as a SCALAR-int subscript
 * (stride 2, then a value load) instead of the row ADDRESS bufs + i*16.
 * Result: every setjmp(bufs[i]) aliased bufs[0] and a cross-frame
 * longjmp(bufs[target]) resumed the wrong (deepest) frame.
 *
 * The fix (var_aoa_dim + mkidx): an array-of-array-typedef variable is
 * registered IDIR(elem) with the correct N*D*sizeof(elem) size, and a
 * one-level subscript bufs[i] desugars to the bare pointer add
 * bufs + i*D (no deref) -> the int* row address.  setjmp(bufs[i]) and
 * bufs[i][j] then both compose naturally.
 *
 * Model-independent (program output only); gated medium + compact + large,
 * matching setjmp_probe.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/arr_jmpbuf_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/arr_jmpbuf_probe/arr_jmpbuf_probe.exe \
 *             | diff - minic/dos/tests/arr_jmpbuf_probe.golden.txt
 */

#include <stdio.h>
#include <setjmp.h>

#define N 6
static jmp_buf bufs[N];          /* file-scope array of jmp_buf */

/* --- case A: cross-frame longjmp into a runtime-indexed buffer --- */
static void
recurse(int depth, int target)
{
	volatile int mylevel = depth;   /* live across setjmp */
	int r;

	r = setjmp(bufs[depth]);        /* variable index on the setjmp side */
	if (r == 0) {
		printf("set %d\n", depth);
		if (depth + 1 < N)
			recurse(depth + 1, target);
		else
			longjmp(bufs[target], target + 50); /* variable index */
		/* levels above target resume here as the stack unwinds */
		printf("unwound %d (mylevel=%d)\n", depth, mylevel);
	} else {
		printf("caught %d r=%d mylevel=%d\n", depth, r, mylevel);
	}
}

/* --- case B: a BLOCK-LOCAL jmp_buf array, runtime-indexed in-frame --- */
static int
blocal(int which)
{
	jmp_buf lb[3];                  /* block-scope array-of-jmp_buf */
	int r;

	r = setjmp(lb[which]);
	if (r == 0)
		longjmp(lb[which], which + 1);   /* same buffer, variable index */
	return r;                            /* arrives as which+1 */
}

/* --- case D: a function-local STATIC jmp_buf array, runtime-indexed --- */
static int
slocal(int which)
{
	static jmp_buf sb[3];           /* function-local static array-of-jmp_buf */
	int r;

	r = setjmp(sb[which]);
	if (r == 0)
		longjmp(sb[which], which + 10);
	return r;                       /* arrives as which+10 */
}

/* --- case C: double subscript dd[i][j] (stride must compose) --- */
typedef int ddrow_t[8];          /* element is an array typedef, like jmp_buf */
static ddrow_t dd[4];

int
main(void)
{
	int target = 2;     /* variable => bufs[target] is a runtime index */
	int i, j, sum;

	recurse(0, target);

	printf("blocal0=%d blocal2=%d\n", blocal(0), blocal(2));
	printf("slocal0=%d slocal2=%d\n", slocal(0), slocal(2));

	for (i = 0; i < 4; i++)
		for (j = 0; j < 8; j++)
			dd[i][j] = i * 10 + j;
	sum = 0;
	for (i = 0; i < 4; i++)
		sum += dd[i][0] + dd[i][7];
	printf("dd0_0=%d dd3_7=%d sum=%d\n", dd[0][0], dd[3][7], sum);

	printf("done\n");
	return 0;
}
