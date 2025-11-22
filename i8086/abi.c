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
	int cty, nargs, stk, off;
	Ins *i;
	Ref r;

	/* Calculate stack space needed for arguments
	 * cdecl: all arguments on stack
	 * Arguments are already in reverse order in the Oarg sequence
	 */
	stk = 0;
	nargs = 0;
	for (i = i0; i < icall; i++) {
		if (!isarg(i->op))
			continue;
		nargs++;
		/* Each argument takes at least 2 bytes (one word) */
		if (i->cls == Kl) {
			stk += 4;  /* 32-bit long takes 4 bytes */
		} else {
			stk += 2;  /* 16-bit word or smaller */
		}
	}

	/* Set up call type encoding */
	cty = 0;

	/* emit() builds in reverse, so emit in reverse order of execution:
	 * Execution order: allocate -> store args -> call -> get result -> cleanup
	 * Emit order: cleanup -> get result -> call -> store args -> allocate
	 */

	/* 5. Caller cleanup (last emitted, last executed)
	 * Use Osalloc with negative value to deallocate (add to SP)
	 */
	if (stk > 0) {
		emit(Osalloc, Kw, R, getcon(-stk, fn), R);
	}

	/* 4. Handle return value (get result from AX after call) */
	if (!req(icall->to, R)) {
		/* Function returns a value */
		if (KBASE(icall->cls) == 0) {
			/* Integer return in AX */
			/* TODO: This causes register allocation issues
			 * emit(Ocopy, icall->cls, icall->to, TMP(RAX), R); */
			cty |= 1;  /* 1 GP register returned */
		}
		/* No FP support yet */
	}

	/* 3. Emit the call */
	emit(Ocall, 0, R, icall->arg[0], CALL(cty));

	/* 2. Store arguments to stack (right-to-left for cdecl)
	 * Process arguments in forward order for correct stack layout
	 */
	if (stk > 0) {
		Ref sp_tmp;

		sp_tmp = newtmp("abi", Kw, fn);
		off = 0;
		for (i = i0; i < icall; i++) {
			Ref addr;

			if (!isarg(i->op))
				continue;

			/* Skip variadic marker (Oarg with empty argument) */
			if (req(i->arg[0], R))
				continue;

			/* Calculate stack address [sp+off] */
			if (off == 0) {
				/* First argument at [sp] - use SP copy directly */
				addr = sp_tmp;
			} else {
				/* Subsequent arguments at [sp+off] */
				addr = newtmp("abi", Kw, fn);
			}

			/* Store argument at calculated address
			 * Emit store first (executed second) */
			emit(Ostorew+i->cls, Kw, R, i->arg[0], addr);

			/* Emit address calculation second (executed first)
			 * so addr is defined before use
			 * Use Ocopy then Oadd to compute addr = sp_tmp + off */
			if (off != 0) {
				Ref tmp;
				tmp = newtmp("abi", Kw, fn);
				emit(Oadd, Kw, addr, tmp, getcon(off, fn));
				emit(Ocopy, Kw, tmp, sp_tmp, R);
			}

			/* Move to next stack position */
			if (i->cls == Kl) {
				off += 4;
			} else {
				off += 2;
			}
		}

		/* Osalloc allocates stack and returns new SP in sp_tmp */
		emit(Osalloc, Kw, sp_tmp, getcon(stk, fn), R);
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j;
	Ref r0;

	j = b->jmp.type;

	/* Only handle returns with values */
	if (!isret(j) || j == Jret0)
		return;

	r0 = b->jmp.arg;
	b->jmp.type = Jret0;

	/* Move return value to AX (word) or DX:AX (long) */
	if (j == Jretw) {
		/* Word return - copy to AX */
		emit(Ocopy, Kw, TMP(RAX), r0, R);
	} else if (j == Jretl) {
		/* Long return - DX:AX */
		/* For now, just copy to AX (need to handle DX:AX pair) */
		emit(Ocopy, Kw, TMP(RAX), r0, R);
	}
	/* No support for float returns yet */
}

void
i8086_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0;
	int n0, n1, ioff;

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

	/* Lower calls and remove Oarg instructions
	 * Even without proper argument lowering, we must remove Oarg
	 * instructions or the register allocator will crash
	 */
	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];

		/* Handle function returns */
		/* TODO: selret is causing register allocation issues
		 * For now, returns are handled in emit phase */
		/* selret(b, fn); */

		for (i = &b->ins[b->nins]; i != b->ins;) {
			i--;

			if (i->op == Ocall) {
				/* Find arguments for this call */
				for (i0 = i; i0 > b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;

				/* For now, just emit the call and skip the args
				 * TODO: Properly lower arguments to stack operations
				 */
				selcall(fn, i0, i);

				/* Skip past the argument instructions */
				i = i0;
			} else if (isarg(i->op)) {
				/* Skip Oarg instructions - they should have been
				 * handled with their associated call
				 */
			} else {
				/* Regular instruction - emit it */
				emiti(*i);
			}
		}

		/* Replace instructions in the block */
		n0 = &insb[NIns] - curi;
		vgrow(&b->ins, n0);
		icpy(b->ins, curi, n0);
		b->nins = n0;
	}
}
