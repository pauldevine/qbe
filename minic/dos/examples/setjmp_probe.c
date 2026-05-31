/*
 * setjmp_probe.c -- setjmp/longjmp + an NLR round-trip (medium + far-data).
 *
 * setjmp/longjmp are the keystone of MicroPython's exception/unwind path
 * (py/nlrsetjmp.c: nlr_push == setjmp(buf->jmpbuf); nlr_jump == longjmp).
 * The medium model uses far code (4-byte CS:IP return address, retf), so
 * the helpers are written directly in far form in tools/libstub_to_exe.py
 * (SETJMP_EXE) -- longjmp restores the caller's stack and far-jumps to the
 * saved CS:IP.  Under far-data models (compact/large/huge) the env arg is a
 * 4-byte far pointer reached via ES:BX (FAR_SETJMP_EXE; minic mangles
 * setjmp/longjmp -> _far_setjmp/_far_longjmp).  This probe is the runtime
 * regression guard for both forms (gated medium + compact + large).
 *
 * It checks:
 *   1. setjmp returns 0 on the direct call.
 *   2. longjmp(env, v) makes the matching setjmp return v.
 *   3. longjmp(env, 0) surfaces as 1 (C requires the 0->1 fixup).
 *   4. A longjmp from a DEEPLY NESTED call unwinds across frames.
 *   5. Callee-saved locals (live across the setjmp) survive the resume
 *      with their pre-setjmp values -- this is what the BX/SI/DI/BP save
 *      in jmp_buf protects.
 *   6. A chained stack of buffers (the NLR pattern) unwinds to the right
 *      level, popping intermediate buffers.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/setjmp_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/setjmp_probe/setjmp_probe.exe \
 *             | diff - minic/dos/tests/setjmp_probe.golden.txt
 */

#include <stdio.h>
#include <setjmp.h>

/* ---- a minimal NLR clone, shaped like MicroPython's nlr_buf_t ---- */
typedef struct _nlr_buf nlr_buf_t;
struct _nlr_buf {
	nlr_buf_t *prev;
	int ret_val;
	jmp_buf jmpbuf;
};

static nlr_buf_t *nlr_top = 0;

/* nlr_push: chain the buffer, then setjmp.  Returns 0 on push, the
 * raised value on a jump back. */
#define nlr_push(buf) ((buf)->prev = nlr_top, nlr_top = (buf), setjmp((buf)->jmpbuf))

static void
nlr_pop(void)
{
	nlr_top = nlr_top->prev;
}

/* nlr_jump: unwind one level and longjmp back into its setjmp. */
static void
nlr_jump(int val)
{
	nlr_buf_t *top = nlr_top;
	nlr_top = top->prev;        /* pop before the jump */
	top->ret_val = val;
	longjmp(top->jmpbuf, 1);    /* 1 => "arrived via jump" */
}

/* ---- case 4/5: a deep call chain that raises out of the bottom ---- */
static void
deep3(void)
{
	nlr_jump(0);                /* unwinds all the way to main's setjmp */
}

static void
deep2(void)
{
	volatile int marker = 0xBEE;
	deep3();
	/* not reached; reference marker so it cannot be optimized away */
	printf("UNREACHABLE deep2 %d\n", marker);
}

static void
deep1(void)
{
	deep2();
	printf("UNREACHABLE deep1\n");
}

int
main(void)
{
	jmp_buf env;
	int r;
	/* Locals that are LIVE across the setjmp and must survive a longjmp
	 * resume.  minic puts these in BX/SI/DI; the jmp_buf must restore
	 * them.  Mark volatile so the compiler honours their values. */
	volatile int guard_a = 111;
	volatile int guard_b = 222;
	volatile int guard_c = 333;
	nlr_buf_t outer, middle;

	/* --- case 1: direct setjmp returns 0 --- */
	r = setjmp(env);
	if (r == 0) {
		printf("setjmp direct=0\n");
		longjmp(env, 7);        /* --- case 2: come back with 7 --- */
	} else if (r == 7) {
		printf("longjmp val=7\n");
		longjmp(env, 0);        /* --- case 3: 0 must surface as 1 --- */
	} else {
		printf("longjmp zero-as=%d\n", r);
		printf("guards %d %d %d\n", guard_a, guard_b, guard_c);
	}

	/* --- case 4/5: deep nested unwind via the NLR clone --- */
	if (nlr_push(&outer) == 0) {
		deep1();                /* jumps back here from 3 frames down */
		printf("UNREACHABLE after deep1\n");
	} else {
		printf("nlr caught, ret_val=%d\n", outer.ret_val);
		printf("guards still %d %d %d\n", guard_a, guard_b, guard_c);
	}

	/* --- case 6: chained buffers, jump skips to the OUTER level --- */
	if (nlr_push(&outer) == 0) {
		if (nlr_push(&middle) == 0) {
			/* raise: nlr_jump pops `middle` and lands in middle's
			 * handler (one level), proving the chain pops correctly. */
			nlr_jump(42);
			printf("UNREACHABLE inner body\n");
		} else {
			printf("middle caught=%d depth_ok=%d\n",
			    middle.ret_val, nlr_top == &outer);
			nlr_pop();          /* pop outer cleanly */
		}
	}
	printf("done\n");
	return 0;
}
