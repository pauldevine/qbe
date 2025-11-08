#include "all.h"

/* 16-bit x86 cdecl calling convention for DOS
 *
 * - Arguments pushed on stack right-to-left
 * - Caller cleans up stack
 * - Return value in AX (16-bit) or DX:AX (32-bit)
 * - Callee-save: BX, SI, DI, BP
 * - Caller-save: AX, CX, DX
 * - All arguments passed on stack (no register args in cdecl)
 */

/* layout of call's second argument (RCall)
 *
 *  29    4  2  0
 *  |0..00|xx|xx|
 *        |  ` gp regs returned (0..2)  [AX or DX:AX]
 *        ` fp regs returned    (0)     [none for now]
 *
 * All arguments go on stack for cdecl, so no arg regs encoded
 */

bits
i8086_retregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp;

	assert(rtype(r) == RCall);
	ngp = r.val & 3;
	nfp = 0;  /* no FPU for now */

	if (p) {
		p[0] = ngp;
		p[1] = nfp;
	}

	b = 0;
	/* Return in AX, or DX:AX for wide values */
	if (ngp >= 1)
		b |= BIT(RAX);
	if (ngp >= 2)
		b |= BIT(RDX);

	return b;
}

bits
i8086_argregs(Ref r, int p[2])
{
	/* cdecl: all args on stack, no register args */
	if (p) {
		p[0] = 0;
		p[1] = 0;
	}
	return 0;
}

static int
i8086_classify(Ref r, Fn *fn)
{
	int cls;

	/* Determine the class (size) of the value */
	switch (rtype(r)) {
	case RCon:
		cls = fn->con[r.val].type == CBits ? Kw : Kl;
		break;
	case RTmp:
		cls = fn->tmp[r.val].cls;
		break;
	default:
		cls = Kw;
		break;
	}

	/* 16-bit target: Kl (32-bit) needs two registers, Kw fits in one */
	return cls;
}

void
i8086_abi(Fn *fn)
{
	Blk *b;
	Ins *i;
	int stk;

	/* Process each basic block */
	for (b = fn->start; b; b = b->link) {
		stk = 0;

		for (i = b->ins; i < &b->ins[b->nins]; i++) {
			/* Handle function arguments */
			if (isarg(i->op)) {
				/* All arguments go on stack in cdecl */
				/* Stack grows down, args pushed right-to-left */
				/* Will be handled in emit phase */
			}

			/* Handle return values */
			if (isret(b->jmp.type)) {
				/* Return value goes in AX (or DX:AX) */
				/* Will be handled in emit phase */
			}
		}
	}

	/* TODO: Implement full ABI lowering:
	 * 1. Convert Oarg instructions to stack pushes
	 * 2. Convert return values to use AX/DX:AX
	 * 3. Handle structure passing
	 * 4. Insert callee-save register preservation
	 */
}
