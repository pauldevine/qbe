#include "all.h"

/* Assembly code emission for 8086/286/386 16-bit mode */

enum {
	Ki = -1, /* matches Kw and Kl */
	Ka = -2, /* matches all classes */
};

/* Instruction format table
 * Maps QBE operations to x86 assembly mnemonics
 */
static struct {
	short op;
	short cls;
	char *fmt;
} omap[] = {
	/* Arithmetic */
	{ Oadd,    Ki, "add %=, %1" },
	{ Osub,    Ki, "sub %=, %1" },
	{ Omul,    Ki, "imul %=, %1" },
	{ Odiv,    Ki, "idiv %1" },
	{ Oudiv,   Ki, "div %1" },
	{ Orem,    Ki, "idiv %1" },  /* remainder in DX */
	{ Ourem,   Ki, "div %1" },   /* remainder in DX */

	/* Bitwise */
	{ Oand,    Ki, "and %=, %1" },
	{ Oor,     Ki, "or %=, %1" },
	{ Oxor,    Ki, "xor %=, %1" },
	{ Oshl,    Ki, "shl %=, %1" },
	{ Oshr,    Ki, "shr %=, %1" },
	{ Osar,    Ki, "sar %=, %1" },

	/* Memory operations */
	{ Ostoreb, Kw, "mov byte ptr %M1, %0" },
	{ Ostoreh, Kw, "mov word ptr %M1, %0" },
	{ Ostorew, Kw, "mov word ptr %M1, %0" },
	{ Ostorel, Kw, "mov dword ptr %M1, %0" },

	{ Oloadsb, Ki, "movsx %=, byte ptr %M0" },
	{ Oloadub, Ki, "movzx %=, byte ptr %M0" },
	{ Oloadsh, Ki, "movsx %=, word ptr %M0" },
	{ Oloaduh, Ki, "movzx %=, word ptr %M0" },
	{ Oloadsw, Ki, "mov %=, word ptr %M0" },
	{ Oloaduw, Ki, "mov %=, word ptr %M0" },
	{ Oload,   Kw, "mov %=, word ptr %M0" },
	{ Oload,   Kl, "mov %=, dword ptr %M0" },

	/* Data movement */
	{ Ocopy,   Ki, "mov %=, %0" },
	{ Oswap,   Ki, "xchg %=, %0" },
	{ Oaddr,   Ki, "lea %=, %M0" },

	/* Comparisons - special handling needed for setCC (8-bit only) */
	{ Oceqw,   Kw, "cmp %0, %1\n\tsete %B=\n\tmovzx %=, %B=" },
	{ Ocnew,   Kw, "cmp %0, %1\n\tsetne %B=\n\tmovzx %=, %B=" },
	{ Ocsltw,  Kw, "cmp %0, %1\n\tsetl %B=\n\tmovzx %=, %B=" },
	{ Ocsgtw,  Kw, "cmp %0, %1\n\tsetg %B=\n\tmovzx %=, %B=" },
	{ Ocslew,  Kw, "cmp %0, %1\n\tsetle %B=\n\tmovzx %=, %B=" },
	{ Ocsgew,  Kw, "cmp %0, %1\n\tsetge %B=\n\tmovzx %=, %B=" },
	{ Ocultw,  Kw, "cmp %0, %1\n\tsetb %B=\n\tmovzx %=, %B=" },
	{ Ocugtw,  Kw, "cmp %0, %1\n\tseta %B=\n\tmovzx %=, %B=" },
	{ Oculew,  Kw, "cmp %0, %1\n\tsetbe %B=\n\tmovzx %=, %B=" },
	{ Ocugew,  Kw, "cmp %0, %1\n\tsetae %B=\n\tmovzx %=, %B=" },

	/* Control flow */
	{ Ocall,   Kw, "call %0" },
	{ Osalloc, Kw, "sub sp, %0" },

	/* 8087 FPU operations - Single precision (float - 32-bit) */
	{ Oload,   Ks, "fld dword %M0" },      /* Load float */
	{ Ostores, Ks, "fstp dword %M1" },     /* Store float and pop */
	{ Oadd,    Ks, "faddp" },              /* ST(0) += ST(1), pop */
	{ Osub,    Ks, "fsubp" },              /* ST(0) -= ST(1), pop */
	{ Omul,    Ks, "fmulp" },              /* ST(0) *= ST(1), pop */
	{ Odiv,    Ks, "fdivp" },              /* ST(0) /= ST(1), pop */
	{ Oneg,    Ks, "fchs" },               /* ST(0) = -ST(0) */

	/* 8087 FPU operations - Double precision (double - 64-bit) */
	{ Oload,   Kd, "fld qword %M0" },      /* Load double */
	{ Ostored, Kd, "fstp qword %M1" },     /* Store double and pop */
	{ Oadd,    Kd, "faddp" },              /* ST(0) += ST(1), pop */
	{ Osub,    Kd, "fsubp" },              /* ST(0) -= ST(1), pop */
	{ Omul,    Kd, "fmulp" },              /* ST(0) *= ST(1), pop */
	{ Odiv,    Kd, "fdivp" },              /* ST(0) /= ST(1), pop */
	{ Oneg,    Kd, "fchs" },               /* ST(0) = -ST(0) */

	/* 8087 FPU comparisons */
	{ Oceqs,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tsete %B=\n\tmovzx %=, %B=" },
	{ Ocges,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tsetae %B=\n\tmovzx %=, %B=" },
	{ Ocgts,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tseta %B=\n\tmovzx %=, %B=" },
	{ Ocles,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tsetbe %B=\n\tmovzx %=, %B=" },
	{ Oclts,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tsetb %B=\n\tmovzx %=, %B=" },
	{ Ocnes,   Ks, "fcompp\n\tfstsw ax\n\tsahf\n\tsetne %B=\n\tmovzx %=, %B=" },

	{ Oceqd,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tsete %B=\n\tmovzx %=, %B=" },
	{ Ocged,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tsetae %B=\n\tmovzx %=, %B=" },
	{ Ocgtd,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tseta %B=\n\tmovzx %=, %B=" },
	{ Ocled,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tsetbe %B=\n\tmovzx %=, %B=" },
	{ Ocltd,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tsetb %B=\n\tmovzx %=, %B=" },
	{ Ocned,   Kd, "fcompp\n\tfstsw ax\n\tsahf\n\tsetne %B=\n\tmovzx %=, %B=" },

	/* 8087 type conversions */
	/* Note: Conversions will be handled in isel.c through load/store operations */

	{ NOp, 0, 0 }
};

/* Register names for 16-bit x86 */
static char *rname[] = {
	[RAX] = "ax",
	[RCX] = "cx",
	[RDX] = "dx",
	[RBX] = "bx",
	[RSI] = "si",
	[RDI] = "di",
	[RBP] = "bp",
	[RSP] = "sp",
};

/* 8-bit register names (low byte) */
static char *rname8[] = {
	[RAX] = "al",
	[RCX] = "cl",
	[RDX] = "dl",
	[RBX] = "bl",
};

static int64_t
slot(Ref r, Fn *fn)
{
	int s;

	s = rsval(r);
	assert(s <= fn->slot);
	/* Stack grows down, slots are 2 bytes for 16-bit */
	if (s < 0)
		return 2 * -s;
	else
		return -2 * (fn->slot - s);
}

static void
emitaddr(Con *c, FILE *f)
{
	assert(c->sym.type == SGlo || c->sym.type == SThr);
	fputs(str(c->sym.id), f);
	if (c->bits.i)
		fprintf(f, "+%"PRIi64, c->bits.i);
}

static void
emitf(char *s, Ins *i, Fn *fn, FILE *f)
{
	Ref r;
	int k, c;
	Con *pc;
	int64_t offset;

	fputc('\t', f);
	for (;;) {
		c = *s++;
		if (!c) {
			fputc('\n', f);
			break;
		}
		if (c != '%') {
			fputc(c, f);
			continue;
		}

		switch ((c = *s++)) {
		case 'B': /* 8-bit register version of next ref */
			c = *s++;
			if (c == '=')
				r = i->to;
			else if (c == '0')
				r = i->arg[0];
			else if (c == '1')
				r = i->arg[1];
			else
				die("invalid 8-bit register specifier");

			if (rtype(r) == RTmp && r.val <= RBX)
				fprintf(f, "%s", rname8[r.val]);
			else
				die("8-bit register only available for AX-BX");
			break;
		case '=': /* destination register */
			r = i->to;
			goto Ref;
		case '0': /* first argument */
			r = i->arg[0];
			goto Ref;
		case '1': /* second argument */
			r = i->arg[1];
			goto Ref;
		Ref:
			/* Handle empty reference (R) */
			if (req(r, R)) {
				/* Empty reference - don't emit anything */
				break;
			}
			switch (rtype(r)) {
			case RTmp:
				fprintf(f, "%s", rname[r.val]);
				break;
			case RCon:
				pc = &fn->con[r.val];
				switch (pc->type) {
				case CBits:
					fprintf(f, "%"PRIi64, pc->bits.i);
					break;
				case CAddr:
					emitaddr(pc, f);
					break;
				default:
					die("invalid constant type");
				}
				break;
			case RSlot:
				offset = slot(r, fn);
				fprintf(f, "[bp%+ld]", (long)offset);
				break;
			case RMem: {
				/* Memory reference used as operand - emit as memory location */
				Mem *m = &fn->mem[r.val];
				int has_offset = (m->offset.type != CUndef);
				int has_base = !req(m->base, R);
				int has_index = !req(m->index, R);

				fprintf(f, "word ptr [");

				/* Emit base register if present */
				if (has_base) {
					if (rtype(m->base) == RTmp)
						fprintf(f, "%s", rname[m->base.val]);
					else if (rtype(m->base) == RSlot) {
						fprintf(f, "bp%+ld", (long)slot(m->base, fn));
					}
				}

				/* Emit index register if present */
				if (has_index) {
					if (has_base)
						fprintf(f, " + ");
					if (rtype(m->index) == RTmp)
						fprintf(f, "%s", rname[m->index.val]);
					/* i8086 doesn't support scale > 1 */
					if (m->scale != 1 && m->scale != 0)
						die("i8086 only supports scale of 1");
				}

				/* Emit offset if present */
				if (has_offset) {
					if (has_base || has_index)
						fprintf(f, " + ");
					if (m->offset.type == CAddr) {
						emitaddr(&m->offset, f);
					} else if (m->offset.type == CBits) {
						fprintf(f, "%"PRIi64, m->offset.bits.i);
					}
				}

				fputc(']', f);
				break;
			}
			default:
				fprintf(stderr, "Invalid reference type: %d (RTmp=%d, RCon=%d, RSlot=%d, RMem=%d)\n",
					rtype(r), RTmp, RCon, RSlot, RMem);
				die("invalid reference type");
			}
			break;
		case 'M': /* memory operand */
			c = *s++;
			if (c == '0')
				r = i->arg[0];
			else if (c == '1')
				r = i->arg[1];
			else
				die("invalid memory operand");

			/* Handle different reference types for memory operands */
			switch (rtype(r)) {
			case RMem: {
				/* Complex addressing mode: [base + index + offset] */
				Mem *m = &fn->mem[r.val];
				int has_offset = (m->offset.type != CUndef);
				int has_base = !req(m->base, R);
				int has_index = !req(m->index, R);

				fputc('[', f);

				/* Emit base register if present */
				if (has_base) {
					if (rtype(m->base) == RTmp)
						fprintf(f, "%s", rname[m->base.val]);
					else if (rtype(m->base) == RSlot) {
						fprintf(f, "bp%+ld", (long)slot(m->base, fn));
					}
				}

				/* Emit index register if present */
				if (has_index) {
					if (has_base)
						fprintf(f, " + ");
					if (rtype(m->index) == RTmp)
						fprintf(f, "%s", rname[m->index.val]);
					/* i8086 doesn't support scale > 1 */
					if (m->scale != 1 && m->scale != 0)
						die("i8086 only supports scale of 1");
				}

				/* Emit offset if present */
				if (has_offset) {
					if (has_base || has_index)
						fprintf(f, " + ");
					if (m->offset.type == CAddr) {
						emitaddr(&m->offset, f);
					} else if (m->offset.type == CBits) {
						fprintf(f, "%"PRIi64, m->offset.bits.i);
					}
				}

				fputc(']', f);
				break;
			}
			case RCon:
				pc = &fn->con[r.val];
				if (pc->type == CAddr) {
					fputc('[', f);
					emitaddr(pc, f);
					fputc(']', f);
				} else {
					fprintf(f, "%"PRIi64, pc->bits.i);
				}
				break;
			case RTmp:
				fprintf(f, "[%s]", rname[r.val]);
				break;
			case RSlot:
				offset = slot(r, fn);
				fprintf(f, "[bp%+ld]", (long)offset);
				break;
			default:
				die("invalid memory reference type");
			}
			break;
		default:
			die("invalid format specifier %%%c", c);
		}
	}
}

static void
loadaddr(Con *c, char *rn, FILE *f)
{
	fprintf(f, "\tlea %s, ", rn);
	emitaddr(c, f);
	fputc('\n', f);
}

static void
emitins(Ins *i, Fn *fn, FILE *f)
{
	int o;
	char *fmt;
	Ref r0, r1;
	char *shiftop;

	/* Special handling for shift operations */
	if (i->op == Oshl || i->op == Oshr || i->op == Osar) {
		/* Determine shift operation mnemonic */
		shiftop = (i->op == Oshl) ? "shl" :
		          (i->op == Oshr) ? "shr" : "sar";

		r0 = i->arg[0]; /* value to shift */
		r1 = i->arg[1]; /* shift count */

		/* If shift count is a register (not CX) and not immediate, move to CX */
		if (rtype(r1) == RTmp && r1.val != RCX) {
			fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
		} else if (rtype(r1) == RSlot) {
			/* Load from stack slot into CX */
			fprintf(f, "\tmov cx, [bp%+ld]\n", (long)slot(r1, fn));
		}

		/* Move value to destination if needed (before shift) */
		if (rtype(i->to) == RTmp && i->to.val != r0.val && rtype(r0) == RTmp) {
			fprintf(f, "\tmov %s, %s\n", rname[i->to.val], rname[r0.val]);
			r0 = i->to; /* Now shift the destination */
		}

		/* Emit the shift operation */
		fprintf(f, "\t%s ", shiftop);

		/* Emit destination/source */
		if (rtype(r0) == RTmp)
			fprintf(f, "%s", rname[r0.val]);
		else if (rtype(i->to) == RTmp)
			fprintf(f, "%s", rname[i->to.val]);
		else
			fprintf(f, "?");

		fprintf(f, ", ");

		/* Emit shift count - use CL for register, immediate for constant */
		if (rtype(r1) == RCon) {
			/* Immediate shift count */
			fprintf(f, "%"PRIi64, fn->con[r1.val].bits.i);
		} else {
			/* Shift count in CL (we moved it there if needed) */
			fprintf(f, "cl");
		}

		fprintf(f, "\n");

		return;
	}

	/* Special handling for division and remainder */
	if (i->op == Odiv || i->op == Orem) {
		/* Signed division/remainder
		 * mov ax, dividend
		 * cwd              ; sign-extend AX into DX:AX
		 * idiv divisor
		 * ; quotient in AX, remainder in DX
		 */
		r0 = i->arg[0]; /* dividend */
		r1 = i->arg[1]; /* divisor */

		/* Move dividend to AX if not already there */
		if (rtype(r0) != RTmp || r0.val != RAX) {
			fprintf(f, "\tmov ax, ");
			if (rtype(r0) == RTmp)
				fprintf(f, "%s\n", rname[r0.val]);
			else if (rtype(r0) == RCon)
				fprintf(f, "%"PRIi64"\n", fn->con[r0.val].bits.i);
			else
				fprintf(f, "?\n");
		}

		/* Sign-extend AX into DX:AX */
		fprintf(f, "\tcwd\n");

		/* Perform signed division */
		fprintf(f, "\tidiv ");
		if (rtype(r1) == RTmp)
			fprintf(f, "%s\n", rname[r1.val]);
		else if (rtype(r1) == RCon)
			fprintf(f, "%"PRIi64"\n", fn->con[r1.val].bits.i);
		else
			fprintf(f, "?\n");

		/* Move result to destination */
		if (i->op == Odiv) {
			/* Quotient is in AX */
			if (rtype(i->to) == RTmp && i->to.val != RAX)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		} else {
			/* Remainder is in DX */
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, dx\n", rname[i->to.val]);
		}
		return;
	}

	if (i->op == Oudiv || i->op == Ourem) {
		/* Unsigned division/remainder
		 * mov ax, dividend
		 * xor dx, dx       ; zero-extend into DX:AX
		 * div divisor
		 * ; quotient in AX, remainder in DX
		 */
		r0 = i->arg[0]; /* dividend */
		r1 = i->arg[1]; /* divisor */

		/* Move dividend to AX if not already there */
		if (rtype(r0) != RTmp || r0.val != RAX) {
			fprintf(f, "\tmov ax, ");
			if (rtype(r0) == RTmp)
				fprintf(f, "%s\n", rname[r0.val]);
			else if (rtype(r0) == RCon)
				fprintf(f, "%"PRIi64"\n", fn->con[r0.val].bits.i);
			else
				fprintf(f, "?\n");
		}

		/* Zero-extend into DX:AX */
		fprintf(f, "\txor dx, dx\n");

		/* Perform unsigned division */
		fprintf(f, "\tdiv ");
		if (rtype(r1) == RTmp)
			fprintf(f, "%s\n", rname[r1.val]);
		else if (rtype(r1) == RCon)
			fprintf(f, "%"PRIi64"\n", fn->con[r1.val].bits.i);
		else
			fprintf(f, "?\n");

		/* Move result to destination */
		if (i->op == Oudiv) {
			/* Quotient is in AX */
			if (rtype(i->to) == RTmp && i->to.val != RAX)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		} else {
			/* Remainder is in DX */
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, dx\n", rname[i->to.val]);
		}
		return;
	}

	/* Special handling for Osalloc (stack allocation) */
	if (i->op == Osalloc) {
		Con *c;
		int64_t val;

		/* Get the allocation size from arg[0] */
		if (rtype(i->arg[0]) != RCon)
			die("Osalloc requires constant argument");

		c = &fn->con[i->arg[0].val];
		val = c->bits.i;

		if (val < 0) {
			/* Negative value = deallocate (add to sp) */
			fprintf(f, "\tadd sp, %"PRId64"\n", -val);
		} else {
			/* Positive value = allocate (sub from sp) */
			fprintf(f, "\tsub sp, %"PRId64"\n", val);
		}

		/* If destination is not R, copy SP to destination */
		if (!req(i->to, R) && rtype(i->to) == RTmp) {
			fprintf(f, "\tmov %s, sp\n", rname[i->to.val]);
		}

		return;
	}

	/* Find the appropriate format string */
	for (o = 0; omap[o].op != NOp; o++) {
		if (omap[o].op == i->op) {
			if (omap[o].cls == i->cls ||
			    omap[o].cls == Ka ||
			    (omap[o].cls == Ki && (i->cls == Kw || i->cls == Kl)))
				break;
		}
	}

	if (omap[o].op == NOp) {
		/* Instruction not in table */
		fprintf(f, "\t; TODO: op %d cls %d\n", i->op, i->cls);
		return;
	}

	fmt = omap[o].fmt;
	emitf(fmt, i, fn, f);
}

void
i8086_emitfn(Fn *fn, FILE *f)
{
	Blk *b;
	Ins *i;

	/* Function header */
	fprintf(f, "\n");
	emitfnlnk(fn->name, &fn->lnk, f);
	fprintf(f, "%s:\n", fn->name);

	/* Function prologue */
	fprintf(f, "\tpush bp\n");
	fprintf(f, "\tmov bp, sp\n");
	if (fn->slot > 0)
		fprintf(f, "\tsub sp, %d\n", 2 * fn->slot);

	/* Emit blocks */
	for (b = fn->start; b; b = b->link) {
		if (b != fn->start)
			fprintf(f, "%s:\n", b->name);

		for (i = b->ins; i < &b->ins[b->nins]; i++)
			emitins(i, fn, f);

		/* Emit jump */
		switch (b->jmp.type) {
		case Jret0:
		case Jretw:
		case Jretl:
			fprintf(f, "\tmov sp, bp\n");
			fprintf(f, "\tpop bp\n");
			fprintf(f, "\tret\n");
			break;
		case Jjmp:
			if (b->s1 != b->link)
				fprintf(f, "\tjmp %s\n", b->s1->name);
			break;
		case Jjnz:
			fprintf(f, "\ttest %s, %s\n", rname[b->jmp.arg.val], rname[b->jmp.arg.val]);
			fprintf(f, "\tjnz %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		/* Conditional jumps based on flags (from comparison) */
		case Jjfieq:
			fprintf(f, "\tje %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfine:
			fprintf(f, "\tjne %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfislt:
			fprintf(f, "\tjl %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisgt:
			fprintf(f, "\tjg %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisle:
			fprintf(f, "\tjle %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisge:
			fprintf(f, "\tjge %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiult:
			fprintf(f, "\tjb %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiugt:
			fprintf(f, "\tja %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiule:
			fprintf(f, "\tjbe %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiuge:
			fprintf(f, "\tjae %s\n", b->s1->name);
			if (b->s2 != b->link)
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		default:
			/* Unsupported jump type */
			fprintf(f, "\t; TODO: jump type %d\n", b->jmp.type);
			break;
		}
	}
}
