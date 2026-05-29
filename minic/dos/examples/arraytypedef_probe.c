/*
 * arraytypedef_probe.c -- Phase 1 array-typedef gate.
 *
 * Pins the array-typedef grammar + storage support added 2026-05-29
 * (see NEXT_SESSION.md Layer 1).  Before this, minic rejected
 * `typedef int jmp_buf[8];` outright ("parse error"), the single
 * biggest MicroPython spike convergence point — jmp_buf arrives via
 * setjmp.h -> nlr.h -> obj.h, so it hits essentially every file.
 *
 * An array typedef must stay a REAL array type (C requires it so the
 * name decays to a pointer when passed to a function), not a papered-
 * over pointer.  This probe exercises CODEGEN, not just parse:
 *
 *   1. The typedef name decays to a pointer-to-element when passed to
 *      a function, which writes through it (caller sees the writes) AND
 *      the whole array is really allocated (all 8 slots are distinct).
 *   2. An array-typedef STRUCT member reserves the whole array, so the
 *      member after it sits at the correct offset (no overlap) and
 *      sizeof(struct) accounts for DIM*sizeof(elem).
 *
 * NOTE: sizeof() of a *local* array variable (typedef'd or plain
 * `int buf[8]`) returns the pointer size, not DIM*sizeof(elem), under
 * minic today — local array dims aren't tracked for sizeof.  That is a
 * pre-existing, orthogonal limitation (it affects every local array),
 * so this probe pins the array-typedef behaviour through the struct
 * member, where the dimension IS recorded, rather than through a local
 * sizeof.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/arraytypedef_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/arraytypedef_probe/arraytypedef_probe.exe \
 *             | diff - minic/dos/tests/arraytypedef_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

/* The canonical motivating case: an array typedef, exactly like the
 * in-tree <setjmp.h> ships for the MicroPython port. */
typedef int jmp_buf[8];

/* A struct holding an array-typedef member sandwiched between scalars,
 * the exact shape of MicroPython's nlr_buf_t (prev / jmpbuf / ret). */
struct nlr {
	int prev;        /* offset 0 */
	jmp_buf jmpbuf;  /* offset 2, occupies 8*2 = 16 bytes */
	int ret;         /* offset 18 */
};

/* Fills env[0..n-1] = i*i through the decayed pointer.  Proves the
 * array-typedef parameter behaves as `int *`. */
static void fill(jmp_buf env, int n)
{
	int i;
	for (i = 0; i < n; i++)
		env[i] = i * i;
}

int main()
{
	jmp_buf env;
	struct nlr nb;

	/* Feature 1: decay-to-pointer on a call; caller sees the writes,
	 * and all 8 slots are distinct storage (env[7] != env[0]). */
	fill(env, 8);
	printf("env=%d,%d,%d (want 0,9,49)\r\n", env[0], env[3], env[7]);

	/* Feature 2: struct member sized as the full array -> `ret` does
	 * not overlap `jmpbuf`.  Stamp jmpbuf end + ret and read back. */
	nb.prev = 11;
	nb.jmpbuf[0] = 22;
	nb.jmpbuf[7] = 33;
	nb.ret = 44;
	printf("nlr=%d,%d,%d,%d (want 11,22,33,44)\r\n",
	    nb.prev, nb.jmpbuf[0], nb.jmpbuf[7], nb.ret);
	printf("nlrsz=%d (want 20)\r\n", (int)sizeof(struct nlr));

	return 0;
}
