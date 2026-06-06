/*
 * volatile_ptr_probe.c — compile-time probe for POINTER-TO-VOLATILE / MMIO
 * (`volatile T *p; *p = x; y = *p;`), the §3l EXTEND phase of C `volatile`.
 *
 * Unlike a named volatile object (§3j local/§3k global, tracked by
 * varh[].isvolatile + symb_isvolatile), the qualifier here belongs to the
 * POINTEE, not the pointer object `p`.  minic encodes it as a QVOLATILE bit
 * riding inside the pointer type — IDIR(T|QVOLATILE) — recovered by DREF when
 * `*p` is dereferenced, so the deref's load()/loadfar()/store emits the QBE
 * `volatile` keyword while `p` itself stays an ordinary (non-volatile)
 * pointer.  The four QBE gates (promote/loadopt/coalesce/gcm) then keep the
 * access.  This is the canonical memory-mapped-I/O case.
 *
 * volatile is a CODEGEN property, not runtime-observable in a self-contained
 * program, so this is checked by INSPECTING THE EMITTED ASM
 * (tools/test-dos.sh run_volatile_ptr_asm_probe) under the MEDIUM model — the
 * discriminating model, where a near deref uses loadw/storew that loadopt CAN
 * forward/CSE.  (Under compact/large a deref goes through the i8086 far
 * loadfw/storefw ops, which QBE's loadopt does not optimize at all, so a far
 * volatile deref is conservatively honored regardless; the keyword is still
 * emitted — verified at the IR level — and is future-proof.)
 *
 * The pair of functions in each case shares an IDENTICAL body; the only
 * difference is the `volatile` on the pointee.  The volatile member MUST keep
 * more memory ops than its plain twin:
 *
 *   - vp_read()/np_read(): two reads of `*p`.  Volatile keeps both; a plain
 *     `int *` CSEs the second read away.
 *   - vp_fwd()/np_fwd(): `*p = 5; return *p`.  Volatile reloads; a plain
 *     `int *` forwards the stored constant 5 without a reload.
 *
 * Bug-loud: without the QVOLATILE deref emit, the volatile twin optimizes
 * identically to the plain one (vp_* == np_*), so the strict-greater asserts
 * fail.  The plain twins also confirm non-volatile pointer codegen is
 * unchanged (byte-identical to before this change — QVOLATILE is never set).
 */

int vp_read(volatile int *p) { return *p + *p; }
int np_read(int *p)          { return *p + *p; }

int vp_fwd(volatile int *p)  { *p = 5; return *p; }
int np_fwd(int *p)           { *p = 5; return *p; }

int
main(void)
{
	static volatile int v;
	static int n;
	return vp_read(&v) + np_read(&n) + vp_fwd(&v) + np_fwd(&n);
}
