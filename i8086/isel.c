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
			/* Stack slot addressing */
			r1 = SLOT(s);
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
	 * We emit the comparison operation with the QBE cmp opcode,
	 * and the emit phase will translate it to cmp + setCC
	 */

	/* Map QBE comparison to x86 comparison operation */
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
		/* Unsupported comparison */
		die("unsupported comparison %d", cmp);
	}

	emiti(i);
	i0 = curi;
	fixarg(&i0->arg[0], k, i0, fn);
	fixarg(&i0->arg[1], k, i0, fn);
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
