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
 * Stack layout after prologue (near call - tiny/small models):
 *   [bp+6]  arg1 (second parameter)
 *   [bp+4]  arg0 (first parameter)
 *   [bp+2]  return address (2 bytes: offset only)
 *   [bp+0]  saved BP  <-- BP points here
 *   [bp-2]  local variable
 *   ...
 *
 * Stack layout after prologue (far call - medium/large/huge models):
 *   [bp+8]  arg1 (second parameter)
 *   [bp+6]  arg0 (first parameter)
 *   [bp+2]  return address (4 bytes: segment:offset)
 *   [bp+0]  saved BP  <-- BP points here
 *   [bp-2]  local variable
 *   ...
 */

/* Check if current memory model uses far code (requires RETF, CALL FAR) */
static int
uses_far_code(void)
{
	return T.memmodel == Mmedium ||
	       T.memmodel == Mlarge ||
	       T.memmodel == Mhuge;
}

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

	/* Parameters start at [bp+4] for near calls, [bp+6] for far calls:
	 *
	 * Near call (tiny/small models):
	 *   [bp+0] = saved BP
	 *   [bp+2] = return address (2 bytes: offset only)
	 *   [bp+4] = first parameter  (slot -2, since 2*-(-2)=4)
	 *   [bp+6] = second parameter (slot -3, since 2*-(-3)=6)
	 *
	 * Far call (medium/large/huge models):
	 *   [bp+0] = saved BP
	 *   [bp+2] = return address offset
	 *   [bp+4] = return address segment
	 *   [bp+6] = first parameter  (slot -3, since 2*-(-3)=6)
	 *   [bp+8] = second parameter (slot -4, since 2*-(-4)=8)
	 *
	 * The slot() function in emit.c converts:
	 *   s < 0: return 2 * -s  (parameters)
	 *   s >= 0: return -2 * (fn->slot - s)  (locals)
	 */
	if (uses_far_code())
		s = -3;  /* Far call: params start at [bp+6] */
	else
		s = -2;  /* Near call: params start at [bp+4] */

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
			} else if (i->cls == Ks) {
				/* Float parameter (4 bytes) - load from stack */
				emit(Oload, Ks, i->to, SLOT(s), R);
				s -= 2;  /* Float takes 4 bytes = 2 words */
			} else if (i->cls == Kd) {
				/* Double parameter (8 bytes) - load from stack */
				emit(Oload, Kd, i->to, SLOT(s), R);
				s -= 4;  /* Double takes 8 bytes = 4 words */
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
	 * Skip variadic markers (empty arguments)
	 */
	stk = 0;
	nargs = 0;
	for (i = i0; i < icall; i++) {
		if (!isarg(i->op))
			continue;
		if (req(i->arg[0], R))
			continue;  /* Skip variadic marker */
		nargs++;
		/* Each argument takes at least 2 bytes (one word) */
		if (i->cls == Kl) {
			stk += 4;  /* 32-bit long takes 4 bytes */
		} else if (i->cls == Ks) {
			stk += 4;  /* float takes 4 bytes */
		} else if (i->cls == Kd) {
			stk += 8;  /* double takes 8 bytes */
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
			emit(Ocopy, icall->cls, icall->to, TMP(RAX), R);
			cty |= 1;  /* 1 GP register returned */
		}
		/* No FP support yet */
	}

	/* 3. Emit the call (far call for medium/large/huge models) */
	if (uses_far_code())
		emit(Ocallfar, 0, R, icall->arg[0], CALL(cty));
	else
		emit(Ocall, 0, R, icall->arg[0], CALL(cty));

	/* 2. Pass arguments on stack (right-to-left for cdecl)
	 *
	 * FINAL SIMPLIFIED APPROACH for 8086:
	 * The problem: mov [ax], value is invalid (AX can't be base register)
	 * The solution: Use BP-relative addressing instead!
	 *
	 * After allocating stack space, we can store arguments using
	 * [BP-offset] addressing, which is valid on 8086.
	 *
	 * Example:
	 *   sub sp, 4      ; Allocate 4 bytes
	 *   mov [bp-4], 72  ; Store arg1 (at SP position)
	 *   mov [bp-2], 105 ; Store arg2 (at SP+2 position)
	 *   call func
	 *   add sp, 4      ; Clean up
	 */
	if (stk > 0) {
		/* Emit stores FIRST (so they execute AFTER allocation due to reversal)
		 * Then emit allocation LAST (so it executes FIRST)
		 *
		 * Execution order will be:
		 * 1. sub sp, stk       (allocate space)
		 * 2. mov [bp-stk], arg1  (store first argument)
		 * 3. mov [bp-stk+2], arg2 (store second argument)
		 * 4. call function
		 */

		/* Calculate offset for each argument and emit stores
		 * After "sub sp, stk", the arguments are at [bp-stk], [bp-stk+2], etc.
		 */
		off = stk;  /* Start from the bottom of allocated space */
		for (i = i0; i < icall; i++) {
			if (!isarg(i->op))
				continue;
			if (req(i->arg[0], R))
				continue;

			int arg_size;
			if (i->cls == Kl) arg_size = 4;
			else if (i->cls == Ks) arg_size = 4;
			else if (i->cls == Kd) arg_size = 8;
			else arg_size = 2;

			/* Store this argument at [bp - off]
			 * Create a memory reference with BP as base and negative offset
			 */
			int midx = fn->nmem++;
			vgrow(&fn->mem, fn->nmem);
			fn->mem[midx] = (Mem){
				.base = TMP(RBP),  /* Use BP as base */
				.index = R,
				.offset = {.type = CBits, .bits.i = -off},
				.scale = 0
			};
			Ref mem_ref = MEM(midx);

			/* Emit store based on type */
			if (i->cls == Ks)
				emit(Ostores, Ks, R, i->arg[0], mem_ref);
			else if (i->cls == Kd)
				emit(Ostored, Kd, R, i->arg[0], mem_ref);
			else
				emit(Ostorew, Kw, R, i->arg[0], mem_ref);

			off -= arg_size;  /* Move to next argument position */
		}

		/* NOW emit allocation (emitted last, executes first) */
		emit(Osalloc, Kw, R, getcon(stk, fn), R);
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j, ca;
	Ref r0;
	int farret;

	(void)fn;  /* unused */

	j = b->jmp.type;
	farret = uses_far_code();

	/* Handle void returns - convert to far return if needed */
	if (j == Jret0) {
		if (farret)
			b->jmp.type = Jretf0;
		return;
	}

	/* Only handle returns with values */
	if (!isret(j))
		return;

	r0 = b->jmp.arg;

	/* Move return value to AX (word) or DX:AX (long) */
	if (j == Jretw) {
		/* Word return - copy to AX */
		emit(Ocopy, Kw, TMP(RAX), r0, R);
		ca = 1;  /* 1 GP register used for return */
		b->jmp.type = farret ? Jretfw : Jret0;
	} else if (j == Jretl) {
		/* Long return - DX:AX pair */
		emit(Ocopy, Kw, TMP(RDX), r0, R);  /* High word */
		emit(Ocopy, Kw, TMP(RAX), r0, R);  /* Low word */
		ca = 2;  /* 2 GP registers used for return (DX:AX) */
		b->jmp.type = farret ? Jretfl : Jret0;
	} else {
		/* No support for float returns yet - convert to void return */
		b->jmp.type = farret ? Jretf0 : Jret0;
		return;
	}

	/* Tell register allocator that return uses these registers */
	b->jmp.arg = CALL(ca);
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
		selret(b, fn);

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
