/*
 * volatile_global_probe.c — compile-time probe for C `volatile` on named
 * file-scope GLOBAL objects (minic + QBE i8086 backend), the §3j EXTEND phase.
 *
 * A global has no `alloc`, so the §3j markvol() pass (which propagates the
 * vol bit from a volatile alloc to its loads/stores) cannot reach it.  Instead
 * minic emits the QBE `volatile` keyword DIRECTLY on the load/store of a
 * volatile global (symb_isvolatile at the scalar load()/loadfar()/store
 * sites).  The four QBE gates (promote/loadopt/coalesce/gcm) then keep it.
 *
 * volatile is a CODEGEN property, not runtime-observable in a self-contained
 * program, so this is checked by INSPECTING THE EMITTED ASM
 * (tools/test-dos.sh run_volatile_global_asm_probe) under the MEDIUM model —
 * the discriminating model, where a global uses the standard near loadw/storew
 * that loadopt CAN forward/CSE.  (Under compact/large a global goes through the
 * i8086 far loadfw/storefw ops, which QBE's loadopt does not optimize at all,
 * so volatile is conservatively honored there regardless; the keyword is still
 * emitted — verified at the IR level — and is future-proof.)
 *
 *   - vg_load() / vg_fwd() read a `volatile int` such that a NON-volatile
 *     global would be CSE'd (the second read reused) or forwarded (the stored
 *     constant returned without a reload).  Every volatile load MUST re-read
 *     memory: 2 word loads in vg_load, store+reload in vg_fwd → 4 word mem ops.
 *     Bug-loud: against a QBE/minic without the global volatile emit, vg_load
 *     folds to one load and vg_fwd forwards the constant → only 2 word ops.
 *
 *   - ng_load() / ng_fwd() are the identical bodies on a plain `int`.  They
 *     MUST optimize (1 word op each → 2 total), proving the probe discriminates
 *     and that non-volatile global codegen is unchanged (byte-identical).
 */

volatile int vg;
int ng;

/* extern: declared here, defined in another TU.  `extern volatile` must also
 * mark the symbol (varaddextern consumes the qualifier like varadd). */
extern volatile int evg;
extern int eng;

/* two reads of the same global: volatile keeps both; plain int CSEs to one */
int vg_load(void) { return vg + vg; }
int ng_load(void) { return ng + ng; }

/* store then read: volatile reloads; plain int forwards the stored constant */
int vg_fwd(void) { vg = 7; return vg; }
int ng_fwd(void) { ng = 7; return ng; }

/* same CSE discrimination through an extern declaration */
int evg_load(void) { return evg + evg; }
int eng_load(void) { return eng + eng; }

int
main(void)
{
	return vg_load() + ng_load() + vg_fwd() + ng_fwd()
	     + evg_load() + eng_load();
}
