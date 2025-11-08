#include "all.h"

/* 16-bit x86 cdecl calling convention for DOS
 *
 * - Arguments pushed on stack right-to-left
 * - Caller cleans up stack
 * - Return value in AX (16-bit) or DX:AX (32-bit)
 * - Callee-save: BX, SI, DI, BP
 * - Caller-save: AX, CX, DX
 * - All arguments passed on stack (no register args in cdecl)
 *
 * Stack layout after prologue:
 *   [bp+6]  arg1 (second parameter)
 *   [bp+4]  arg0 (first parameter)
 *   [bp+2]  return address
 *   [bp+0]  saved BP  <-- BP points here
 *   [bp-2]  local variable
 *   ...
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

static void
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Ins *i;
	int s;  /* Slot number for parameters */

	curi = &insb[NIns];

	/* Parameters start at [bp+4]:
	 *   [bp+0] = saved BP
	 *   [bp+2] = return address (near call)
	 *   [bp+4] = first parameter  (slot -2, since 2*-(-2)=4)
	 *   [bp+6] = second parameter (slot -3, since 2*-(-3)=6)
	 *   etc.
	 *
	 * The slot() function in emit.c converts:
	 *   s < 0: return 2 * -s  (parameters)
	 *   s >= 0: return -2 * (fn->slot - s)  (locals)
	 */
	s = -2;  /* Start at slot -2 for first parameter */

	/* Process each parameter instruction */
	for (i = i0; i < i1; i++) {
		if (!ispar(i->op))
			continue;

		/* For i8086 cdecl, all parameters come from stack */
		/* Emit a load from [bp+offset] */
		switch (i->op) {
		case Opar:
		case Oparc:
			/* Regular parameter - load from stack */
			if (i->cls == Kw) {
				/* 16-bit parameter */
				emit(Oload, Kw, i->to, SLOT(s), R);
				s--;  /* Next parameter is 2 bytes higher */
			} else if (i->cls == Kl) {
				/* 32-bit parameter (takes 4 bytes = 2 words) */
				emit(Oload, Kl, i->to, SLOT(s), R);
				s -= 2;  /* Next parameter is 4 bytes higher */
			} else {
				/* Byte/half-word parameters - still take at least 2 bytes on stack */
				emit(Oload, i->cls, i->to, SLOT(s), R);
				s--;
			}
			break;
		case Oparsb:
		case Oparub:
		case Oparsh:
		case Oparuh:
			/* Sign/zero-extended parameters */
			emit(Oload, i->cls, i->to, SLOT(s), R);
			s--;
			break;
		default:
			break;
		}
	}
}

static void
selcall(Fn *fn, Ins *i0, Ins *icall)
{
	/* For now, just emit the call and skip arguments
	 * This is a minimal implementation to get things working
	 * TODO: Properly lower arguments to stack stores
	 */
	(void)fn;
	(void)i0;

	/* Just emit the call as-is */
	emiti(*icall);
}

void
i8086_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0;
	int n, n0, n1, ioff;

	/* Lower parameters in the entry block */
	b = fn->start;

	/* Find where parameters end */
	for (i = b->ins; i < &b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;

	/* Transform parameter loads */
	if (i > b->ins) {
		selpar(fn, b->ins, i);

		/* Replace parameter instructions with loads */
		n0 = &insb[NIns] - curi;  /* number of new instructions */
		ioff = i - b->ins;        /* offset to first non-par instruction */
		n1 = b->nins - ioff;      /* number of remaining instructions */

		/* Grow instruction array */
		vgrow(&b->ins, n0 + n1);

		/* Copy remaining instructions */
		icpy(b->ins + n0, b->ins + ioff, n1);

		/* Copy new parameter load instructions */
		icpy(b->ins, curi, n0);

		b->nins = n0 + n1;
	}

	/* TODO: Lower calls and arguments
	 * For now, skip call lowering to get parameters working first
	 */
}
