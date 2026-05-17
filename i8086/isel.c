#include "all.h"

/* Instruction selection for 8086/286/386 16-bit mode
 *
 * This is a minimal implementation to get started.
 * It needs significant expansion to handle all QBE IR operations.
 */

static void
fixarg(Ref *r, int k, Ins *i, Fn *fn)
{
	Ref r0, r1;
	int s;

	(void)i; /* unused for now */
	r0 = r1 = *r;

	switch (rtype(r0)) {
	case RCon:
		/* Constants can be used directly in most x86 instructions */
		/* Only load into temp if absolutely necessary */
		break;
	case RTmp:
		/* Check if this is a stack slot */
		if (isreg(r0))
			break;
		s = fn->tmp[r0.val].slot;
		if (s != -1) {
			/* Fast local: this temp's "value" is the address of a
			 * stack slot allocated by Oalloc4/8/16.  Materialize
			 * the address into a fresh temp via Oaddr (lea bp+off)
			 * before each use — mirrors amd64/isel.c.
			 *
			 * Use the ORIGINAL temp's class for the materialized
			 * address.  alloc4 results have class Kw on i8086
			 * (medium-model near pointers are 16-bit), so this keeps
			 * the address temp register-resident — important since
			 * spill.c forces Kl temps to be slot-resident, and a Kl
			 * address temp would become an RSlot that storew/loadw
			 * would write/read directly instead of dereferencing. */
			int ak = fn->tmp[r0.val].cls;
			r1 = newtmp("isel", ak, fn);
			emit(Oaddr, ak, r1, SLOT(s), R);
			break;
		}
		/* Check class compatibility */
		if (k == Kw && fn->tmp[r0.val].cls == Kl) {
			/* Need to narrow */
			/* For now, just use as-is */
		}
		break;
	}
	*r = r1;
}

static void
selcmp(Ins i, int k, int cmp, Fn *fn)
{
	Ins *i0;

	/* For x86, comparisons work as:
	 * 1. cmp arg0, arg1  (sets flags)
	 * 2. setCC dest      (sets dest based on flags)
	 *
	 * setCC needs an 8-bit-capable register (AX/BX/CX/DX low/high).
	 * The register allocator doesn't currently know about this; for
	 * non-byte-capable destinations the emit phase emits an `al`
	 * fallback (with comment).  TODO: hint or constraint mechanism.
	 */

	/* 32-bit comparisons use the Oc*l ops; emit.c has multi-step
	 * handlers (compare high then low, branch on flags) that handle
	 * RSlot, RCon, and RTmp arg combinations.  16-bit comparisons stay
	 * with the Oc*w ops and the format-string template. */

	/* 8086 cmp requires reg/mem on the left and immediate on the right;
	 * `cmp imm, reg` is illegal.  If the operands are flipped, swap them
	 * and invert the comparison so the flag-test semantics are preserved.
	 * (Equality/inequality are swap-symmetric; ordering relations need
	 * the operator inverted: a < b ↔ b > a, etc.) */
	if (rtype(i.arg[0]) == RCon && rtype(i.arg[1]) != RCon) {
		Ref tmp = i.arg[0]; i.arg[0] = i.arg[1]; i.arg[1] = tmp;
		switch (cmp) {
		case Cislt: cmp = Cisgt; break;
		case Cisgt: cmp = Cislt; break;
		case Cisle: cmp = Cisge; break;
		case Cisge: cmp = Cisle; break;
		case Ciult: cmp = Ciugt; break;
		case Ciugt: cmp = Ciult; break;
		case Ciule: cmp = Ciuge; break;
		case Ciuge: cmp = Ciule; break;
		/* Cieq, Cine: swap-symmetric */
		}
	}
	/* Two-constant cmp (`cmp imm, imm`) is illegal in any register/memory
	 * combination 8086 supports.  Hoist arg[0] into a fresh temp so the
	 * generated form becomes `cmp reg, imm`.  The Ocopy must execute
	 * before the cmp, so emit it AFTER emiti() — QBE's instruction buffer
	 * fills backwards (last emit() runs first at runtime). */
	Ref hoist_src = R;
	Ref hoist_dst = R;
	if (rtype(i.arg[0]) == RCon && rtype(i.arg[1]) == RCon) {
		hoist_src = i.arg[0];
		hoist_dst = newtmp("isel", k, fn);
		i.arg[0] = hoist_dst;
	}

	if (k == Kl) {
		switch (cmp) {
		case Cieq:  i.op = Oceql; break;
		case Cine:  i.op = Ocnel; break;
		case Cislt: i.op = Ocsltl; break;
		case Cisgt: i.op = Ocsgtl; break;
		case Cisle: i.op = Ocslel; break;
		case Cisge: i.op = Ocsgel; break;
		case Ciult: i.op = Ocultl; break;
		case Ciugt: i.op = Ocugtl; break;
		case Ciule: i.op = Oculel; break;
		case Ciuge: i.op = Ocugel; break;
		default:
			die("unsupported comparison %d", cmp);
		}
	} else {
		switch (cmp) {
		case Cieq:  i.op = Oceqw; break;
		case Cine:  i.op = Ocnew; break;
		case Cislt: i.op = Ocsltw; break;
		case Cisgt: i.op = Ocsgtw; break;
		case Cisle: i.op = Ocslew; break;
		case Cisge: i.op = Ocsgew; break;
		case Ciult: i.op = Ocultw; break;
		case Ciugt: i.op = Ocugtw; break;
		case Ciule: i.op = Oculew; break;
		case Ciuge: i.op = Ocugew; break;
		default:
			die("unsupported comparison %d", cmp);
		}
	}

	emiti(i);
	if (!req(hoist_dst, R))
		emit(Ocopy, k, hoist_dst, hoist_src, R);
	i0 = curi;
	fixarg(&i0->arg[0], k, i0, fn);
	fixarg(&i0->arg[1], k, i0, fn);
	/* Hint the register allocator to avoid SI/DI/BP/SP/ES/DS for the
	 * result.  setCC needs an 8-bit-capable register and these have none.
	 * hint.m is consulted only under register pressure (after exhausting
	 * the per-temp hint.r preference), so this is a soft preference, not
	 * a hard constraint.  When rega ignores it the emit phase falls back
	 * to a setCC al + movzx <dst>, al sequence (correct codegen, but it
	 * clobbers AL silently). */
	if (rtype(i0->to) == RTmp && i0->to.val >= Tmp0) {
		fn->tmp[i0->to.val].hint.m
		    |= BIT(RSI) | BIT(RDI) | BIT(RBP) | BIT(RSP)
		    |  BIT(RES) | BIT(RDS);
	}
}

