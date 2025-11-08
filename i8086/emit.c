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
	{ Oshl,    Ki, "shl %=, cl" },
	{ Oshr,    Ki, "shr %=, cl" },
	{ Osar,    Ki, "sar %=, cl" },

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
			default:
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

			if (rtype(r) == RCon) {
				pc = &fn->con[r.val];
				if (pc->type == CAddr) {
					fputc('[', f);
					emitaddr(pc, f);
					fputc(']', f);
				} else {
					fprintf(f, "%"PRIi64, pc->bits.i);
				}
			} else if (rtype(r) == RTmp) {
				fprintf(f, "[%s]", rname[r.val]);
			} else if (rtype(r) == RSlot) {
				offset = slot(r, fn);
				fprintf(f, "[bp%+ld]", (long)offset);
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
