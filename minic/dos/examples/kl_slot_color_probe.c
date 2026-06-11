/*
 * kl_slot_color_probe.c — §4w regression guard for Kl/Ks stack-slot
 * coloring (spill.c::colorklslots, [[gen-frame-diet]]).
 *
 * THE CHANGE: on i8086 every Kl (32-bit long / far-pointer) temp is
 * slot-resident ([[i8086-kl-load-loses-high]]).  Before §4w each such
 * temp owned a private 2-word slot for the whole function, so frame
 * size grew with the NUMBER of Kl temps (MicroPython's
 * mp_execute_bytecode: 1261 temps = 5464-byte frame for a peak
 * simultaneous liveness of ~10 — and that frame is the per-level cost
 * of generator recursion).  §4w assigns the slots by interference-graph
 * coloring instead, so temps with disjoint live ranges SHARE a slot
 * (same function: 12 colors, 472-byte frame).
 *
 * What could break: a slot shared by two temps whose live ranges DO
 * overlap (interference miss) silently corrupts one of them.  The probe
 * stresses the coloring decisions and is bug-loud on value bleed:
 *   ok1  14 longs simultaneously live across a call (must NOT share);
 *   ok2  a chain of short disjoint Kl lifetimes (these DO share — the
 *        values must still be independent);
 *   ok3  a loop-carried long swap cycle (phi web; pins the phi-dest /
 *        phi-arg no-share rule — rega's pmgen would otherwise need a
 *        slot<->slot Oswap that i8086 emit does not implement);
 *   ok4  loop-carried longs live across calls in the loop body;
 *   ok5  pointer ping-pong walk (far Kl pointers under compact, near
 *        Kw under medium; one golden serves both).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/kl_slot_color_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/kl_slot_color_probe/kl_slot_color_probe.exe \
 *             | diff - minic/dos/tests/kl_slot_color_probe.golden.txt
 */

#include <stdio.h>

long lid(long x) { return x; }
int iid(int x) { return x; }

/* 14 longs all live across a trailing call: pairwise interference, no
 * sharing allowed.  A miss makes two of them alias and the sum drifts. */
long many_live(long seed)
{
	long a = lid(seed + 1);
	long b = lid(seed + 2);
	long c = lid(seed + 3);
	long d = lid(seed + 4);
	long e = lid(seed + 5);
	long f = lid(seed + 6);
	long g = lid(seed + 7);
	long h = lid(seed + 8);
	long i = lid(seed + 9);
	long j = lid(seed + 10);
	long k = lid(seed + 11);
	long l = lid(seed + 12);
	long m = lid(seed + 13);
	long n = lid(seed + 14);
	long z = lid(seed * 100);
	return a + b + c + d + e + f + g + h + i + j + k + l + m + n + z;
}

/* Short back-to-back Kl lifetimes: the colorer folds these onto a few
 * shared slots; each value must still come out independent. */
long seq_chain(long s)
{
	long t1 = lid(s) * 3;
	long u = t1 + 1;
	long t2 = lid(u) * 5;
	long v = t2 + 2;
	long t3 = lid(v) * 7;
	return t3 + 3;
}

/* Loop-carried swap: a/b/t form a phi web with a copy cycle on the
 * back edge.  Shared slots between the phi resources would corrupt the
 * rotation (or demand a slot<->slot swap emit can't do). */
long swap_loop(long a, long b, int n)
{
	long t;
	int i;
	for (i = 0; i < n; i++) {
		t = a;
		a = b + i;
		b = t;
	}
	return a * 1000 + b;
}

/* Loop-carried longs live across two calls per iteration: their slots
 * must survive the calls and never be reused for call-local temps. */
long carry_calls(int n)
{
	long acc = 0;
	long w = 1;
	int i;
	for (i = 0; i < n; i++) {
		acc += lid(w);
		w = w * 2 + iid(1);
	}
	return acc;
}

char bufA[8];
char bufB[8];

/* Pointer ping-pong: p/q/t rotate buffer addresses through a phi web.
 * Under compact these are far (Kl) pointers — colored slots; under
 * medium they are near (Kw) — same golden either way. */
long ptr_pingpong(int n)
{
	char *p;
	char *q;
	char *t;
	long sum = 0;
	int i;
	for (i = 0; i < 8; i++) {
		bufA[i] = (char)(i + 1);
		bufB[i] = (char)(10 * (i + 1));
	}
	p = bufA;
	q = bufB;
	for (i = 0; i < n; i++) {
		sum += *p;
		t = p;
		p = q + (i & 1);
		q = t;
	}
	return sum;
}

int main(void)
{
	printf("ok1 %ld\n", many_live(10));
	printf("ok2 %ld\n", seq_chain(2));
	printf("ok3 %ld\n", swap_loop(1, 2, 5));
	printf("ok4 %ld\n", carry_calls(6));
	printf("ok5 %ld\n", ptr_pingpong(6));
	return 0;
}
