/*
 * volatile_copy_probe.c — compile-time probe for C `volatile` on a struct-to-
 * struct COPY (`*d = *s` / `d = s`), closing §3m limitation (b).
 *
 * Before this fix the word-by-word `emit_struct_copy` path (used for every
 * aggregate assignment) predated the QVOLATILE machinery and emitted plain
 * loadw/storew (loadfw/storefw under far-data) regardless of whether either
 * operand was volatile.  So a volatile struct copy — e.g. snapshotting a
 * memory-mapped register block `snap = *regblock` or programming one
 * `*regblock = cfg` — was treated as ordinary memory.
 *
 * The fix (emit_struct_copy, minic.y): the LOADS carry the QBE `volatile`
 * keyword when the SOURCE is volatile, the STORES when the DESTINATION is
 * volatile.  The qualifier reaches the copy as the QVOLATILE bit on each
 * aggregate lvalue (the deref of a `volatile struct S *` propagates the
 * pointee bit down via DREF; lval `case 'V'` re-derives it for a directly-
 * declared volatile object) or via the NAMED symbol of a volatile global.
 *
 * WHY THIS PROBE CHECKS THE IR, NOT AN ASM OP-COUNT (unlike the scalar
 * volatile probes): volatile's observable effect is preventing a redundant
 * access from being folded.  QBE folds redundant SCALAR global accesses
 * (CSE / store-forward), so the scalar probes can assert an asm op-count
 * delta.  But QBE does NOT optimize a multi-word aggregate copy — it neither
 * CSEs the copy's loads across the copy's own intervening stores nor dead-
 * store-eliminates global stores — so there is no asm fold to prevent and an
 * op-count would be identical with or without the keyword.  The fix lives
 * entirely in minic's emit_struct_copy, so the correct, directly bug-loud
 * granularity is IR-PRESENCE: the harness (run_volatile_copy_asm_probe)
 * inspects the emitted .ssa and asserts the `volatile` keyword appears on the
 * copy's loads/stores for the volatile twins and is ABSENT for the plain
 * twins.  Against a pre-fix minic the volatile twins carry no keyword and the
 * presence asserts fail.  Checked under MEDIUM (near loadw/storew); under
 * far-data the same keyword rides on loadfw/storefw (loadopt leaves those
 * untouched anyway, so volatile is conservatively honored there regardless).
 *
 * The struct is 5 bytes (two 16-bit words + a byte `tag`) so each copy
 * exercises BOTH the per-word and the byte-tail path of emit_struct_copy.
 */

struct S { int ctrl; int data; char tag; };

volatile struct S vsrc;   /* volatile source */
struct S          nsrc;   /* plain source    */
volatile struct S vdst;   /* volatile dest   */
struct S          ndst;   /* plain dest      */
struct S          sink;   /* plain scratch   */
struct S          feed;   /* plain scratch   */

/* Direct-global copies: SRC volatile -> volatile loads; DST volatile ->
 * volatile stores.  The plain twins carry no volatile keyword. */
int vcsrc(void) { sink = vsrc; return 0; }
int ncsrc(void) { sink = nsrc; return 0; }
int vcdst(void) { vdst = feed; return 0; }
int ncdst(void) { ndst = feed; return 0; }

/* Canonical MMIO pointer-deref copies: a `volatile struct S *` on the dest
 * side makes the stores volatile, on the source side makes the loads volatile. */
void vpcopy_dst(volatile struct S *d, struct S *s) { *d = *s; }
void vpcopy_src(struct S *d, volatile struct S *s) { *d = *s; }

int
main(void)
{
	return vcsrc() + ncsrc() + vcdst() + ncdst();
}
