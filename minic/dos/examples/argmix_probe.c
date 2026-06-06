/*
 * argmix_probe.c — coerce arguments on an INDIRECT (function-pointer) call so a
 * narrow arg handed to a wide parameter doesn't shift the stack-arg layout.
 *
 * §2q canonical victim: MicroPython compiles a function body's variable reads
 * through a METHOD TABLE — emitcommon.c:131
 *   emit_method_table->local(emit, qst, id->local_num, MP_EMIT_IDOP_LOCAL_FAST)
 * whose member type is  void (*)(emit_t*, qstr, mp_uint_t local_num, int kind),
 * a (l, w, l, w) call.  `id->local_num' is a uint16_t (Kw), but the parameter
 * is mp_uint_t (Kl, 4 bytes under far-data).  minic's coerce_arg only fired for
 * DIRECT calls (fnproto, keyed by name); the indirect path eval'd args without
 * coercing, so local_num stayed `w' (2-byte push) where the callee reads 4 —
 * shifting `kind' so it was read from the wrong slot (arrived 0xb0, not 0).  The
 * LOAD_FAST emitter then took the 2-byte `LOAD_FAST_N + kind' branch and emitted
 * `d4 NN' bytecode the VM landed outside the body -> def add(a,b): return a+b
 * hung.  The SSA was correct on both sides; the fix records each fn-ptr
 * declarator's parameter types (struct member or `T (*fp)(...)' variable) and
 * coerces arguments to them at the indirect call.
 *
 * Two reproductions of the (l, w, l, w) shape with a narrow 3rd arg (widened
 * w->l) and a trailing `int k': a method-table member call (the MP shape) and a
 * directly-declared fn-ptr variable.  Without the fix, `kind' is read from the
 * wrong slot -> `m0'/`fp0' come back non-zero.  On medium (near data) a pointer
 * param is itself `w' but `unsigned long' is `l' in every model, so the middle
 * Kl arg still forces the coercion; gated medium + compact + large.
 *
 * The method table is filled at RUNTIME (not a `static const' initializer) so
 * the probe needs no --far-static-data: a code-symbol in a static data
 * initializer is only relocated to a far seg:off under that opt-in (which the
 * MicroPython port enables), whereas a runtime `store' of the function gets the
 * correct code segment in every model.  Either way the call-site coercion under
 * test is identical.
 *
 * NOTE: the fn-ptr-via-TYPEDEF form (`typedef int (*F)(...); F fp;') is now
 * also coerced (§2s) — the proto is recorded on the typedef entry and
 * transfers to the variable/member.  See typedef_fnptr_probe.c.
 */

#include <stdio.h>

static int g;

/* returns k so a mis-passed 4th arg is bug-loud; folds a and n in too so a
 * shift in the 3rd (Kl) slot would also corrupt the result. */
static int take4(void *p, unsigned a, unsigned long n, int k)
{
	if (p != (void *)&g) return -1000;          /* far ptr arrived intact   */
	return (int)(a * 1000u) + (int)(n * 100u) + k;
}

/* Method table mirroring MicroPython's mp_emit_method_table_id_ops_t. */
struct ops {
	int (*local)(void *p, unsigned a, unsigned long n, int k);
};

int main(void)
{
	unsigned short narrow_n;     /* the uint16_t local_num analogue (Kw)     */
	int (*fp)(void *p, unsigned a, unsigned long n, int k) = take4;
	struct ops bc_ops;
	const struct ops *t = &bc_ops;
	int r;

	bc_ops.local = take4;        /* runtime fill (see header note) */

	/* --- method-table member call: the exact MicroPython shape --- */
	narrow_n = 2;
	r = t->local(&g, 1, narrow_n, 0);
	if (r == 1200) printf("m0 ok\r\n"); else printf("m0 FAIL %d\r\n", r);

	narrow_n = 3;
	r = t->local(&g, 4, narrow_n, 7);
	if (r == 4307) printf("m7 ok\r\n"); else printf("m7 FAIL %d\r\n", r);

	/* --- directly-declared fn-ptr variable --- */
	narrow_n = 2;
	r = fp(&g, 1, narrow_n, 0);
	if (r == 1200) printf("fp0 ok\r\n"); else printf("fp0 FAIL %d\r\n", r);

	narrow_n = 5;
	r = (*fp)(&g, 6, narrow_n, 9);
	if (r == 6509) printf("fp9 ok\r\n"); else printf("fp9 FAIL %d\r\n", r);

	return 0;
}
