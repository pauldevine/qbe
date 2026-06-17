/*
 * isr_fnptr_param_probe.c -- function-POINTER parameter whose pointee carries
 * __far and/or __attribute__((interrupt)) qualifiers (§8r, the minic-PARSE
 * bucket: newlibc drivers/interrupts.c's isr_entry).  Upstream spells the
 * IVT-installer helper
 *
 *   static void isr_entry(ivt_entry_t *entry,
 *                         void __far __attribute__((interrupt)) (*isr)(void))
 *
 * and minic's grammar accepted only `type '(' '*' IDENT ')' '(' fptpar0 ')'`
 * for a fn-ptr parameter -- a __far or __attribute__((...)) between the
 * return type and the `(*name)` declarator was a hard parse error (the
 * `type TFAR '*'` pointer-type extension dead-ended at the `(`).  The fix
 * adds an `fpquals` qualifier run (TFAR and/or an attribute) to the fn-ptr
 * param productions, dropping the qualifiers exactly as the function-header
 * `type TFAR attropt IDENT` rule does (they qualify the pointed-to function's
 * far/iret calling convention, a memory-model property here).
 *
 * Bug-loud TWO ways:
 *  (1) On the UNFIXED compiler every isr_entry-shaped function below is a
 *      parse error, so the program will not even build.
 *  (2) The param's __attribute__((interrupt)) MUST NOT leak onto the
 *      ENCLOSING function's linkage (ansi_func_proto reads cur_fn_interrupt
 *      AFTER the params are parsed).  A leak would compile isr_entry as a
 *      QBE `interrupt function`, giving it the ISR prologue/epilogue + iret;
 *      calling it through the normal call/ret ABI (as main does, twice) would
 *      then corrupt the stack and never return.  Reaching the final prints at
 *      all proves the enclosing functions stayed ordinary call/ret functions.
 *
 * The pointer values are compared two ways (two calls agree; the value the
 * callee stored equals the address main took) rather than printed, so the
 * golden is model-independent (near-code small/compact vs far-code
 * medium/large/huge).  Gated small + medium + compact + large + huge.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/isr_fnptr_param_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/isr_fnptr_param_probe/isr_fnptr_param_probe.exe \
 *             | diff - minic/dos/tests/isr_fnptr_param_probe.golden.txt
 */

#include <stdio.h>

/* The pointed-to "ISR" targets.  They are never run as real interrupts here;
 * the probe only takes their addresses (exactly as isr_entry does before
 * stuffing them into an IVT entry). */
static void target_a(void) { }
static void target_b(void) { }

/* (1) The real newlibc isr_entry shape: __far + __attribute__((interrupt))
 *     on the fn-ptr param's pointee.  The enclosing function is an ordinary
 *     call/ret function and must stay one. */
static void store_far_attr(unsigned long *out,
                           void __far __attribute__((interrupt)) (*isr)(void))
{
	*out = (unsigned long)(void *)isr;
}

/* (2) __far only on the pointee. */
static void store_far(unsigned long *out, void __far (*isr)(void))
{
	*out = (unsigned long)(void *)isr;
}

/* (3) __attribute__((interrupt)) only on the pointee. */
static void store_attr(unsigned long *out,
                       void __attribute__((interrupt)) (*isr)(void))
{
	*out = (unsigned long)(void *)isr;
}

int
main(void)
{
	unsigned long a1 = 0, a2 = 0, f = 0, t = 0;

	/* Call the far+attr helper TWICE: a leaked interrupt attribute would
	 * have made store_far_attr end in iret and crash this normal call. */
	store_far_attr(&a1, target_a);
	store_far_attr(&a2, target_a);
	printf("far_attr_stable=%d\n", (a1 == a2) ? 1 : 0);
	printf("far_attr_addr=%d\n",
	       (a1 == (unsigned long)(void *)target_a) ? 1 : 0);

	store_far(&f, target_b);
	printf("far_addr=%d\n", (f == (unsigned long)(void *)target_b) ? 1 : 0);

	store_attr(&t, target_b);
	printf("attr_addr=%d\n", (t == (unsigned long)(void *)target_b) ? 1 : 0);

	/* Distinct targets give distinct addresses (no slot aliasing). */
	printf("distinct=%d\n", (a1 != f) ? 1 : 0);

	printf("returned=1\n");
	return 0;
}
