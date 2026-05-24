/*
 * farretprobe.c -- stress test for the selret Kl return path.
 *
 * Background:
 *   On i8086 a Kl (32-bit) value can't fit in one 16-bit register, so
 *   spill.c forces every Kl SSA temp into a stack slot.  selret in
 *   abi.c emits `Ofarseg(DX, r0); Ofaroff(AX, r0)` to materialise a
 *   Kl return in DX:AX.  The emit RTmp branch assumes "high in DX,
 *   low in AX" -- safe ONLY if Kl temps stay slot-resident across
 *   any intervening computation (else DX/AX get clobbered between
 *   the producer and the consumer).
 *
 *   This probe makes Kl returns survive non-trivial intervening work
 *   (imul, far calls, store/load sequences) and verifies both halves
 *   of the returned far pointer are intact.  A regression would show
 *   up as a wrong segment or offset back in main.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/farretprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/farretprobe/farretprobe.exe
 *
 * Validation rule: one helper-returning value per printf line
 * ([[i8086-compact-loadfb-aliases-ax]]).  We use unsigned-equal
 * checks against expected segment/offset words so each printf
 * consumes a single Kw.
 */

#include <stdio.h>
#include <string.h>

int g_sink;
char g_buf[64];

/* (1) Load a far pointer from a global, do intervening 32-bit imul,
 * then return.  imul writes DX:AX -- a slot-resident Kl will survive;
 * a register-resident one would be silently shredded. */
static char *load_then_mul(int n)
{
	char *p;
	long x;

	p = g_buf;            /* far load from DGROUP into Kl temp */
	x = (long)n * 12345L; /* 32-bit imul exercises DX:AX */
	g_sink = (int)x;
	return p;
}

/* (2) Far-ptr arithmetic followed by a far call.  The intervening
 * call clobbers all caller-save GPRs (AX/CX/DX) per the i8086 ABI;
 * if the result Kl isn't slot-resident the high half is gone. */
static char *advance_then_call(char *base, int idx)
{
	char *p;
	int n;

	p = base + idx;
	n = (int)strlen(base);
	g_sink = n;
	return p;
}

/* (3) Two Kl values live simultaneously across intervening work.
 * Returns one; the other must not collide on the slot path. */
static char *two_live(char *a, char *b, int pick)
{
	char *p;
	char *q;
	long x;

	p = a;
	q = b;
	x = (long)pick * 7L;
	g_sink = (int)x;
	if (pick & 1)
		return p;
	return q;
}

/* (4) Kl value reloaded inside a loop, returned after the loop.
 * Forces the value back through its slot multiple times. */
static char *loop_then_return(char *base, int iters)
{
	char *p;
	int i;
	int sum;

	p = base;
	sum = 0;
	i = 0;
	while (i < iters) {
		sum = sum + (int)*p;
		i = i + 1;
	}
	g_sink = sum;
	return p;
}

/* (5) Chain of indirect returns: helper returns Kl, caller forwards. */
static char *forward(char *p)
{
	int n;

	n = (int)*p;
	g_sink = n * 3;
	return p;
}

int main(void)
{
	char *p;
	char *want;
	int seg_ok;
	int off_ok;

	strcpy(g_buf, "Hello, far world!");

	/* (1) load_then_mul */
	p = load_then_mul(7);
	want = g_buf;
	seg_ok = (int)((unsigned int)(((unsigned long)p) >> 16)
	             == (unsigned int)(((unsigned long)want) >> 16));
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("load_mul seg_ok=%d (want 1)\r\n", seg_ok);
	printf("load_mul off_ok=%d (want 1)\r\n", off_ok);

	/* (2) advance_then_call: p = g_buf+3 */
	p = advance_then_call(g_buf, 3);
	want = g_buf + 3;
	seg_ok = (int)((unsigned int)(((unsigned long)p) >> 16)
	             == (unsigned int)(((unsigned long)want) >> 16));
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("adv_call seg_ok=%d (want 1)\r\n", seg_ok);
	printf("adv_call off_ok=%d (want 1)\r\n", off_ok);
	printf("adv_call char=%d (want 108)\r\n", (int)*p);  /* 'l' */

	/* (3) two_live: pick=2 even -> returns b (g_buf+5) */
	p = two_live(g_buf, g_buf + 5, 2);
	want = g_buf + 5;
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("two_live off_ok=%d (want 1)\r\n", off_ok);
	printf("two_live char=%d (want 44)\r\n", (int)*p);   /* ',' */

	/* pick=1 odd -> returns a (g_buf) */
	p = two_live(g_buf, g_buf + 5, 1);
	want = g_buf;
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("two_live_a off_ok=%d (want 1)\r\n", off_ok);

	/* (4) loop_then_return: p stays = g_buf */
	p = loop_then_return(g_buf, 4);
	want = g_buf;
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("loop_ret off_ok=%d (want 1)\r\n", off_ok);
	printf("loop_ret char=%d (want 72)\r\n", (int)*p);   /* 'H' */

	/* (5) forward of (g_buf+7) */
	p = forward(g_buf + 7);
	want = g_buf + 7;
	off_ok = (int)((unsigned int)(unsigned long)p
	             == (unsigned int)(unsigned long)want);
	printf("forward off_ok=%d (want 1)\r\n", off_ok);
	printf("forward char=%d (want 102)\r\n", (int)*p);   /* 'f' */

	return 0;
}
