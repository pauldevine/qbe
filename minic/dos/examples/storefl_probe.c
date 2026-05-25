/*
 * storefl_probe.c — exercises Ostorefl / Oloadfl: 32-bit long through
 * a far pointer under far-data models (compact / large / huge).
 *
 * Pre-fix gap ([[storefar-lacks-storefl]]): minic.y::storefar() /
 * loadfar() handled b/h/w only.  When `irtyp(LNG) == 'l'` reached the
 * `else` branch, the emitter routed long writes through `storefw` and
 * long reads through `loadfw`, silently truncating the high 16 bits.
 * Realistic shape `long *p = far_ptr_to_long; *p = v` lost dword/byte
 * 2..3.  Latent because no in-tree consumer wrote a `long` through a
 * far pointer (hugeprobe.c uses `char[]`; huge_stack_arith_probe.c
 * uses `int[]`).
 *
 * Probe strategy: hide a `long *` value behind an opaque function call
 * so QBE can't fold `&g_long` into a CAddr that routes through Ostorel
 * with CAddr-r1 (the already-portable global-write path).  With the
 * opaque indirection, the lvalue carries the FAR flag and goes through
 * minic.y::storefar() → emits storefl → i8086 Ostorefl handler.
 *
 * Cross-checks: write via far ptr, read via direct global (portable
 * Oload Kl CAddr); write via direct global (portable Ostorel CAddr),
 * read via far ptr; round-trip both far → far.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/storefl_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/storefl_probe/storefl_probe.exe) \
 *              minic/dos/tests/storefl_probe.golden.txt
 */

#include <stdio.h>

static long g_long;
static long g_long2;

/* Opaque indirection — QBE can't fold across a call return, so the
 * returned far ptr lives as a Kl tmp (spilled to a slot by spill.c).
 * The subsequent `*p = v` then goes through storefl, NOT through
 * Ostorel with CAddr-r1 (which would dodge the bug). */
long *opaque_pl(long *p) { return p; }

int main(void)
{
	long *p;
	long  v;

	/* Test 1: store via far ptr, read back via direct global.
	 * Pre-fix: storefw truncates 0xDEADBEEF → 0xBEEF only; g_long high
	 * half stays 0 from BSS → read shows 0x0000BEEF. */
	g_long = 0;
	p = opaque_pl(&g_long);
	*p = 0xDEADBEEFL;
	if (g_long == 0xDEADBEEFL)
		printf("store_far ok\r\n");
	else
		printf("store_far FAIL: %lx (want deadbeef)\r\n", g_long);

	/* Test 2: store via direct global (portable Ostorel CAddr path),
	 * read back via far ptr.  Pre-fix: loadfw → only low 16 bits → v
	 * gets 0x00005678 not 0x12345678. */
	g_long2 = 0x12345678L;
	p = opaque_pl(&g_long2);
	v = *p;
	if (v == 0x12345678L)
		printf("load_far ok\r\n");
	else
		printf("load_far FAIL: %lx (want 12345678)\r\n", v);

	/* Test 3: high-bit-set value through far store.  Pre-fix: truncates
	 * 0xFFFF0001 → 0x0001; checking against the post-fix value catches
	 * any further regressions where only the high half is mis-stored. */
	g_long = 0;
	p = opaque_pl(&g_long);
	*p = 0xFFFF0001L;
	if (g_long == 0xFFFF0001L)
		printf("store_far_high ok\r\n");
	else
		printf("store_far_high FAIL: %lx (want ffff0001)\r\n", g_long);

	/* Test 4: round-trip — far store followed by far load.  Both halves
	 * must round-trip; pre-fix fails on the load side too. */
	g_long = 0;
	p = opaque_pl(&g_long);
	*p = 0xCAFEBABEL;
	v = *p;
	if (v == 0xCAFEBABEL)
		printf("round_trip ok\r\n");
	else
		printf("round_trip FAIL: %lx (want cafebabe)\r\n", v);

	/* Test 5: high half preserved when overwriting with a fully-new
	 * value — guards against any half-store partial bug where one
	 * half writes and the other doesn't. */
	g_long = 0xAABBCCDDL;
	p = opaque_pl(&g_long);
	*p = 0x11223344L;
	if (g_long == 0x11223344L)
		printf("overwrite ok\r\n");
	else
		printf("overwrite FAIL: %lx (want 11223344)\r\n", g_long);

	return 0;
}
