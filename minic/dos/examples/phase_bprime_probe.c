/*
 * phase_bprime_probe.c — Ostorel with RSlot pointer arg.
 *
 * Phase B' track of huge-mode bring-up: i8086/emit.c's Ostorel handler
 * treated an RSlot pointer arg (arg[1]) as the destination's storage
 * itself — `mov word [bp+slot(r1)], ax` — overwriting the spilled
 * pointer value instead of dereferencing it.  The semantics is wrong
 * for any non-alloca Kl pointer temp that spill.c's force_kl_slot
 * pass evicts to a slot.  Pre-fix the gap was masked because:
 *
 *   1. QBE constant-folds `&local + small + indirect_call` away,
 *      so realistic codegen rarely carried an opaque Kl pointer
 *      value through to Ostorel under the existing memory models.
 *   2. Phase B sidestepped it by skipping `_qbe_huge_add` when
 *      either operand is a Var (local), so stack pointer arith
 *      stayed on the flat-add path.  See `[[huge-phase-b]]`.
 *
 * Probe strategy: hide a `long **` value behind an opaque function
 * call so QBE can't fold the indirection.  Then write `*pp = q`
 * (minic emits bare `storel %q, %pp` because the lvalue's KIND is
 * PTR — the storefar branch is skipped).  Both temps are Kl;
 * force_kl_slot spills them to slots; emit's Ostorel sees
 * r0=RSlot, r1=RSlot.  Pre-fix: writes value to slot(r1) directly,
 * leaving the actual destination (where pp points) unchanged.
 * Post-fix: loads pp's slot → ES:BX (4-byte far pointer), writes
 * value through [ES:BX] → the actual destination.
 *
 * Verify via a direct global load on the destination (loadl
 * $g_handle — already portable via Oload Kl RCon CAddr).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/phase_bprime_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/phase_bprime_probe/phase_bprime_probe.exe) \
 *              minic/dos/tests/phase_bprime_probe.golden.txt
 */

#include <stdio.h>

static long g_value;
static long *g_handle;
static long *g_handle2;

/* Opaque call boundaries: QBE can't fold through them, so the returned
 * pointer values live in spill slots at the emit-time storel site. */
long **opaque_pp(long **p) { return p; }
long *opaque_p(long *p) { return p; }

int main(void)
{
	long **pp;
	long *q;

	/* Case 1: write a pointer through a pointer-to-pointer. */
	g_value = 0;
	g_handle = (long *)0;

	pp = opaque_pp(&g_handle);
	q  = opaque_p(&g_value);

	/* The Phase B' shape:
	 *   storel %q_tmp, %pp_tmp   ; both Kl, both spilled to slots
	 * Pre-fix wrote q's value into pp's spill slot, leaving g_handle
	 * unchanged.  Post-fix loads pp's slot → ES:BX, writes q through
	 * [ES:BX] → g_handle = q. */
	*pp = q;

	if (g_handle == q) printf("storel_kl_slot ok\r\n");
	else               printf("storel_kl_slot FAIL\r\n");

	/* Case 2: same shape, different destination — guards against any
	 * accidental aliasing in the spill slot layout (e.g. the buggy
	 * write happens to clobber the right place by coincidence in one
	 * test but not the other). */
	g_handle2 = (long *)0;
	pp = opaque_pp(&g_handle2);
	q  = opaque_p(&g_value);
	*pp = q;

	if (g_handle2 == q) printf("storel_kl_slot_alt ok\r\n");
	else                printf("storel_kl_slot_alt FAIL\r\n");

	/* Case 3: chain — store, then store again to make sure each *pp =
	 * q hits a distinct location.  If the write went into pp's slot
	 * (the buggy path), the second write would overwrite the first's
	 * apparent effect; with the fix, both globals end up with their
	 * respective q values. */
	g_handle  = (long *)0;
	g_handle2 = (long *)0;
	pp = opaque_pp(&g_handle);
	*pp = opaque_p(&g_value);
	pp = opaque_pp(&g_handle2);
	*pp = opaque_p((long *)0xDEAD);

	if (g_handle == &g_value) printf("storel_chain_a ok\r\n");
	else                      printf("storel_chain_a FAIL\r\n");
	if (g_handle2 == (long *)0xDEAD) printf("storel_chain_b ok\r\n");
	else                             printf("storel_chain_b FAIL\r\n");

	return 0;
}
