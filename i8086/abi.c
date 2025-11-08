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
	Ins *i;
	int stk, off;
	Ref sp, addr;

	/* cdecl: arguments pushed right-to-left on stack
	 * Execution order:
	 *   1. sub sp, total_size   (allocate)
	 *   2. store args to [sp+off]
	 *   3. call function
	 *   4. add sp, total_size   (cleanup)
	 *
	 * Emission order (reversed):
	 *   1. add sp (cleanup)
	 *   2. call
	 *   3. stores
	 *   4. sub sp (allocate)
	 */

	stk = 0;

	/* Count total stack space needed */
	for (i = i0; i < icall; i++) {
		if (!isarg(i->op))
			continue;
		if (i->cls == Kl)
			stk += 4;  /* 32-bit arg */
		else
			stk += 2;  /* 16-bit or smaller */
	}

	/* 1. Emit stack cleanup (executed after call) */
	/* For i8086, we manually clean up the stack after the call */
	if (stk > 0) {
		sp = newtmp("abi", Kw, fn);
		emit(Ocopy, Kw, TMP(RSP), sp, R);
	}

	/* 2. Emit the call */
	emit(icall->op, icall->cls, icall->to, icall->arg[0], icall->arg[1]);

	/* 3. Emit argument stores (executed before call) */
	if (stk > 0) {
		/* Allocate a temp for the base of arguments */
		sp = newtmp("abi", Kl, fn);

		off = 0;
		for (i = i0; i < icall; i++) {
			int sz;

			if (!isarg(i->op))
				continue;

			sz = (i->cls == Kl) ? 4 : 2;

			/* Create address [sp+off] */
			if (off > 0) {
				addr = newtmp("abi", Kl, fn);
				emit(Oadd, Kl, addr, sp, getcon(off, fn));
			} else {
				addr = sp;
			}

			/* Store argument */
			if (i->cls == Kl)
				emit(Ostorel, Kw, R, i->arg[0], addr);
			else
				emit(Ostorew, Kw, R, i->arg[0], addr);

			off += sz;
		}

		/* 4. Emit stack allocation (executed first) */
		/* Use Osalloc with negative value to allocate */
		emit(Osalloc, Kl, sp, getcon(-stk, fn), R);
	}
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

	/* Set function register usage - no register arguments for cdecl */
	fn->reg = 0;

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

	/* Lower calls in all blocks */
	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];
		n = 0;

		for (i = &b->ins[b->nins]; i != b->ins;) {
			i--;

			if (i->op == Ocall) {
				/* Find arguments for this call */
				for (i0 = i; i0 > b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;

				selcall(fn, i0, i);
				i = i0;
			} else if (isarg(i->op)) {
				/* Skip - handled with call */
			} else {
				emiti(*i);
			}
		}

		/* Replace instructions */
		n0 = &insb[NIns] - curi;
		vgrow(&b->ins, n0);
		icpy(b->ins, curi, n0);
		b->nins = n0;
	}
}