static void
seljmp(Blk *b, Fn *fn)
{
	Ref r;

	if (b->jmp.type == Jjnz) {
		/* test reg, reg; jnz label */
		r = b->jmp.arg;
		fixarg(&r, Kw, 0, fn);
		b->jmp.arg = r;
	}
	/* Other jump types are handled in emit phase */
}

static void
seldiv(Ins i, Fn *fn, int issigned)
{
	Ins *i0;

	/* x86 division is special:
	 * - Dividend is in DX:AX (32-bit)
	 * - Divisor is the operand
	 * - Quotient goes to AX, remainder to DX
	 *
	 * For signed division (idiv):
	 *   mov ax, dividend
	 *   cwd              ; sign-extend AX into DX:AX
	 *   idiv divisor
	 *
	 * For unsigned division (div):
	 *   mov ax, dividend
	 *   xor dx, dx       ; zero-extend into DX:AX
	 *   div divisor
	 *
	 * The emit phase will handle this specially.
	 */

	emiti(i);
	i0 = curi;
	fixarg(&i0->arg[0], Kw, i0, fn);
	fixarg(&i0->arg[1], Kw, i0, fn);
}

static void
selshift(Ins i, Fn *fn)
{
	Ins *i0;

	/* x86 shift instructions are special:
	 * - Shift count can be immediate: shl ax, 5
	 * - Or must be in CL register: shl ax, cl
	 *
	 * The emit phase will handle moving the count to CL if needed.
	 */

	emiti(i);
	i0 = curi;
	fixarg(&i0->arg[0], Kw, i0, fn);
	fixarg(&i0->arg[1], Kw, i0, fn);
}

static void
selfp(Ins i, Fn *fn)
{
	Ins *i0;

	/* For 8087 FPU operations, we use stack-based instructions
	 * The emit phase will handle the actual FP instruction emission
	 * We just need to ensure arguments are properly handled
	 */
	emiti(i);
	i0 = curi;
	fixarg(&i0->arg[0], argcls(&i, 0), i0, fn);
	fixarg(&i0->arg[1], argcls(&i, 1), i0, fn);
}

