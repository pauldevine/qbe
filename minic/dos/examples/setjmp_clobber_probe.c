/*
 * setjmp_clobber_probe.c — a local modified AFTER setjmp must survive a
 * longjmp (the C "volatile across setjmp" guarantee).
 *
 * Pins the alias.c + mem.c fix: QBE treated a non-escaping local alloca as
 * invisible to the setjmp call, so promote (mem2reg) hoisted it into a
 * callee-saved register and/or GCM sank its store past the setjmp.  On
 * longjmp, callee-saved regs revert to their setjmp-time values, so the
 * post-setjmp modification was LOST.  calls_setjmp() now forces every stack
 * slot AEsc (and gates promote) in a setjmp-calling function, keeping locals
 * memory-backed across the longjmp.
 *
 * Real-world victim: MicroPython's VM `exc_sp` (volatile, advanced by
 * SETUP_EXCEPT) reverted across a VM-internal-raise longjmp, so a
 * function-frame `except` never saw its handler.
 *
 * Model-independent (medium near-setjmp + compact far-setjmp both hit it).
 */
#include <setjmp.h>
#include <stdio.h>

static jmp_buf jb;

static void raiser(void) { longjmp(jb, 1); }
static void helper(void)  { raiser(); }

int main(void)
{
	int pushed = 0;          /* set 0 before setjmp, 1 after; like exc_sp */
	int tag = 0xAB;          /* another post-setjmp value */
	int got = setjmp(jb);
	if (got == 0) {
		pushed = 1;          /* establish state AFTER setjmp */
		tag = 0xCD;
		helper();            /* deep nested call longjmps back */
		printf("unreachable\r\n");
		return 1;
	}
	/* after longjmp: the post-setjmp values must persist */
	if (pushed == 1) printf("pushed ok\r\n");
	else             printf("pushed FAIL %d\r\n", pushed);
	if (tag == 0xcd) printf("tag ok\r\n");
	else             printf("tag FAIL %x\r\n", tag);
	printf("got=%d\r\n", got);
	return 0;
}
