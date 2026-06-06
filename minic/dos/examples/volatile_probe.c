/*
 * volatile_probe.c — compile-time probe for C `volatile` on named local
 * objects (minic + QBE i8086 backend).
 *
 * volatile semantics are a CODEGEN property, not a runtime one: a
 * self-contained program with no external agent produces identical results
 * whether or not volatile accesses are optimized away.  So this probe is
 * checked by INSPECTING THE EMITTED ASM (tools/test-dos.sh
 * run_volatile_asm_probe), not by a runtime golden:
 *
 *   - volf()    declares a `volatile int` and writes it twice then reads it
 *               twice.  Every store (incl. the immediately-dead `x = 5`) and
 *               every load MUST survive in the asm — no promote/forward/
 *               dead-store-elim/reorder.  Bug-loud: against a QBE without the
 *               volatile gates, volf folds to a single `mov ax, 20` with NO
 *               memory accesses, and the probe fails.
 *
 *   - nonvolf() is the identical body with a plain `int`.  It MUST fold away
 *               (0 memory accesses) — the control proving the probe actually
 *               discriminates and that non-volatile codegen is unchanged.
 */

int sink;

int
volf(void)
{
	volatile int x;
	x = 5;   /* dead store: volatile keeps it */
	x = 10;
	return x + x;  /* two volatile loads */
}

int
nonvolf(void)
{
	int y;
	y = 5;
	y = 10;
	return y + y;  /* folds to 20 */
}

int
main(void)
{
	sink = volf() + nonvolf();
	return 0;
}
