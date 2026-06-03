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

/* Check if current memory model uses far code (requires RETF, CALL FAR).
 *
 * Compact is conventionally "near code, far data".  But our toolchain
 * places each .obj's CODE class into its own physical 64KB segment
 * (per asm_to_omf / omf_link), so user code and libstub already live in
 * distinct CS frames.  Treating compact as far-code here lets the
 * crt0's `call far _main` line up with main's `retf`, and inter-module
 * calls (cprobe -> _far_printf) traverse segments safely.  The "near
 * code" of compact stays an aspirational property: it can be regained
 * later by coalescing CODE segments under a single `_TEXT` name.
 */
static int
uses_far_code(void)
{
	return T.memmodel == Mmedium ||
	       T.memmodel == Mcompact ||
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
				/* 32-bit parameter (takes 4 bytes = 2 words).  The
				 * Oload below materializes the param into its temp; on
				 * i8086 spill.c::spill aliases this temp's slot to the
				 * incoming ABI slot s so the load becomes an elided
				 * self-copy (see the "Kl param" pass in spill.c).  We
				 * must NOT pre-set tmp[].slot here: isel overloads a
				 * set .slot to mean "fast-local alloca address" and
				 * would materialize &param instead of the value. */
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

	/* Record the BP-relative byte offset of the first variadic argument
	 * (just past the named params).  SLOT(s) maps to 2*-s, so after the
	 * loop `s` indexes the next stack slot = the first vararg.  The Ovargp
	 * op (va_start) materialises SS:(bp+vararg_off).  See
	 * [[project-minic-vararg-stub]]. */
	fn->vararg_off = 2 * -s;
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
	 * Execution order: store args -> call -> get result
	 * Emit order: get result -> call -> store args
	 *
	 * NOTE: arg slots are pre-reserved at the bottom of the locals frame
	 * (see i8086_abi).  The prologue's `sub sp, 2*fn->slot` already accounts
	 * for them, so we don't emit any per-call sub/add of SP.  Args are
	 * written directly into the reserved slots via SLOT() refs.
	 */

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

	/* 2. Pass arguments via pre-reserved slots at the bottom of the
	 * locals frame.  Slot indices 0..arg_words-1 always lie at the
	 * deepest part of the frame (just above SP after prologue), so
	 * they hand off to a far/near call's return-address push naturally.
	 *
	 * Layout (after prologue, just before this call's stores):
	 *   SP = BP - 2*fn->slot   (set by `sub sp, 2*fn->slot` in prologue)
	 *   slot 0   → [BP - 2*fn->slot]      = [SP]      (first arg)
	 *   slot 1   → [BP - 2*(fn->slot-1)]  = [SP+2]    (second arg)
	 *   ...
	 *
	 * Far CALL pushes 4 bytes of return at [SP-4..SP-1]; the callee's
	 * `[bp+6]` (after push bp; mov bp,sp) reads our slot 0.  ✓
	 */
	if (stk > 0) {
		off = 0;  /* slot index for first arg = bottom of arg region */
		for (i = i0; i < icall; i++) {
			if (!isarg(i->op))
				continue;
			if (req(i->arg[0], R))
				continue;

			int arg_words;
			if (i->cls == Kl) arg_words = 2;
			else if (i->cls == Ks) arg_words = 2;
			else if (i->cls == Kd) arg_words = 4;
			else arg_words = 1;

			Ref slot_ref = SLOT(off);

			/* Emit store based on type */
			if (i->cls == Ks)
				emit(Ostores, Ks, R, i->arg[0], slot_ref);
			else if (i->cls == Kd)
				emit(Ostored, Kd, R, i->arg[0], slot_ref);
			else if (i->cls == Kl)
				emit(Ostorel, Kw, R, i->arg[0], slot_ref);
			else
				emit(Ostorew, Kw, R, i->arg[0], slot_ref);

			off += arg_words;  /* Next slot index */
		}
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j, cty;
	Ref r0;
	int farret;

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
		cty = 1;  /* 1 GP register used (AX) */
		b->jmp.type = farret ? Jretfw : Jret0;
	} else if (j == Jretl) {
		/* Always route through Ofarseg/Ofaroff: Ocopy Kw on a Kl source
		 * extracts only the low word (rega has no register-pair concept
		 * on 8086), so the fallback path produced `mov dx, [slot+0]` for
		 * the high half: duplicate low word in DX:AX, segment lost.
		 * Surfaced 2026-05-23 (j) by fnptrprobe.c via an indirect call
		 * returning `char *` (Kl) in compact mode. */
		emit(Ofarseg, Kw, TMP(RDX), r0, R);
		emit(Ofaroff, Kw, TMP(RAX), r0, R);
		cty = 2;  /* 2 GP registers used (DX:AX) */
		b->jmp.type = farret ? Jretfl : Jret0;
	} else {
		/* No support for float returns yet - convert to void return */
		b->jmp.type = farret ? Jretf0 : Jret0;
		return;
	}

	/* Encode which registers contain return value */
	b->jmp.arg = CALL(cty);
}

void
i8086_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0;
	int n0, n1, ioff;
	int max_arg_words;

	/* Pre-pass: compute the maximum arg-region size across all calls
	 * in this function and reserve that many slots at the bottom of
	 * the locals frame.  selcall stores args to slots 0..N-1, which
	 * the prologue's `sub sp, 2*fn->slot` allocates for free.
	 *
	 * Slot index 0 maps to [BP - 2*final_fn_slot] = [SP after prologue],
	 * so the first arg lands exactly where a CALL needs it.  All call
	 * sites in this function share these slots since only one call is
	 * live at a time.
	 */
	max_arg_words = 0;
	for (b = fn->start; b; b = b->link) {
		int call_words = 0;
		for (i = b->ins; i < &b->ins[b->nins]; i++) {
			if (isarg(i->op)) {
				if (req(i->arg[0], R))
					continue;
				if (i->cls == Kl) call_words += 2;
				else if (i->cls == Ks) call_words += 2;
				else if (i->cls == Kd) call_words += 4;
				else call_words += 1;
			} else if (i->op == Ocall) {
				if (call_words > max_arg_words)
					max_arg_words = call_words;
				call_words = 0;
			}
		}
		/* Also handle the last call in the block (no trailing op
		 * to flush against). */
		if (call_words > max_arg_words)
			max_arg_words = call_words;
	}
	fn->slot += max_arg_words;
	/* Record the call-arg slot count so emit can distinguish ABI's
	 * direct-slot writes (Ostorel %val, SLOT(off) where off lives in
	 * the call-arg region at the bottom of the frame) from frontend
	 * spilled-Kl-ptr writes (Ostorel %val, %ptr_spilled_to_slot where
	 * the slot HOLDS a pointer value).  See [[huge-phase-b-storel-gap]]
	 * and i8086/emit.c's Ostorel + Oload Kl handlers. */
	fn->arg_slot_top = max_arg_words;

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
