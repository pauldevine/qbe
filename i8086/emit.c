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

	/* Stack allocation for locals - class 0 (untyped) */
	{ Oalloc4,  0, "; alloc4 (stack slot allocated in prologue)" },
	{ Oalloc8,  0, "; alloc8 (stack slot allocated in prologue)" },
	{ Oalloc16, 0, "; alloc16 (stack slot allocated in prologue)" },

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

	/* 8087 FPU type conversions and copy */
	{ Ocopy,   Ks, "; fp copy (nop - already on FP stack)" },  /* FP copy is a nop for stack */
	{ Ocopy,   Kd, "; fp copy (nop - already on FP stack)" },
	{ Otruncd,  Ks, "; truncd: double to float (handled by load/store size)" },
	{ Oexts,   Kd, "; exts: float to double (handled by load/store size)" },

	/* 8087 int to float conversions */
	{ Oswtof,  Ks, "fild word %M0" },     /* Load signed word, convert to float */
	{ Oswtof,  Kd, "fild word %M0" },     /* Load signed word, convert to double */
	{ Ouwtof,  Ks, "fild word %M0" },     /* Load unsigned word, convert to float */
	{ Ouwtof,  Kd, "fild word %M0" },     /* Load unsigned word, convert to double */

	/* 8087 float to int conversions */
	{ Ostosi,  Kw, "fistp word %M1" },    /* Convert float to signed int, store and pop */
	{ Ostoui,  Kw, "fistp word %M1" },    /* Convert float to unsigned int, store and pop */
	{ Odtosi,  Kw, "fistp word %M1" },    /* Convert double to signed int, store and pop */
	{ Odtoui,  Kw, "fistp word %M1" },    /* Convert double to unsigned int, store and pop */

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

	/* Special handling for 32-bit (Kl) operations on 16-bit hardware */
	if (i->cls == Kl) {
		/*
		 * 32-bit operations on 16-bit x86 require multi-instruction sequences.
		 * 32-bit values are stored as two consecutive 16-bit words in memory
		 * (low word first, little-endian).
		 *
		 * For operations, we use register pairs:
		 * - DX:AX for the result (high:low)
		 * - Memory operands for source/destination
		 */
		r0 = i->arg[0];
		r1 = i->arg[1];

		switch (i->op) {
		case Oadd:
			/*
			 * 32-bit addition: dest = src0 + src1
			 * add low words, then adc high words
			 */
			/* Load src0 low word to AX */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				/* Register - assume it's actually a slot reference */
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
				fprintf(f, "\txor dx, dx\n");  /* Extend to 32-bit */
			}

			/* Add src1 */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tadd ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tadc dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tadd ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tadc dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r1) == RTmp) {
				fprintf(f, "\tadd ax, %s\n", rname[r1.val]);
				fprintf(f, "\tadc dx, 0\n");
			}

			/* Store result to destination */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				/* For register destination, we can only store low word */
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Osub:
			/*
			 * 32-bit subtraction: dest = src0 - src1
			 * sub low words, then sbb high words
			 */
			/* Load src0 to DX:AX */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
				fprintf(f, "\txor dx, dx\n");
			}

			/* Subtract src1 */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tsub ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tsbb dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tsub ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tsbb dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r1) == RTmp) {
				fprintf(f, "\tsub ax, %s\n", rname[r1.val]);
				fprintf(f, "\tsbb dx, 0\n");
			}

			/* Store result */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Omul:
			/*
			 * 32-bit multiplication: dest = src0 * src1
			 * For simplicity, use 16x16->32 multiplication when possible
			 * Full 32x32 multiplication requires more complex code
			 */
			/* Load src0 low word to AX */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
			}

			/* Multiply by src1 low word (result in DX:AX) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\timul word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tmov bx, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\timul bx\n");
			} else if (rtype(r1) == RTmp) {
				fprintf(f, "\timul %s\n", rname[r1.val]);
			}

			/* Store result */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oand:
			/*
			 * 32-bit bitwise AND: dest = src0 & src1
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tand ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tand dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tand ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tand dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oor:
			/*
			 * 32-bit bitwise OR: dest = src0 | src1
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tor ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tor dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tor ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tor dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oxor:
			/*
			 * 32-bit bitwise XOR: dest = src0 ^ src1
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\txor ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\txor dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\txor ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\txor dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oshl:
			/*
			 * 32-bit left shift
			 * For shifts by constant < 16, we can use shld/shl
			 * For larger shifts, more complex handling needed
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: low word becomes 0, high = low << (n-16) */
					fprintf(f, "\tmov dx, ax\n");
					fprintf(f, "\txor ax, ax\n");
					if (shift > 16) {
						fprintf(f, "\tshl dx, %d\n", shift - 16);
					}
				} else if (shift > 0) {
					/* Use loop for shift */
					fprintf(f, "\tmov cx, %d\n", shift);
					fprintf(f, ".L_shl32_%p:\n", (void*)i);
					fprintf(f, "\tshl ax, 1\n");
					fprintf(f, "\trcl dx, 1\n");
					fprintf(f, "\tloop .L_shl32_%p\n", (void*)i);
				}
			} else {
				/* Variable shift count - use loop */
				if (rtype(r1) == RTmp)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_shl32_done_%p\n", (void*)i);
				fprintf(f, ".L_shl32_%p:\n", (void*)i);
				fprintf(f, "\tshl ax, 1\n");
				fprintf(f, "\trcl dx, 1\n");
				fprintf(f, "\tloop .L_shl32_%p\n", (void*)i);
				fprintf(f, ".L_shl32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oshr:
			/*
			 * 32-bit logical right shift (unsigned)
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: high word becomes 0, low = high >> (n-16) */
					fprintf(f, "\tmov ax, dx\n");
					fprintf(f, "\txor dx, dx\n");
					if (shift > 16) {
						fprintf(f, "\tshr ax, %d\n", shift - 16);
					}
				} else if (shift > 0) {
					fprintf(f, "\tmov cx, %d\n", shift);
					fprintf(f, ".L_shr32_%p:\n", (void*)i);
					fprintf(f, "\tshr dx, 1\n");
					fprintf(f, "\trcr ax, 1\n");
					fprintf(f, "\tloop .L_shr32_%p\n", (void*)i);
				}
			} else {
				if (rtype(r1) == RTmp)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_shr32_done_%p\n", (void*)i);
				fprintf(f, ".L_shr32_%p:\n", (void*)i);
				fprintf(f, "\tshr dx, 1\n");
				fprintf(f, "\trcr ax, 1\n");
				fprintf(f, "\tloop .L_shr32_%p\n", (void*)i);
				fprintf(f, ".L_shr32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Osar:
			/*
			 * 32-bit arithmetic right shift (signed)
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: sign-extend high word, low = high >> (n-16) */
					fprintf(f, "\tmov ax, dx\n");
					fprintf(f, "\tsar dx, 15\n");  /* Sign-extend */
					if (shift > 16) {
						fprintf(f, "\tsar ax, %d\n", shift - 16);
					}
				} else if (shift > 0) {
					fprintf(f, "\tmov cx, %d\n", shift);
					fprintf(f, ".L_sar32_%p:\n", (void*)i);
					fprintf(f, "\tsar dx, 1\n");
					fprintf(f, "\trcr ax, 1\n");
					fprintf(f, "\tloop .L_sar32_%p\n", (void*)i);
				}
			} else {
				if (rtype(r1) == RTmp)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word ptr [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_sar32_done_%p\n", (void*)i);
				fprintf(f, ".L_sar32_%p:\n", (void*)i);
				fprintf(f, "\tsar dx, 1\n");
				fprintf(f, "\trcr ax, 1\n");
				fprintf(f, "\tloop .L_sar32_%p\n", (void*)i);
				fprintf(f, ".L_sar32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Ocopy:
			/*
			 * 32-bit copy
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				/* Register to register - just copy low word, extend high */
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
				fprintf(f, "\tcwd\n");  /* Sign-extend AX to DX:AX */
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Oload:
			/*
			 * 32-bit load from memory
			 */
			/* Memory address is in arg[0] */
			if (rtype(r0) == RSlot) {
				/* Load from local variable (stack slot that contains a 32-bit value) */
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RTmp) {
				/* Load from address in register */
				fprintf(f, "\tmov bx, %s\n", rname[r0.val]);
				fprintf(f, "\tmov ax, word ptr [bx]\n");
				fprintf(f, "\tmov dx, word ptr [bx+2]\n");
			} else if (rtype(r0) == RMem) {
				/* Complex addressing mode */
				Mem *m = &fn->mem[r0.val];
				if (!req(m->base, R) && rtype(m->base) == RTmp) {
					fprintf(f, "\tmov bx, %s\n", rname[m->base.val]);
					if (m->offset.type == CBits) {
						fprintf(f, "\tmov ax, word ptr [bx+%"PRIi64"]\n", m->offset.bits.i);
						fprintf(f, "\tmov dx, word ptr [bx+%"PRIi64"]\n", m->offset.bits.i + 2);
					} else {
						fprintf(f, "\tmov ax, word ptr [bx]\n");
						fprintf(f, "\tmov dx, word ptr [bx+2]\n");
					}
				}
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}
			return;

		case Ostorel:
			/*
			 * 32-bit store to memory
			 * arg[0] = value to store, arg[1] = destination address
			 */
			/* Load value to store */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
				fprintf(f, "\tcwd\n");
			}

			/* Store to destination */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(r1, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], dx\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RTmp) {
				fprintf(f, "\tmov bx, %s\n", rname[r1.val]);
				fprintf(f, "\tmov word ptr [bx], ax\n");
				fprintf(f, "\tmov word ptr [bx+2], dx\n");
			} else if (rtype(r1) == RMem) {
				Mem *m = &fn->mem[r1.val];
				if (!req(m->base, R) && rtype(m->base) == RTmp) {
					fprintf(f, "\tmov bx, %s\n", rname[m->base.val]);
					if (m->offset.type == CBits) {
						fprintf(f, "\tmov word ptr [bx+%"PRIi64"], ax\n", m->offset.bits.i);
						fprintf(f, "\tmov word ptr [bx+%"PRIi64"], dx\n", m->offset.bits.i + 2);
					} else {
						fprintf(f, "\tmov word ptr [bx], ax\n");
						fprintf(f, "\tmov word ptr [bx+2], dx\n");
					}
				}
			}
			return;

		/* 32-bit comparison operations */
		case Oceql:
			/*
			 * 32-bit equality comparison
			 * Compare both words, result is 1 if both equal
			 */
			/* Load first operand */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			/* Compare high word first */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjne .L_ceql_ne_%p\n", (void*)i);

			/* Compare low word */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjne .L_ceql_ne_%p\n", (void*)i);

			/* Equal */
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, "\tjmp .L_ceql_done_%p\n", (void*)i);
			fprintf(f, ".L_ceql_ne_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, ".L_ceql_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocnel:
			/*
			 * 32-bit inequality comparison
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			/* Compare high word */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjne .L_cnel_ne_%p\n", (void*)i);

			/* Compare low word */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjne .L_cnel_ne_%p\n", (void*)i);

			/* Equal - result is 0 */
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cnel_done_%p\n", (void*)i);
			fprintf(f, ".L_cnel_ne_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cnel_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocsltl:
			/*
			 * 32-bit signed less than
			 * Compare high words first (signed), then low words (unsigned) if high equal
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			/* Compare high word (signed) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjl .L_csltl_true_%p\n", (void*)i);
			fprintf(f, "\tjg .L_csltl_false_%p\n", (void*)i);

			/* High words equal, compare low (unsigned) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjb .L_csltl_true_%p\n", (void*)i);

			fprintf(f, ".L_csltl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csltl_done_%p\n", (void*)i);
			fprintf(f, ".L_csltl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csltl_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocslel:
			/*
			 * 32-bit signed less than or equal
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjl .L_cslel_true_%p\n", (void*)i);
			fprintf(f, "\tjg .L_cslel_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjbe .L_cslel_true_%p\n", (void*)i);

			fprintf(f, ".L_cslel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cslel_done_%p\n", (void*)i);
			fprintf(f, ".L_cslel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cslel_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocsgtl:
			/*
			 * 32-bit signed greater than
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjg .L_csgtl_true_%p\n", (void*)i);
			fprintf(f, "\tjl .L_csgtl_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tja .L_csgtl_true_%p\n", (void*)i);

			fprintf(f, ".L_csgtl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csgtl_done_%p\n", (void*)i);
			fprintf(f, ".L_csgtl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csgtl_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocsgel:
			/*
			 * 32-bit signed greater than or equal
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjg .L_csgel_true_%p\n", (void*)i);
			fprintf(f, "\tjl .L_csgel_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjae .L_csgel_true_%p\n", (void*)i);

			fprintf(f, ".L_csgel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csgel_done_%p\n", (void*)i);
			fprintf(f, ".L_csgel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csgel_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocultl:
			/*
			 * 32-bit unsigned less than
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjb .L_cultl_true_%p\n", (void*)i);
			fprintf(f, "\tja .L_cultl_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjb .L_cultl_true_%p\n", (void*)i);

			fprintf(f, ".L_cultl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cultl_done_%p\n", (void*)i);
			fprintf(f, ".L_cultl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cultl_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Oculel:
			/*
			 * 32-bit unsigned less than or equal
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tjb .L_culel_true_%p\n", (void*)i);
			fprintf(f, "\tja .L_culel_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjbe .L_culel_true_%p\n", (void*)i);

			fprintf(f, ".L_culel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_culel_done_%p\n", (void*)i);
			fprintf(f, ".L_culel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_culel_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocugtl:
			/*
			 * 32-bit unsigned greater than
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tja .L_cugtl_true_%p\n", (void*)i);
			fprintf(f, "\tjb .L_cugtl_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tja .L_cugtl_true_%p\n", (void*)i);

			fprintf(f, ".L_cugtl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cugtl_done_%p\n", (void*)i);
			fprintf(f, ".L_cugtl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cugtl_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocugel:
			/*
			 * 32-bit unsigned greater than or equal
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp dx, word ptr [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
			}
			fprintf(f, "\tja .L_cugel_true_%p\n", (void*)i);
			fprintf(f, "\tjb .L_cugel_false_%p\n", (void*)i);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tcmp ax, word ptr [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				int64_t val = fn->con[r1.val].bits.i;
				fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
			}
			fprintf(f, "\tjae .L_cugel_true_%p\n", (void*)i);

			fprintf(f, ".L_cugel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cugel_done_%p\n", (void*)i);
			fprintf(f, ".L_cugel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cugel_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		default:
			/* Fall through to generic handling for unsupported 32-bit ops */
			fprintf(f, "\t; TODO: 32-bit op %d\n", i->op);
			break;
		}
	}

	/*
	 * Special handling for 8087 FPU operations (Ks = float, Kd = double)
	 *
	 * The 8087 uses a stack-based architecture with registers ST(0) through ST(7).
	 * Operations typically work on ST(0) and ST(1), with results left on ST(0).
	 *
	 * For binary operations (add, sub, mul, div):
	 *   1. Load first operand -> ST(0)
	 *   2. Load second operand -> ST(0), first becomes ST(1)
	 *   3. Execute operation with pop (e.g., faddp) -> result in ST(0)
	 *   4. Store result (fstp) -> pops ST(0) to memory
	 *
	 * Memory sizes:
	 *   - Float (Ks): 4 bytes (dword)
	 *   - Double (Kd): 8 bytes (qword)
	 */
	if (i->cls == Ks || i->cls == Kd) {
		int isdbl = (i->cls == Kd);
		char *szp = isdbl ? "qword" : "dword";  /* size prefix */
		int sz = isdbl ? 8 : 4;  /* byte size */

		r0 = i->arg[0];
		r1 = i->arg[1];

		switch (i->op) {
		case Oadd:
		case Osub:
		case Omul:
		case Odiv:
			/*
			 * Binary FP operation: result = arg0 op arg1
			 * Load both operands, perform operation, store result
			 */
			/* Load first operand to ST(0) */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				/* FP constant - need to load from memory */
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					/* Integer constant treated as FP bits */
					fprintf(f, "\t; TODO: FP immediate constant\n");
				}
			}

			/* Load second operand to ST(0), first becomes ST(1) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				Con *c = &fn->con[r1.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					fprintf(f, "\t; TODO: FP immediate constant\n");
				}
			}

			/* Perform operation: ST(1) op ST(0), pop, result in ST(0) */
			switch (i->op) {
			case Oadd:
				fprintf(f, "\tfaddp st(1), st\n");
				break;
			case Osub:
				/* fsubp: ST(1) - ST(0), pop */
				fprintf(f, "\tfsubp st(1), st\n");
				break;
			case Omul:
				fprintf(f, "\tfmulp st(1), st\n");
				break;
			case Odiv:
				/* fdivp: ST(1) / ST(0), pop */
				fprintf(f, "\tfdivp st(1), st\n");
				break;
			default:
				break;
			}

			/* Store result from ST(0) to destination */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s ptr [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oneg:
			/*
			 * Unary negation: result = -arg0
			 * Load, change sign, store
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			}
			fprintf(f, "\tfchs\n");
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s ptr [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oload:
			/*
			 * Load FP value from memory to FP stack slot
			 * In register-less mode, we store immediately to destination
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				}
			}
			/* Store to destination slot */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s ptr [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Ostores:
		case Ostored:
			/*
			 * Store FP value from arg0 to memory location in arg1
			 */
			/* Load source to FPU stack */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					/* Store FP constant bits directly */
					fprintf(f, "\t; TODO: store FP immediate\n");
				}
			}
			/* Store to destination address */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tfstp %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				Con *c = &fn->con[r1.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfstp %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				}
			}
			return;

		case Ocopy:
			/*
			 * Copy FP value from source to destination
			 * Load from source, store to destination
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s ptr [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else if (c->type == CBits) {
					/* FP constant encoded as bits - store to temp then load */
					if (isdbl) {
						int64_t bits = c->bits.i;
						fprintf(f, "\tmov word ptr [bp-2], %d\n", (int)(bits & 0xFFFF));
						fprintf(f, "\tmov word ptr [bp-4], %d\n", (int)((bits >> 16) & 0xFFFF));
						fprintf(f, "\tmov word ptr [bp-6], %d\n", (int)((bits >> 32) & 0xFFFF));
						fprintf(f, "\tmov word ptr [bp-8], %d\n", (int)((bits >> 48) & 0xFFFF));
						fprintf(f, "\tfld qword ptr [bp-8]\n");
					} else {
						int32_t bits = (int32_t)c->bits.i;
						fprintf(f, "\tmov word ptr [bp-2], %d\n", (int)(bits & 0xFFFF));
						fprintf(f, "\tmov word ptr [bp-4], %d\n", (int)((bits >> 16) & 0xFFFF));
						fprintf(f, "\tfld dword ptr [bp-4]\n");
					}
				}
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s ptr [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oexts:
			/*
			 * Extend float to double: load as float, store as double
			 * The 8087 internally uses 80-bit extended precision,
			 * so conversion is implicit in load/store sizes
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld dword ptr [bp%+ld]\n", (long)slot(r0, fn));
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp qword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			}
			return;

		case Otruncd:
			/*
			 * Truncate double to float: load as double, store as float
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld qword ptr [bp%+ld]\n", (long)slot(r0, fn));
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp dword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			}
			return;

		/* FP comparisons - return integer result */
		case Oceqs:
		case Oceqd:
			/*
			 * Floating point equality comparison
			 * Load both operands, compare, get flags, set result
			 */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));

			/* Compare ST(0) with ST(1) and pop both */
			fprintf(f, "\tfcompp\n");
			/* Transfer FPU status word to AX */
			fprintf(f, "\tfstsw ax\n");
			/* Transfer AH flags to CPU flags */
			fprintf(f, "\tsahf\n");
			/* Set result based on zero flag (equal) */
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tje .Lceq_true_%p\n", (void*)i);
			fprintf(f, "\tjmp .Lceq_done_%p\n", (void*)i);
			fprintf(f, ".Lceq_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lceq_done_%p:\n", (void*)i);

			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocnes:
		case Ocned:
			/* Not equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjne .Lcne_true_%p\n", (void*)i);
			fprintf(f, "\tjmp .Lcne_done_%p\n", (void*)i);
			fprintf(f, ".Lcne_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcne_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocgts:
		case Ocgtd:
			/* Greater than: ST(1) > ST(0) after loading arg0, arg1 */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			/* After fcompp with arg0, arg1: flags set for ST(1) vs ST(0) = arg0 vs arg1 */
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tja .Lcgt_true_%p\n", (void*)i);  /* above = greater (unsigned compare of FP status) */
			fprintf(f, "\tjmp .Lcgt_done_%p\n", (void*)i);
			fprintf(f, ".Lcgt_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcgt_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocges:
		case Ocged:
			/* Greater or equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjae .Lcge_true_%p\n", (void*)i);  /* above or equal */
			fprintf(f, "\tjmp .Lcge_done_%p\n", (void*)i);
			fprintf(f, ".Lcge_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcge_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Oclts:
		case Ocltd:
			/* Less than */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjb .Lclt_true_%p\n", (void*)i);  /* below = less than */
			fprintf(f, "\tjmp .Lclt_done_%p\n", (void*)i);
			fprintf(f, ".Lclt_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lclt_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocles:
		case Ocled:
			/* Less or equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjbe .Lcle_true_%p\n", (void*)i);  /* below or equal */
			fprintf(f, "\tjmp .Lcle_done_%p\n", (void*)i);
			fprintf(f, ".Lcle_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcle_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocos:
		case Ocod:
			/* Ordered (neither is NaN) */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			/* Parity flag is set if unordered (NaN) */
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, "\tjnp .Lcord_done_%p\n", (void*)i);  /* not parity = ordered */
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, ".Lcord_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		case Ocuos:
		case Ocuod:
			/* Unordered (at least one is NaN) */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s ptr [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			/* Parity flag is set if unordered (NaN) */
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjp .Lcuord_true_%p\n", (void*)i);  /* parity = unordered */
			fprintf(f, "\tjmp .Lcuord_done_%p\n", (void*)i);
			fprintf(f, ".Lcuord_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcuord_done_%p:\n", (void*)i);
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
			return;

		default:
			/* Fall through to check for int/float conversions or generic handling */
			break;
		}
	}

	/*
	 * Integer to/from floating point conversions
	 * These are handled separately because they cross between Ks/Kd and Kw/Kl classes
	 */
	switch (i->op) {
	case Oswtof:
		/*
		 * Signed word to float/double
		 * fild loads a signed integer and converts to FP
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfild word ptr [bp%+ld]\n", (long)slot(r0, fn));
		} else if (rtype(r0) == RTmp) {
			/* Need to store register to temp location first */
			fprintf(f, "\tpush %s\n", rname[r0.val]);
			fprintf(f, "\tfild word ptr [sp]\n");
			fprintf(f, "\tadd sp, 2\n");
		}
		/* Store result based on destination class */
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword ptr [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Ouwtof:
		/*
		 * Unsigned word to float/double
		 * 8087 only has signed integer loads, so we need to handle unsigned specially
		 * For 16-bit unsigned, extend to 32-bit signed and load as dword
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			/* Load as word, zero-extend mentally (push 0 for high word) */
			fprintf(f, "\tpush word ptr 0\n");
			fprintf(f, "\tpush word ptr [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tfild dword ptr [sp]\n");
			fprintf(f, "\tadd sp, 4\n");
		} else if (rtype(r0) == RTmp) {
			fprintf(f, "\tpush word ptr 0\n");
			fprintf(f, "\tpush %s\n", rname[r0.val]);
			fprintf(f, "\tfild dword ptr [sp]\n");
			fprintf(f, "\tadd sp, 4\n");
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword ptr [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Osltof:
		/*
		 * Signed long (32-bit) to float/double
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfild dword ptr [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword ptr [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Oultof:
		/*
		 * Unsigned long (32-bit) to float/double
		 * Need to handle as 64-bit signed to avoid sign issues
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			/* Push 0 for high 32 bits, then the unsigned 32-bit value */
			fprintf(f, "\tpush word ptr 0\n");
			fprintf(f, "\tpush word ptr 0\n");
			fprintf(f, "\tpush word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
			fprintf(f, "\tpush word ptr [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tfild qword ptr [sp]\n");
			fprintf(f, "\tadd sp, 8\n");
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword ptr [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword ptr [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Ostosi:
		/*
		 * Float to signed word
		 * fistp stores and pops FP stack as integer
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld dword ptr [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tfistp word ptr [bp%+ld]\n", (long)slot(i->to, fn));
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tsub sp, 2\n");
			fprintf(f, "\tfistp word ptr [sp]\n");
			fprintf(f, "\tpop %s\n", rname[i->to.val]);
		}
		return;

	case Odtosi:
		/*
		 * Double to signed word
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld qword ptr [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tfistp word ptr [bp%+ld]\n", (long)slot(i->to, fn));
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tsub sp, 2\n");
			fprintf(f, "\tfistp word ptr [sp]\n");
			fprintf(f, "\tpop %s\n", rname[i->to.val]);
		}
		return;

	case Ostoui:
		/*
		 * Float to unsigned word
		 * 8087 only has signed integer store, need to handle range
		 * For simplicity, treat as signed (works for values < 32768)
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld dword ptr [bp%+ld]\n", (long)slot(r0, fn));
		}
		/* Store as dword to handle full unsigned range, take low word */
		fprintf(f, "\tsub sp, 4\n");
		fprintf(f, "\tfistp dword ptr [sp]\n");
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tpop word ptr [bp%+ld]\n", (long)slot(i->to, fn));
			fprintf(f, "\tadd sp, 2\n");
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tpop %s\n", rname[i->to.val]);
			fprintf(f, "\tadd sp, 2\n");
		}
		return;

	case Odtoui:
		/*
		 * Double to unsigned word
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld qword ptr [bp%+ld]\n", (long)slot(r0, fn));
		}
		fprintf(f, "\tsub sp, 4\n");
		fprintf(f, "\tfistp dword ptr [sp]\n");
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tpop word ptr [bp%+ld]\n", (long)slot(i->to, fn));
			fprintf(f, "\tadd sp, 2\n");
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tpop %s\n", rname[i->to.val]);
			fprintf(f, "\tadd sp, 2\n");
		}
		return;

	case Ocast:
		/*
		 * Bitwise cast between integer and floating point
		 * For Kw->Ks or Ks->Kw: 32-bit reinterpret
		 * For Kl->Kd or Kd->Kl: 64-bit reinterpret
		 */
		r0 = i->arg[0];
		if (i->cls == Ks) {
			/* Integer to float bitcast */
			if (rtype(r0) == RSlot) {
				/* Just copy the bytes */
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
				fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			}
		} else if (i->cls == Kd) {
			/* Long to double bitcast */
			if (rtype(r0) == RSlot && rtype(i->to) == RSlot) {
				for (int j = 0; j < 4; j++) {
					fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn) + j*2);
					fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn) + j*2);
				}
			}
		} else if (i->cls == Kw) {
			/* Float to integer bitcast */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn));
				if (rtype(i->to) == RSlot)
					fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn));
				else if (rtype(i->to) == RTmp)
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn) + 2);
				if (rtype(i->to) == RSlot)
					fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			}
		} else if (i->cls == Kl) {
			/* Double to long bitcast */
			if (rtype(r0) == RSlot && rtype(i->to) == RSlot) {
				for (int j = 0; j < 4; j++) {
					fprintf(f, "\tmov ax, word ptr [bp%+ld]\n", (long)slot(r0, fn) + j*2);
					fprintf(f, "\tmov word ptr [bp%+ld], ax\n", (long)slot(i->to, fn) + j*2);
				}
			}
		}
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

	/* Special handling for Ocall (function calls) */
	if (i->op == Ocall) {
		Ref target = i->arg[0];

		/* Call target must be either:
		 * 1. RTmp (register) for indirect calls
		 * 2. RCon with CAddr (function name) for direct calls
		 */
		fprintf(f, "\tcall ");

		if (rtype(target) == RTmp) {
			/* Indirect call through register */
			fprintf(f, "%s\n", rname[target.val]);
		} else if (rtype(target) == RCon) {
			/* Direct call to function */
			Con *c = &fn->con[target.val];
			if (c->type == CAddr) {
				/* Function name */
				emitaddr(c, f);
				fputc('\n', f);
			} else {
				die("call with non-address constant");
			}
		} else {
			/* Invalid call target - must load into register first */
			die("invalid call target type (must be register or function name)");
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
