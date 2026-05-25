/*
 * huge_stack_arith_probe.c -- validating probe for the Phase B
 * Var-operand carveout removal in minic.y::huge_ptr_binop.
 *
 * Pre-removal, `stk + i` (any stack pointer + offset) stayed on the
 * flat `add ax,lo / adc dx,hi` path under huge, because the backend's
 * Ostorel-via-Kl-slot direct-write would otherwise have mis-stored a
 * normalised pointer's value into the spill slot rather than through
 * the pointer.  Session ff closed that gap (Phase B': fn->arg_slot_top
 * threshold in i8086/emit.c), and this session lifts the carveout in
 * minic.y so stack pointer arith routes through _qbe_huge_add
 * identically to global pointer arith.
 *
 * The probe exercises three shapes:
 *
 *   (1) Plain *(stk + i) = v / x = *(stk + i) inside a loop.  Stresses
 *       the normalised-add + far store/load round trip without an
 *       opaque call boundary -- pure carveout-removal coverage.
 *
 *   (2) Function-call indirection through opaque_p(stk), forcing the
 *       returned Kl ptr into DX:AX then a spill slot.  *(p + i) = v
 *       then emits storefw through the spilled normalised pointer --
 *       i.e. the Phase B' path Ostorel-via-RSlot-deref of the holding
 *       Kl pointer tmp (the value being stored is Kw / 16-bit).
 *
 *   (3) Cross-check against a global heap[] of the same shape, which
 *       already routes through _qbe_huge_add pre-carveout-removal
 *       (globals are Glo/Ext, never Var).  Equality of post-write
 *       contents proves stack-Var arith produces the same answers
 *       global arith does.
 *
 * The element type is `int` (Kw / 16-bit on i8086), not `long`,
 * because minic.y::storefar() only handles b/h/w widths -- there is
 * no storefl / Ostorefl, so long-through-far-ptr is a separate
 * latent gap orthogonal to the carveout this probe exercises.  Using
 * int keeps the probe focused on the carveout removal itself.
 *
 * Pointer values are never compared (a normalised (seg,off) and a
 * flat (seg,off) to the same linear address are != under naive ceql
 * Kl); only dereffed scalar values are compared.
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/huge_stack_arith_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/huge_stack_arith_probe/huge_stack_arith_probe.exe \
 *             | diff - minic/dos/tests/huge_stack_arith_probe.golden.txt
 */

#include <stdio.h>

#define N 64

static int heap[N];

/* Opaque indirection: minic emits an Ocallfar to retrieve the returned
 * Kl pointer, so QBE can't fold the value at the use site.  Forces the
 * returned 4-byte pointer through DX:AX into a spill slot. */
int *opaque_p(int *p) { return p; }

int
main(void)
{
	int stk[N];
	int *ps;
	int *ph;
	int i;
	int ok;

	/* (1) Direct stack arith through *(stk + i).  Carveout removal
	 * means each iteration emits a call to _qbe_huge_add(stk_addr,
	 * i*2); the normalised result is then stored through. */
	for (i = 0; i < N; i++) {
		*(stk + i) = i * 7 + 3;
	}
	ok = 1;
	for (i = 0; i < N; i++) {
		if (*(stk + i) != i * 7 + 3)
			ok = 0;
	}
	if (ok) printf("stk_direct ok\r\n");
	else    printf("stk_direct FAIL\r\n");

	/* (2) Opaque indirection: opaque_p(stk) returns the same address
	 * through DX:AX; minic spills the Kl return into a slot before the
	 * pointer-arith call.  *(ps + i) = v then exercises the Phase B'
	 * Ostorel-via-RSlot-deref path on a normalised stack pointer (the
	 * stored value is Kw, but the pointer holding it is Kl). */
	ps = opaque_p(stk);
	for (i = 0; i < N; i++) {
		*(ps + i) = i * 11 + 5;
	}
	ok = 1;
	for (i = 0; i < N; i++) {
		if (stk[i] != i * 11 + 5)
			ok = 0;
	}
	if (ok) printf("stk_opaque ok\r\n");
	else    printf("stk_opaque FAIL\r\n");

	/* (2b) Read-back through the opaque pointer as well, exercising
	 * Oload Kl via RSlot deref (Phase B' load path). */
	ok = 1;
	for (i = 0; i < N; i++) {
		if (*(ps + i) != i * 11 + 5)
			ok = 0;
	}
	if (ok) printf("stk_opaque_read ok\r\n");
	else    printf("stk_opaque_read FAIL\r\n");

	/* (3) Cross-check against heap[] -- the global path (already
	 * normalised pre-carveout-removal). */
	for (i = 0; i < N; i++) {
		*(heap + i) = i * 11 + 5;
	}
	ok = 1;
	for (i = 0; i < N; i++) {
		if (heap[i] != stk[i])
			ok = 0;
	}
	if (ok) printf("stk_eq_heap ok\r\n");
	else    printf("stk_eq_heap FAIL\r\n");

	/* (3b) Opaque heap pointer: mirror of (2) for globals, confirming
	 * the same arith path produces the same results. */
	ph = opaque_p(heap);
	for (i = 0; i < N; i++) {
		*(ph + i) = i * 13 + 1;
	}
	ok = 1;
	for (i = 0; i < N; i++) {
		if (heap[i] != i * 13 + 1)
			ok = 0;
	}
	if (ok) printf("heap_opaque ok\r\n");
	else    printf("heap_opaque FAIL\r\n");

	/* (4) Boundary samples printed verbatim so a partial corruption
	 * (e.g. only mid-array indices misaddressed) shows up in the diff
	 * instead of being hidden behind the boolean reductions above. */
	printf("stk[0]=%d\r\n",    stk[0]);
	printf("stk[31]=%d\r\n",   stk[31]);
	printf("stk[63]=%d\r\n",   stk[63]);
	printf("heap[0]=%d\r\n",   heap[0]);
	printf("heap[31]=%d\r\n",  heap[31]);
	printf("heap[63]=%d\r\n",  heap[63]);

	return 0;
}