static void
sel(Ins i, Fn *fn)
{
	Ins *i0;
	int ck, cc;

	/* Handle comparisons specially */
	if (iscmp(i.op, &ck, &cc)) {
		selcmp(i, ck, cc, fn);
		return;
	}

	/* Handle floating-point operations */
	if (i.cls == Ks || i.cls == Kd) {
		switch (i.op) {
		case Oadd:
		case Osub:
		case Omul:
		case Odiv:
		case Oneg:
		case Oload:
		case Ostores:
		case Ostored:
		case Ocopy:   /* FP copy/move */
		case Otruncd: /* Double to single conversion */
		case Oexts:   /* Single to double conversion */
		case Oswtof:  /* Signed word to float */
		case Ouwtof:  /* Unsigned word to float */
			selfp(i, fn);
			return;
		}
	}

	/* Handle float to int conversions (these produce int results but take FP inputs) */
	switch (i.op) {
	case Ostosi:  /* Float to signed int */
	case Ostoui:  /* Float to unsigned int */
	case Odtosi:  /* Double to signed int */
	case Odtoui:  /* Double to unsigned int */
		selfp(i, fn);
		return;
	}

	/* Handle division and remainder specially */
	switch (i.op) {
	case Odiv:
	case Orem:
		if (i.cls != Ks && i.cls != Kd) {
			seldiv(i, fn, 1); /* signed */
			return;
		}
		break;
	case Oudiv:
	case Ourem:
		seldiv(i, fn, 0); /* unsigned */
		return;
	/* Handle shift operations specially */
	case Oshl:
	case Oshr:
	case Osar:
		selshift(i, fn);
		return;
	}

	/* Emit the instruction first, then fix args
	 * This follows the pattern from rv64/isel.c
	 */
	if (i.op != Onop) {
		emiti(i);
		i0 = curi; /* fixarg() can change curi */
		fixarg(&i0->arg[0], argcls(&i, 0), i0, fn);
		fixarg(&i0->arg[1], argcls(&i, 1), i0, fn);
	}
}

void
i8086_isel(Fn *fn)
{
	Blk *b, **sb;
	Ins *i;
	Phi *p;
	uint n;
	int al;
	int64_t sz;

	/* Assign slots to "fast" allocs — constant-size Oalloc4/8/16 in
	 * the entry block.  Mirrors amd64/isel.c, but slots are 2 bytes
	 * here (vs. 4 on amd64), so divide by 2 instead of 4.  After this
	 * pass the alloc instruction is a Onop and the result temp's
	 * `slot` field holds its slot index; fixarg() then turns each use
	 * into an explicit `lea reg, [bp+offset]` (Oaddr).  Without this,
	 * Oalloc4/8/16 with class Kl falls through to the unhandled-32-bit
	 * arm of emitins() and produces a "TODO: 32-bit op" comment, so
	 * the alloc'd region is never addressable and the result temp
	 * holds garbage. */
	b = fn->start;
	for (al = Oalloc, n = 4; al <= Oalloc1; al++, n *= 2)
		for (i = b->ins; i < &b->ins[b->nins]; i++)
			if (i->op == al) {
				if (rtype(i->arg[0]) != RCon)
					break;
				sz = fn->con[i->arg[0].val].bits.i;
				if (sz < 0 || sz >= INT_MAX-15)
					err("invalid alloc size %"PRId64, sz);
				sz = (sz + n-1) & -n;
				sz /= 2;  /* 2-byte slots on i8086 */
				if (sz > INT_MAX - fn->slot)
					die("alloc too large");
				fn->tmp[i->to.val].slot = fn->slot;
				fn->slot += sz;
				fn->salign = 2 + al - Oalloc;
				*i = (Ins){.op = Onop};
			}

	/* Process blocks in forward order */
	for (b = fn->start; b; b = b->link) {
		/* Reset instruction buffer for this block */
		curi = &insb[NIns];

		/* Process phi nodes */
		for (sb=(Blk*[3]){b->s1, b->s2, 0}; *sb; sb++)
			for (p=(*sb)->phi; p; p=p->link) {
				for (n=0; p->blk[n] != b; n++)
					assert(n+1 < p->narg);
				fixarg(&p->arg[n], p->cls, 0, fn);
			}

		/* Process jump instruction */
		seljmp(b, fn);

		/* Process regular instructions in reverse */
		for (i = &b->ins[b->nins]; i != b->ins;)
			sel(*--i, fn);

		/* Copy instructions to block */
		idup(b, curi, &insb[NIns]-curi);
	}

	if (debug['I']) {
		fprintf(stderr, "\n> After instruction selection:\n");
		printfn(fn, stderr);
	}
}
