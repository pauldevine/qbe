/*
 * volatile_direct_probe.c — compile-time probe for C `volatile` on a DIRECT
 * (non-pointer) `volatile struct S s` object, accessed at an OFFSET>0 member.
 *
 * This closes §3m limitation (a): before, a directly-declared volatile
 * aggregate honored volatile ONLY for its offset-0 member (a global's
 * offset-0 access reaches the named symbol, so symb_isvolatile fired; a
 * local honored nothing).  An offset>0 member became a computed `$s+off` /
 * `%s+off` address, so neither symb_isvolatile nor any QVOLATILE bit reached
 * the load/store → the access was CSE'd / store-forwarded like plain memory.
 *
 * The fix (minic lval `case 'V'`): a direct volatile struct/union OBJECT now
 * re-derives its QVOLATILE qualifier (from varh[].isvolatile) onto the
 * aggregate lvalue, so EVERY member access — offset 0 AND offset>0 — sees
 * ISVOLATILE(s0.ctyp) and emits the QBE `volatile` keyword.
 *
 * volatile is a CODEGEN property (not runtime-observable in a self-contained
 * program), so this is checked by INSPECTING THE EMITTED ASM
 * (tools/test-dos.sh run_volatile_direct_asm_probe) under the MEDIUM model —
 * the discriminating model where a near member access uses loadw/storew that
 * loadopt CAN forward/CSE.  (Under compact/large a member access goes through
 * the i8086 far loadfw/storefw ops, which QBE's loadopt does not optimize, so
 * a far volatile access is conservatively honored regardless.)
 *
 * `data` is at offset 2 (after the 16-bit `ctrl`), so every assertion below
 * exercises the offset>0 path specifically.  Each volatile/plain pair shares
 * an IDENTICAL body; the volatile twin MUST keep strictly MORE memory ops:
 *
 *   - *_read(): two reads of s.data.  Volatile keeps both; plain CSEs.
 *   - *_fwd(): `s.data = 5; return s.data`.  Volatile reloads; plain forwards.
 *
 * Bug-loud: against a minic without the lval 'V' fix the volatile twins fold
 * to match their plain twins and the strict-greater asserts fail.  The plain
 * twins also confirm non-volatile direct-struct codegen is unchanged.
 */

struct S { int ctrl; int data; };

/* Direct volatile GLOBAL struct, offset>0 member. */
volatile struct S vg;
struct S          ng;

int vg_read(void) { return vg.data + vg.data; }
int ng_read(void) { return ng.data + ng.data; }
int vg_fwd(void)  { vg.data = 5; return vg.data; }
int ng_fwd(void)  { ng.data = 5; return ng.data; }

/* Direct volatile LOCAL struct, offset>0 member (seeded via a param so the
 * read is live and the slot stays in memory). */
int vl_read(int x) { volatile struct S s; s.data = x; return s.data + s.data; }
int nl_read(int x) {          struct S s; s.data = x; return s.data + s.data; }

int
main(void)
{
	return vg_read() + ng_read() + vg_fwd() + ng_fwd()
	     + vl_read(3) + nl_read(3);
}
