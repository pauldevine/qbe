/*
 * volatile_struct_probe.c — compile-time probe for C `volatile` on STRUCT
 * MEMBERS and on a whole VOLATILE AGGREGATE reached through a pointer
 * (`volatile struct S *p; p->m`), the follow-on to the §3l pointer-to-
 * volatile-scalar extend phase.
 *
 * Two distinct sources of member volatility, both honored at the member-
 * access sites (expr/lval `case '.'`):
 *
 *   1. A volatile-qualified MEMBER (`struct { volatile int ctrl; ... }`):
 *      the QVOLATILE bit rides inside m->ctyp (from the `VOLATILE T` member
 *      type), so the member load/store already emits the QBE `volatile`
 *      keyword.  vm_read/vm_fwd exercise this (regression guard — it worked
 *      before the aggregate fix too).
 *
 *   2. A volatile-qualified AGGREGATE reached through a pointer
 *      (`volatile struct S *p`): the QVOLATILE bit rides on the struct TYPE
 *      (from the `VOLATILE STRUCT/UNION IDENT` productions), shifts up through
 *      IDIR and back down through DREF at the `*p` deref, and the member-
 *      access sites OR ISVOLATILE(s0.ctyp) onto each member's value type so
 *      EVERY member access through `p` is volatile.  This is the canonical
 *      memory-mapped register-block case.  vs_read/vs_fwd exercise it.
 *
 * volatile is a CODEGEN property (not runtime-observable in a self-contained
 * program), so this is checked by INSPECTING THE EMITTED ASM
 * (tools/test-dos.sh run_volatile_struct_asm_probe) under the MEDIUM model —
 * the discriminating model where a near member access uses loadw/storew that
 * loadopt CAN forward/CSE.  (Under compact/large a member access goes through
 * the i8086 far loadfw/storefw ops, which QBE's loadopt does not optimize, so
 * a far volatile access is conservatively honored regardless; the keyword is
 * still emitted at the IR level and is future-proof.)
 *
 * Each pair shares an IDENTICAL body; the only difference is `volatile`.  The
 * volatile twin MUST keep MORE memory ops than its plain twin:
 *
 *   - *_read(): two reads of the member.  Volatile keeps both; the plain twin
 *     CSEs the second read away.
 *   - *_fwd(): `m = 5; return m`.  Volatile reloads; the plain twin forwards
 *     the stored constant 5 without a reload.
 *
 * Bug-loud: without the member-access volatile emit, the volatile twin
 * optimizes identically to the plain one, so the strict-greater asserts fail.
 * The plain twins also confirm non-volatile struct codegen is unchanged.
 */

struct Plain { int ctrl; int data; };
struct WithVol { volatile int ctrl; int data; };

/* Case 1: a volatile MEMBER, accessed through a plain pointer. */
int vm_read(struct WithVol *r) { return r->ctrl + r->ctrl; }
int nm_read(struct Plain *r)   { return r->ctrl + r->ctrl; }
int vm_fwd(struct WithVol *r)  { r->ctrl = 5; return r->ctrl; }
int nm_fwd(struct Plain *r)    { r->ctrl = 5; return r->ctrl; }

/* Case 2: a whole volatile AGGREGATE, accessed through a volatile pointer —
 * the plain member `data` is volatile here ONLY because the aggregate is. */
int vs_read(volatile struct Plain *r) { return r->data + r->data; }
int ns_read(struct Plain *r)          { return r->data + r->data; }
int vs_fwd(volatile struct Plain *r)  { r->data = 5; return r->data; }
int ns_fwd(struct Plain *r)           { r->data = 5; return r->data; }

int
main(void)
{
	static struct WithVol wv;
	static struct Plain p;
	return vm_read(&wv) + nm_read(&p) + vm_fwd(&wv) + nm_fwd(&p)
	     + vs_read(&p) + ns_read(&p) + vs_fwd(&p) + ns_fwd(&p);
}
