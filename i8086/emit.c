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
	/* Omul handled in emitins() — `imul reg, r/m` is 286+.  The 8086
	 * form is single-operand `imul r/m` with implicit AX*r/m → DX:AX. */
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
	{ Ostoreb, Kw, "mov byte %M1, %B0" },
	{ Ostoreh, Kw, "mov word %M1, %0" },
	{ Ostorew, Kw, "mov word %M1, %0" },
	{ Ostorel, Kw, "mov dword %M1, %0" },

	/* Oloadsb/Oloadub/Oloadsh/Oloaduh handled in emitins() — movzx/movsx
	 * are 386 instructions.  The 8086 sequence is `xor reg, reg; mov
	 * reg8, byte [mem]` for zero-extend or `mov al, byte [mem]; cbw`
	 * for sign-extend.  For half-word loads on 16-bit hardware the
	 * "extension to 32-bit" is moot — rega doesn't allocate pairs, so
	 * the high half is lost regardless. */
	{ Oloadsw, Ki, "mov %=, word %M0" },
	{ Oloaduw, Ki, "mov %=, word %M0" },
	{ Oload,   Kw, "mov %=, word %M0" },
	{ Oload,   Kl, "mov %=, dword %M0" },

	/* Extensions to word.  On 16-bit 8086 a halfword (16-bit) is already a
	 * word, so Oextsh/Oextuh Kw are just register/memory moves.  Oextub Kw
	 * masks the high byte; Oextsb Kw needs CBW and is handled in emitins()
	 * (CBW only operates on AL → AX). */
	{ Oextsh,  Kw, "mov %=, %0" },
	{ Oextuh,  Kw, "mov %=, %0" },
	{ Oextub,  Kw, "mov %=, %0\n\tand %=, 255" },

	/* Data movement */
	{ Ocopy,   Ki, "mov %=, %0" },
	{ Oswap,   Ki, "xchg %=, %0" },
	{ Oaddr,   Ki, "lea %=, %M0" },

	/* 16-bit comparisons are emitted in emitins() with an explicit
	 * 8086-compatible branchy materialize.  setcc + movzx are 386,
	 * so we don't use them. */

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

/* Register names for 16-bit x86.  Slots beyond RSP must still have valid
 * non-NULL strings: the codegen occasionally lands here for segment regs
 * (RES/RDS).  Use placeholders rather than reading garbage memory. */
static char *rname[] = {
	[RAX] = "ax",
	[RCX] = "cx",
	[RDX] = "dx",
	[RBX] = "bx",
	[RSI] = "si",
	[RDI] = "di",
	[RBP] = "bp",
	[RSP] = "sp",
	[RES] = "es",
	[RDS] = "ds",
};

/* 8-bit register names (low byte) */
static char *rname8[] = {
	[RAX] = "al",
	[RCX] = "cl",
	[RDX] = "dl",
	[RBX] = "bl",
};

/* Memory model names for comments */
static char *memmodel_name[] = {
	[Mflat]    = "flat",
	[Mtiny]    = "tiny",
	[Msmall]   = "small",
	[Mmedium]  = "medium",
	[Mcompact] = "compact",
	[Mlarge]   = "large",
	[Mhuge]    = "huge",
};

/* Emit memory model directive/comment at start of module
 * This is called once at the start of code emission
 */
static int model_header_emitted = 0;

static void
emit_model_header(FILE *f)
{
	if (model_header_emitted)
		return;
	model_header_emitted = 1;

	fprintf(f, "; Memory model: %s\n", memmodel_name[T.memmodel]);

	switch (T.memmodel) {
	case Mtiny:
		fprintf(f, "; Tiny model: .COM format, org 100h\n");
		break;
	case Msmall:
		fprintf(f, "; Small model: near code, near data\n");
		break;
	case Mmedium:
		fprintf(f, "; Medium model: far code, near data\n");
		break;
	case Mcompact:
		fprintf(f, "; Compact model: near code, far data\n");
		break;
	case Mlarge:
		fprintf(f, "; Large model: far code, far data\n");
		break;
	case Mhuge:
		fprintf(f, "; Huge model: far code, far data, large arrays\n");
		break;

	default:
		/* Flat model (non-8086) - just emit code section */
		fprintf(f, ".text\n");
		break;
	}
	fprintf(f, "\n");
}

static int64_t
slot(Ref r, Fn *fn)
{
	int s;

	s = rsval(r);
	assert(s <= fn->slot);
	/* Stack grows down, slots are 2 bytes for 16-bit.  The prologue
	 * pushes BX, SI, DI (3 callee-save words) AFTER `mov bp, sp`, so
	 * locals/slots live BELOW that 6-byte block — shift offsets by -6
	 * so slot 0 lands at [bp - 6 - 2*fn->slot] (still adjacent to SP). */
	if (s < 0)
		return 2 * -s;
	else
		return -6 - 2 * (fn->slot - s);
}

static void
emitaddr(Con *c, FILE *f)
{
	const char *name;
	assert(c->sym.type == SGlo || c->sym.type == SThr);
	name = str(c->sym.id);
	/* Apply target symbol prefix (e.g. "_") so references match the
	 * function/data labels emitted with the same prefix. */
	if (name[0] != '"' && T.assym[0])
		fputs(T.assym, f);
	fputs(name, f);
	if (c->bits.i)
		fprintf(f, "+%"PRIi64, c->bits.i);
}

/* Render just the memory operand text (no leading tab, no instruction
 * mnemonic) so callers can compose it into custom instruction sequences.
 * Mirrors the %M handler in emitf. */
static void
emit_memref(Ref r, Fn *fn, FILE *f)
{
	Con *pc;
	if (rtype(r) == RTmp)
		fprintf(f, "[%s]", rname[r.val]);
	else if (rtype(r) == RSlot)
		fprintf(f, "[bp%+ld]", (long)slot(r, fn));
	else if (rtype(r) == RCon) {
		pc = &fn->con[r.val];
		if (pc->type == CAddr) {
			fputc('[', f);
			emitaddr(pc, f);
			fputc(']', f);
		} else
			fprintf(f, "%"PRIi64, pc->bits.i);
	} else if (rtype(r) == RMem) {
		Mem *m = &fn->mem[r.val];
		int has_offset = (m->offset.type != CUndef);
		int has_base = !req(m->base, R);
		int has_index = !req(m->index, R);
		fputc('[', f);
		if (has_base) {
			if (rtype(m->base) == RTmp)
				fprintf(f, "%s", rname[m->base.val]);
			else if (rtype(m->base) == RSlot)
				fprintf(f, "bp%+ld", (long)slot(m->base, fn));
		}
		if (has_index) {
			if (has_base)
				fprintf(f, " + ");
			if (rtype(m->index) == RTmp)
				fprintf(f, "%s", rname[m->index.val]);
		}
		if (has_offset) {
			if (has_base || has_index)
				fprintf(f, " + ");
			if (m->offset.type == CAddr)
				emitaddr(&m->offset, f);
			else if (m->offset.type == CBits)
				fprintf(f, "%"PRIi64, m->offset.bits.i);
		}
		fputc(']', f);
	}
}

/* Store the AX-resident result of a Kl operation to its destination.
 * Slot dests get a single mov (callers that need the high word write it
 * separately).  Tmp dests just receive the low word — rega doesn't know
 * about register pairs.  Suppresses self-moves when the destination is
 * already AX. */
static void
store_ax_to(Ref to, Fn *fn, FILE *f)
{
	if (rtype(to) == RTmp) {
		if (strcmp(rname[to.val], "ax") != 0)
			fprintf(f, "\tmov %s, ax\n", rname[to.val]);
	} else if (rtype(to) == RSlot)
		fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(to, fn));
}

/* Preserve AX/DX across a Kl op that uses them as scratch.  rega doesn't
 * model the implicit clobber, so we save/restore the caller's AX/DX
 * unless the op's destination is one of them (in which case the op writes
 * it and restoring would overwrite the result).  Push/pop is cheaper than
 * the alternative — letting rega spill via memory — for typical 8086 code
 * pressure.  Skip the save when the destination is the register being
 * saved, since the result must remain there. */
typedef struct AxDxSave {
	int save_ax;
	int save_dx;
} AxDxSave;

static AxDxSave
kl_save_axdx(Ref to, FILE *f)
{
	AxDxSave s;
	int dst_in_ax = (rtype(to) == RTmp && to.val == RAX);
	int dst_in_dx = (rtype(to) == RTmp && to.val == RDX);
	s.save_ax = !dst_in_ax;
	s.save_dx = !dst_in_dx;
	if (s.save_ax) fprintf(f, "\tpush ax\n");
	if (s.save_dx) fprintf(f, "\tpush dx\n");
	return s;
}

static void
kl_restore_axdx(AxDxSave s, FILE *f)
{
	if (s.save_dx) fprintf(f, "\tpop dx\n");
	if (s.save_ax) fprintf(f, "\tpop ax\n");
}

/* When a Kl op uses AX/DX as scratch (Oadd/Osub/Omul), an arg that
 * rega placed in AX or DX is silently lost: `mov ax, r0` overwrites
 * a r1-in-AX; `xor dx, dx` zeros a r1-in-DX.  Subsequent references
 * to rname[r1.val] then resolve to the clobbered reg.  Stage such
 * args into BX or CX before AX/DX are touched; callers reference
 * `stage.scratch_reg` in place of rname[r1.val]. */
typedef struct ArgStage {
	const char *scratch_reg;  /* "bx"/"cx", or NULL if no staging */
	int pushed;               /* whether we push'd to save caller's value */
} ArgStage;

static ArgStage
kl_stage_arg(Ref r1, Ref r0, Ref to, FILE *f)
{
	ArgStage s = { NULL, 0 };
	const char *cands[2] = { "bx", "cx" };
	int dst_aliases_scratch;
	int k;

	if (rtype(r1) != RTmp) return s;
	if (r1.val != RAX && r1.val != RDX) return s;

	/* Pick a scratch reg that isn't r0's reg (else `mov scratch, r1`
	 * would clobber r0 before we load it). */
	for (k = 0; k < 2; k++) {
		if (rtype(r0) == RTmp && strcmp(rname[r0.val], cands[k]) == 0)
			continue;
		s.scratch_reg = cands[k];
		break;
	}
	if (!s.scratch_reg) s.scratch_reg = "bx";

	dst_aliases_scratch = (rtype(to) == RTmp
		&& strcmp(rname[to.val], s.scratch_reg) == 0);
	if (!dst_aliases_scratch) {
		fprintf(f, "\tpush %s\n", s.scratch_reg);
		s.pushed = 1;
	}
	fprintf(f, "\tmov %s, %s\n", s.scratch_reg, rname[r1.val]);
	return s;
}

static void
kl_unstage_arg(ArgStage s, FILE *f)
{
	if (s.pushed) fprintf(f, "\tpop %s\n", s.scratch_reg);
}

/* Emit a Kl RCon value into AX:DX.  CBits gets the literal split into
 * low/high words.  CAddr is materialized as a far pointer: `sym+addend`
 * for the offset, `seg sym` for the segment selector — NASM emits the
 * appropriate FIXUP records so omf_link resolves both at link time.
 * Used in far-data models where minic emits `storel $glo, slot` (e.g.
 * the format-string arg of a variadic printf call). */
static void
load32_axdx_con(Con *pc, FILE *f)
{
	int64_t val;
	if (pc->type == CAddr) {
		fprintf(f, "\tmov ax, ");
		emitaddr(pc, f);
		fputc('\n', f);
		fprintf(f, "\tmov dx, seg ");
		fputs(T.assym, f);
		fputs(str(pc->sym.id), f);
		fputc('\n', f);
	} else {
		val = pc->bits.i;
		fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
		fprintf(f, "\tmov dx, %d\n", (int)((val >> 16) & 0xFFFF));
	}
}

/* Load a far-pointer RCon into ES:BX (offset → BX, segment → ES via AX).
 * For CAddr: emits `sym+addend` for BX and `seg sym` for AX→ES so NASM
 * generates the relocations omf_link uses to fix up the MZ image.  For
 * CBits: splits low/high 16 as offset/segment.  Used by the Oloadf*
 * and Ostoref* handlers; an earlier inline `bits.i`-only path silently
 * dropped the segment word for CAddr operands, which arose after QBE
 * constant-folded e.g. `&arr[const_i]` into a single CAddr. */
static void
load_farptr_con(Con *pc, FILE *f)
{
	int64_t val;
	if (pc->type == CAddr) {
		fprintf(f, "\tmov bx, ");
		emitaddr(pc, f);
		fputc('\n', f);
		fprintf(f, "\tmov ax, seg ");
		fputs(T.assym, f);
		fputs(str(pc->sym.id), f);
		fputc('\n', f);
		fprintf(f, "\tmov es, ax\n");
	} else {
		val = pc->bits.i;
		fprintf(f, "\tmov bx, %d\n", (int)(val & 0xFFFF));
		fprintf(f, "\tmov ax, %d\n", (int)((val >> 16) & 0xFFFF));
		fprintf(f, "\tmov es, ax\n");
	}
}

/* Emit a 32-bit immediate bitwise op (and/or/xor) against DX:AX from
 * a Kl RCon.  Handles both CBits (split low/high 16) and CAddr (same
 * `sym+addend` / `seg sym` relocation pair the Kl Oadd/Osub handlers
 * use).  NASM accepts a relocatable immediate for any arithmetic or
 * logical op, and omf_link resolves both fixups at MZ assembly.
 * Without this, e.g. `x | (uint32_t)&g` would lose the segment word
 * because `op dx, %d` would emit `bits.i >> 16` which is 0 for CAddr
 * — CAddr carries only the addend in bits.i; the segment comes from
 * the relocation.  Same family as the Oadd/Osub Kl fix in 141f2e8. */
static void
emit32_logop_axdx_con(const char *op, Con *pc, FILE *f)
{
	int64_t val;
	if (pc->type == CAddr) {
		fprintf(f, "\t%s ax, ", op);
		emitaddr(pc, f);
		fputc('\n', f);
		fprintf(f, "\t%s dx, seg ", op);
		fputs(T.assym, f);
		fputs(str(pc->sym.id), f);
		fputc('\n', f);
	} else {
		val = pc->bits.i;
		fprintf(f, "\t%s ax, %d\n", op, (int)(val & 0xFFFF));
		fprintf(f, "\t%s dx, %d\n", op, (int)((val >> 16) & 0xFFFF));
	}
}

/* 8086-safe `shift reg, N`: the multi-bit immediate form (e.g. `shl
 * dx, 8`) was introduced on the 80186, so under `cpu 8086` NASM
 * rejects it.  Emit `shift reg, 1` for N==1 (8086-valid), else stage
 * the count into CL.  Caller must have preserved CX (the Kl shift
 * handlers already push/pop CX as part of the AX/DX/CX bracket). */
static void
emit_shift_imm(const char *op, const char *reg, int n, FILE *f)
{
	if (n <= 0) return;
	if (n == 1) {
		fprintf(f, "\t%s %s, 1\n", op, reg);
	} else {
		fprintf(f, "\tmov cl, %d\n", n);
		fprintf(f, "\t%s %s, cl\n", op, reg);
	}
}

/* Load a 32-bit operand into DX:AX.  The original 32-bit handlers only
 * handled RSlot/RCon; this also handles RTmp (treats the temp's register
 * as the low word and zero-extends DX, matching the convention in the
 * Oadd/Osub Kl handlers). */
static void
load32_dxax(Ref r, Fn *fn, FILE *f)
{
	if (rtype(r) == RSlot) {
		fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r, fn));
		fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r, fn) + 2);
	} else if (rtype(r) == RCon) {
		load32_axdx_con(&fn->con[r.val], f);
	} else if (rtype(r) == RTmp) {
		fprintf(f, "\tmov ax, %s\n", rname[r.val]);
		fprintf(f, "\txor dx, dx\n");
	}
}

/* Compare DX:AX against a 32-bit operand by emitting two cmp instructions:
 *   cmp dx, <high(r)>
 *   cmp ax, <low(r)>
 * The caller decides what jcc to insert between them.  For RTmp, treats
 * the high word as 0 (matching load32_dxax's zero-extension).
 *
 * CAddr handling: NASM accepts `seg sym` and `sym+addend` as relocatable
 * immediates for cmp; omf_link supplies both fixups.  Without this, an
 * earlier `bits.i`-only path silently dropped the segment word — `if
 * (kl_var == (long)&g)` would compare against `addend | 0` and either
 * always-true or always-false depending on whether DGROUP happened to be
 * zero. */
static void
cmp32_high(Ref r, Fn *fn, FILE *f)
{
	Con *pc;
	int64_t val;
	if (rtype(r) == RSlot)
		fprintf(f, "\tcmp dx, word [bp%+ld]\n", (long)slot(r, fn) + 2);
	else if (rtype(r) == RCon) {
		pc = &fn->con[r.val];
		if (pc->type == CAddr) {
			fprintf(f, "\tcmp dx, seg ");
			fputs(T.assym, f);
			fputs(str(pc->sym.id), f);
			fputc('\n', f);
		} else {
			val = pc->bits.i;
			fprintf(f, "\tcmp dx, %d\n", (int)((val >> 16) & 0xFFFF));
		}
	} else if (rtype(r) == RTmp)
		fprintf(f, "\tcmp dx, 0\n");
}

static void
cmp32_low(Ref r, Fn *fn, FILE *f)
{
	Con *pc;
	int64_t val;
	if (rtype(r) == RSlot)
		fprintf(f, "\tcmp ax, word [bp%+ld]\n", (long)slot(r, fn));
	else if (rtype(r) == RCon) {
		pc = &fn->con[r.val];
		if (pc->type == CAddr) {
			fprintf(f, "\tcmp ax, ");
			emitaddr(pc, f);
			fputc('\n', f);
		} else {
			val = pc->bits.i;
			fprintf(f, "\tcmp ax, %d\n", (int)(val & 0xFFFF));
		}
	} else if (rtype(r) == RTmp)
		fprintf(f, "\tcmp ax, %s\n", rname[r.val]);
}

/* Push a Kl operand as `push hi; push lo` so that after the pair the low
 * word sits on top of stack — i.e. lower address than the high word.
 * Used to set up cdecl-style 32-bit args for libstub helpers like
 * _qbe_div32u.  Uses CX as scratch (caller is expected to have already
 * saved CX along with AX/DX); deliberately avoids `mov ax, <reg>` /
 * `xor dx, dx` style staging so the source operand can safely live in
 * AX or DX without aliasing — both of which the caller has on stack and
 * may want preserved for a subsequent emit_push_long of the other arg. */
static void
emit_push_long(Ref r, Fn *fn, FILE *f)
{
	int64_t val;
	Con *pc;
	if (rtype(r) == RSlot) {
		fprintf(f, "\tpush word [bp%+ld]\n", (long)slot(r, fn) + 2);
		fprintf(f, "\tpush word [bp%+ld]\n", (long)slot(r, fn));
	} else if (rtype(r) == RCon) {
		pc = &fn->con[r.val];
		/* 8086 has no `push imm16` — route through CX. */
		if (pc->type == CAddr) {
			/* CAddr: segment lives in the relocation, not in bits.i.
			 * Push `seg sym` (high) then `sym+addend` (low); NASM emits
			 * BASE-SEGMENT and OFFSET fixups that omf_link resolves. */
			fprintf(f, "\tmov cx, seg ");
			fputs(T.assym, f);
			fputs(str(pc->sym.id), f);
			fputc('\n', f);
			fprintf(f, "\tpush cx\n");
			fprintf(f, "\tmov cx, ");
			emitaddr(pc, f);
			fputc('\n', f);
			fprintf(f, "\tpush cx\n");
		} else {
			val = pc->bits.i;
			fprintf(f, "\tmov cx, %d\n", (int)((val >> 16) & 0xFFFF));
			fprintf(f, "\tpush cx\n");
			fprintf(f, "\tmov cx, %d\n", (int)(val & 0xFFFF));
			fprintf(f, "\tpush cx\n");
		}
	} else if (rtype(r) == RTmp) {
		/* Temp's register holds the low half (rega doesn't pair Kl);
		 * high half is zero-extended.  Push 0 (via CX) for hi, then
		 * push the temp's register directly. */
		fprintf(f, "\txor cx, cx\n");
		fprintf(f, "\tpush cx\n");
		fprintf(f, "\tpush %s\n", rname[r.val]);
	}
}

/* Detect if a Ref-as-memory-operand uses AX/CX/DX as the base register.
 * 8086 only allows BX/BP/SI/DI as memory base.  Returns the offending
 * register (RAX/RCX/RDX) or 0 if no fixup is needed.
 */
static int
addr_fixup_reg(Ref r, Fn *fn)
{
	Mem *m;
	int v = -1;

	if (rtype(r) == RTmp)
		v = r.val;
	else if (rtype(r) == RMem) {
		m = &fn->mem[r.val];
		if (!req(m->base, R) && rtype(m->base) == RTmp)
			v = m->base.val;
	}
	if (v == RAX || v == RCX || v == RDX)
		return v;
	return 0;
}

/* Swap RTmp register references between RBX and `bad` in a Ref.
 * Used to remap an instruction's operands while BX is xchg'd with
 * the bad addressing register.  Mutates Ref in place.
 */
static void
swap_bx(Ref *r, int bad, Fn *fn)
{
	Mem *m;

	if (rtype(*r) == RTmp) {
		if (r->val == RBX) r->val = bad;
		else if (r->val == bad) r->val = RBX;
	} else if (rtype(*r) == RMem) {
		m = &fn->mem[r->val];
		if (rtype(m->base) == RTmp) {
			if (m->base.val == RBX) m->base.val = bad;
			else if (m->base.val == bad) m->base.val = RBX;
		}
		if (rtype(m->index) == RTmp) {
			if (m->index.val == RBX) m->index.val = bad;
			else if (m->index.val == bad) m->index.val = RBX;
		}
	}
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

			if (rtype(r) == RTmp && r.val <= RBX) {
				fprintf(f, "%s", rname8[r.val]);
			} else if (rtype(r) == RTmp) {
				/* SI/DI/BP/SP have no 8-bit form on 8086.  Emit `al`
				 * as a fallback; selstoreb in i8086/isel.c is supposed
				 * to copy through RAX, but we may end here for ops the
				 * isel doesn't yet rewrite. */
				fprintf(f, "al ; XXX wanted 8-bit form of %s",
					(r.val < (int)(sizeof rname / sizeof rname[0]) && rname[r.val])
						? rname[r.val] : "?");
			} else if (rtype(r) == RCon) {
				/* Immediate operand for a byte op — print the constant. */
				pc = &fn->con[r.val];
				if (pc->type == CBits)
					fprintf(f, "%"PRIi64, pc->bits.i);
				else
					emitaddr(pc, f);
			} else {
				die("8-bit register only available for AX-BX");
			}
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
				case CBits: {
					int64_t v = pc->bits.i;
					/* Truncate to the instruction's size class so that, e.g.,
					 * a Kw `sub 0, 1` folded to int32 -1 (= 0xFFFFFFFF =
					 * 4294967295) emits as -1 / 0xFFFF rather than blowing
					 * past NASM's word bound. */
					if (i->cls == Kw)
						v = (int64_t)(int16_t)v;
					fprintf(f, "%"PRIi64, v);
					break;
				}
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

				fprintf(f, "word [");

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

	/* Handle inline assembly */
	if (i->op == Oasm) {
		int idx = rsval(i->arg[0]);
		if (idx >= 0 && idx < fn->nasmstr && fn->asmstr[idx]) {
			/* Emit the raw assembly code */
			/* Handle escape sequences in the string */
			char *s = fn->asmstr[idx];
			while (*s) {
				if (*s == '\\' && *(s+1) == 'n') {
					fputc('\n', f);
					s += 2;
				} else if (*s == '\\' && *(s+1) == 't') {
					fputc('\t', f);
					s += 2;
				} else if (*s == '%' && *(s+1) == '%') {
					/* %% in GCC asm becomes single % */
					fputc('%', f);
					s += 2;
				} else {
					fputc(*s, f);
					s++;
				}
			}
			fputc('\n', f);
		}
		return;
	}

	/* Special handling for shift operations.  8086 supports only
	 *   shl/shr/sar reg, 1
	 *   shl/shr/sar reg, cl
	 * The `reg, imm8` form was added on the 186.  Counts other than 1
	 * have to come through CL.  Kl (32-bit) shifts fall through to the
	 * Kl-special handler below, which emits the proper rcr/rcl pair. */
	if ((i->op == Oshl || i->op == Oshr || i->op == Osar) && i->cls != Kl) {
		int64_t imm_cnt = -1;  /* >=0 means "use this immediate" */
		shiftop = (i->op == Oshl) ? "shl" :
		          (i->op == Oshr) ? "shr" : "sar";
		r0 = i->arg[0]; /* value to shift */
		r1 = i->arg[1]; /* shift count */

		if (rtype(r1) == RCon)
			imm_cnt = fn->con[r1.val].bits.i;

		/* Move value to destination if needed.  This must happen
		 * before any CX/CL load, because when dst==CX the dst-mov
		 * would otherwise clobber the loaded count. */
		if (rtype(i->to) == RTmp && r0.val != i->to.val && rtype(r0) == RTmp) {
			fprintf(f, "\tmov %s, %s\n", rname[i->to.val], rname[r0.val]);
			r0 = i->to;
		}

		/* Pick the destination register name we'll print in `shl/shr/sar`. */
		const char *dstname =
		    (rtype(r0) == RTmp)        ? rname[r0.val] :
		    (rtype(i->to) == RTmp)     ? rname[i->to.val] : "?";
		int dst_is_cx = (rtype(i->to) == RTmp && i->to.val == RCX) ||
		                (rtype(r0)    == RTmp && r0.val    == RCX);

		if (imm_cnt == 1) {
			fprintf(f, "\t%s %s, 1\n", shiftop, dstname);
		} else if (imm_cnt == 0) {
			/* No-op shift — emit syntactically valid output. */
			fprintf(f, "\t%s %s, 0\n", shiftop, dstname);
		} else if (imm_cnt > 1 && imm_cnt <= 8) {
			/* Small immediate count: unroll into repeated
			 * `shl dst, 1`.  This avoids touching CX/CL entirely,
			 * which is critical because the i8086 backend doesn't
			 * tell rega that shifts clobber CL — without unrolling,
			 * `mov cl, imm` would corrupt any live Kw value rega
			 * happened to keep in CX across the shift. */
			int64_t k;
			for (k = 0; k < imm_cnt; k++)
				fprintf(f, "\t%s %s, 1\n", shiftop, dstname);
		} else if (imm_cnt > 8) {
			/* Large immediate count: must use CL.  Save CX around
			 * the shift so any unrelated live value in CX is
			 * preserved.  When dst is CX itself, the value being
			 * shifted IS what's pushed/popped, so we need a
			 * different scratch — route via BX. */
			if (dst_is_cx) {
				fprintf(f, "\tpush bx\n");
				fprintf(f, "\tmov bx, %s\n", dstname);
				fprintf(f, "\tmov cl, %"PRIi64"\n", imm_cnt);
				fprintf(f, "\t%s bx, cl\n", shiftop);
				fprintf(f, "\tmov %s, bx\n", dstname);
				fprintf(f, "\tpop bx\n");
			} else {
				fprintf(f, "\tpush cx\n");
				fprintf(f, "\tmov cl, %"PRIi64"\n", imm_cnt);
				fprintf(f, "\t%s %s, cl\n", shiftop, dstname);
				fprintf(f, "\tpop cx\n");
			}
		} else {
			/* Non-immediate count: must come through CL.  Save CX
			 * around the shift to preserve any unrelated live value
			 * (and use BX as scratch when dst==CX). */
			if (dst_is_cx) {
				fprintf(f, "\tpush bx\n");
				fprintf(f, "\tmov bx, %s\n", dstname);
				if (rtype(r1) == RTmp && r1.val != RCX)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, [bp%+ld]\n",
						(long)slot(r1, fn));
				fprintf(f, "\t%s bx, cl\n", shiftop);
				fprintf(f, "\tmov %s, bx\n", dstname);
				fprintf(f, "\tpop bx\n");
			} else {
				fprintf(f, "\tpush cx\n");
				if (rtype(r1) == RTmp && r1.val != RCX)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, [bp%+ld]\n",
						(long)slot(r1, fn));
				fprintf(f, "\t%s %s, cl\n", shiftop, dstname);
				fprintf(f, "\tpop cx\n");
			}
		}
		return;
	}

	/* Oaddr Kl with slot dest: lea computes a 16-bit offset, but the
	 * destination is a 32-bit slot.  Stage through AX, write low half
	 * into slot, and the segment into the high half:
	 *   - near-data models (tiny/small/medium): zero (DS- and SS-relative
	 *     offsets carry an implicit segment, the far value isn't really
	 *     used as a far pointer);
	 *   - far-data models (compact/large/huge): SS, because the address
	 *     is necessarily of a stack slot (which is SS-relative) and the
	 *     language-level `&local` operator now yields a far pointer that
	 *     dereferences must honour. */
	if (i->op == Oaddr && i->cls == Kl && rtype(i->to) == RSlot) {
		Ref dst = i->to;
		int dst_lo = (int)slot(dst, fn);
		int far_data = (T.memmodel == Mcompact ||
		                T.memmodel == Mlarge ||
		                T.memmodel == Mhuge);
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\tlea ax, ");
		emit_memref(i->arg[0], fn, f);
		fputc('\n', f);
		fprintf(f, "\tmov word [bp%+d], ax\n", dst_lo);
		if (far_data) {
			fprintf(f, "\tmov ax, ss\n");
			fprintf(f, "\tmov word [bp%+d], ax\n", dst_lo + 2);
		} else {
			fprintf(f, "\tmov word [bp%+d], 0\n", dst_lo + 2);
		}
		fprintf(f, "\tpop ax\n");
		return;
	}

	/* Ocopy Kw with an immediate source into a memory (slot) destination.
	 * The generic `mov %=, %0` template emits `mov [bp-N], <imm>` with no
	 * size qualifier; for a relocatable address (`mov [bp-N], _sym+off`,
	 * from a `=w add $sym, off` folded to a copy) or a bare integer, NASM
	 * can't size the operand and the OBJ writer rejects the relocation
	 * ("OBJ format can only handle 16- or 32-bit relocations").  A memory
	 * destination needs an explicit `word`; a register dest does not.  No
	 * scratch register is touched, so rega's allocation is unaffected. */
	if (i->op == Ocopy && i->cls == Kw
	    && rtype(i->to) == RSlot && rtype(i->arg[0]) == RCon) {
		Con *pc = &fn->con[i->arg[0].val];
		fprintf(f, "\tmov word [bp%+ld], ", (long)slot(i->to, fn));
		if (pc->type == CAddr)
			emitaddr(pc, f);
		else
			fprintf(f, "%"PRIi64, (int64_t)(int16_t)pc->bits.i);
		fputc('\n', f);
		return;
	}

	/* Special handling for 32-bit (Kl) operations on 16-bit hardware.
	 * Ostorel reaches here even though its result class is Kw (void) —
	 * the data IS 32-bit, so we need the multi-word path.  Same for
	 * Oc*l comparisons: result is Kw but the args are 32-bit.
	 *
	 * Oaddr is excluded: the address-of (lea) of a fast-local slot is
	 * a 16-bit offset (high half is implicitly 0 for DS-relative
	 * pointers in small/medium model).  Let it fall through to the
	 * format-string `lea %=, %M0` template — rega allocates a single
	 * register and the omap entry is `Ki` (matches both Kw and Kl). */
	if ((i->cls == Kl && i->op != Oaddr && i->op != Oloadfl) || i->op == Ostorel
	    || INRANGE(i->op, Oceql, Ocultl)) {
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
			 *
			 * AX/DX are used as scratch unconditionally.  rega doesn't
			 * model the implicit DX:AX clobber, so preserve whichever
			 * isn't the destination — same pattern as Ostorel/Ocopy Kl.
			 */
			{
			int dst_in_ax = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx = (rtype(i->to) == RTmp && i->to.val == RDX);
			int save_ax = !dst_in_ax;
			int save_dx = !dst_in_dx;
			ArgStage r1s = kl_stage_arg(r1, r0, i->to, f);
			if (save_ax) fprintf(f, "\tpush ax\n");
			if (save_dx) fprintf(f, "\tpush dx\n");

			/* Load src0 low word to AX */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				load32_axdx_con(&fn->con[r0.val], f);
			} else if (rtype(r0) == RTmp) {
				/* Register - assume it's actually a slot reference */
				{ if (strcmp(rname[r0.val], "ax") != 0) fprintf(f, "\tmov ax, %s\n", rname[r0.val]); }
				fprintf(f, "\txor dx, dx\n");  /* Extend to 32-bit */
			}

			/* Add src1 */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tadd ax, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tadc dx, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				Con *pc = &fn->con[r1.val];
				if (pc->type == CAddr) {
					/* `add ax, sym+addend` carries an OFFSET fixup;
					 * `adc dx, seg sym` carries a BASE-SEGMENT fixup
					 * — both resolved by omf_link at MZ assembly. */
					fprintf(f, "\tadd ax, ");
					emitaddr(pc, f);
					fputc('\n', f);
					fprintf(f, "\tadc dx, seg ");
					fputs(T.assym, f);
					fputs(str(pc->sym.id), f);
					fputc('\n', f);
				} else {
					int64_t val = pc->bits.i;
					fprintf(f, "\tadd ax, %d\n", (int)(val & 0xFFFF));
					fprintf(f, "\tadc dx, %d\n", (int)((val >> 16) & 0xFFFF));
				}
			} else if (rtype(r1) == RTmp) {
				const char *r1n = r1s.scratch_reg ? r1s.scratch_reg : rname[r1.val];
				fprintf(f, "\tadd ax, %s\n", r1n);
				fprintf(f, "\tadc dx, 0\n");
			}

			/* Store result to destination */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				/* For register destination, we can only store low word.
				 * If dst is DX, copy AX (the low word) into DX before
				 * the pop dx restores DX's prior value. */
				if (dst_in_dx) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			if (save_dx) fprintf(f, "\tpop dx\n");
			if (save_ax) fprintf(f, "\tpop ax\n");
			kl_unstage_arg(r1s, f);
			}
			return;

		case Osub:
			/*
			 * 32-bit subtraction: dest = src0 - src1
			 * sub low words, then sbb high words.
			 * Preserve AX/DX across the op (rega doesn't see the implicit
			 * scratch — same shape as Oadd Kl).
			 */
			{
			int dst_in_dx_sub = (rtype(i->to) == RTmp && i->to.val == RDX);
			ArgStage r1s = kl_stage_arg(r1, r0, i->to, f);
			AxDxSave s_sub = kl_save_axdx(i->to, f);

			/* Load src0 to DX:AX */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				load32_axdx_con(&fn->con[r0.val], f);
			} else if (rtype(r0) == RTmp) {
				{ if (strcmp(rname[r0.val], "ax") != 0) fprintf(f, "\tmov ax, %s\n", rname[r0.val]); }
				fprintf(f, "\txor dx, dx\n");
			}

			/* Subtract src1 */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tsub ax, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tsbb dx, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				Con *pc = &fn->con[r1.val];
				if (pc->type == CAddr) {
					fprintf(f, "\tsub ax, ");
					emitaddr(pc, f);
					fputc('\n', f);
					fprintf(f, "\tsbb dx, seg ");
					fputs(T.assym, f);
					fputs(str(pc->sym.id), f);
					fputc('\n', f);
				} else {
					int64_t val = pc->bits.i;
					fprintf(f, "\tsub ax, %d\n", (int)(val & 0xFFFF));
					fprintf(f, "\tsbb dx, %d\n", (int)((val >> 16) & 0xFFFF));
				}
			} else if (rtype(r1) == RTmp) {
				const char *r1n = r1s.scratch_reg ? r1s.scratch_reg : rname[r1.val];
				fprintf(f, "\tsub ax, %s\n", r1n);
				fprintf(f, "\tsbb dx, 0\n");
			}

			/* Store result */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				/* dst is a register: it gets the low word (AX).  If dst
				 * is DX, the value lives in AX right now — move before
				 * the pop dx (which would otherwise clobber it).  We
				 * skipped pushing DX in that case, so this is just the
				 * final landing of the result. */
				if (dst_in_dx_sub) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_sub, f);
			kl_unstage_arg(r1s, f);
			}
			return;

		case Omul:
			/*
			 * 32-bit multiplication: dest = src0 * src1
			 * imul writes DX:AX unconditionally; preserve AX/DX across.
			 */
			{
			int dst_in_dx_mul = (rtype(i->to) == RTmp && i->to.val == RDX);
			ArgStage r1s = kl_stage_arg(r1, r0, i->to, f);
			AxDxSave s_mul = kl_save_axdx(i->to, f);

			/* Load src0 low word to AX.  CAddr is rejected: multiplying
			 * a pointer is C-illegal, so this path is unreachable from
			 * realistic frontend output.  bits.i for a CAddr is just the
			 * addend (segment lives in the relocation), so silently
			 * accepting it would drop the seg word and produce wrong
			 * results — die() makes the bug loud instead.  Same family
			 * of CAddr-portability fixes as Oadd/Osub/Oand/Oor/Oxor/cmp/
			 * push: see [[caddr-arith-portable]] and siblings. */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				if (fn->con[r0.val].type == CAddr)
					die("i8086: Omul Kl with CAddr arg0 — pointer multiplication is not a valid C operation");
				int64_t val = fn->con[r0.val].bits.i;
				fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
			} else if (rtype(r0) == RTmp) {
				{ if (strcmp(rname[r0.val], "ax") != 0) fprintf(f, "\tmov ax, %s\n", rname[r0.val]); }
			}

			/* Multiply by src1 low word (result in DX:AX) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\timul word [bp%+ld]\n", (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				if (fn->con[r1.val].type == CAddr)
					die("i8086: Omul Kl with CAddr arg1 — pointer multiplication is not a valid C operation");
				int64_t val = fn->con[r1.val].bits.i;
				/* Hoist the constant through BX with save/restore: BX may
				 * hold a live SSA temp that rega doesn't expect us to
				 * clobber.  Mirrors the Kw Omul const path (emit.c:3488)
				 * and the same pattern used by Oadd/Osub Kl. */
				fprintf(f, "\tpush bx\n");
				fprintf(f, "\tmov bx, %d\n", (int)(val & 0xFFFF));
				fprintf(f, "\timul bx\n");
				fprintf(f, "\tpop bx\n");
			} else if (rtype(r1) == RTmp) {
				const char *r1n = r1s.scratch_reg ? r1s.scratch_reg : rname[r1.val];
				fprintf(f, "\timul %s\n", r1n);
			}

			/* Store result */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				if (dst_in_dx_mul) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_mul, f);
			kl_unstage_arg(r1s, f);
			}
			return;

		case Oand:
			/*
			 * 32-bit bitwise AND: dest = src0 & src1
			 */
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tand ax, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tand dx, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				emit32_logop_axdx_con("and", &fn->con[r1.val], f);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			}
			return;

		case Oor:
			/*
			 * 32-bit bitwise OR: dest = src0 | src1
			 */
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\tor ax, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tor dx, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				emit32_logop_axdx_con("or", &fn->con[r1.val], f);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			}
			return;

		case Oxor:
			/*
			 * 32-bit bitwise XOR: dest = src0 ^ src1
			 */
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RSlot) {
				fprintf(f, "\txor ax, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\txor dx, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
			} else if (rtype(r1) == RCon) {
				emit32_logop_axdx_con("xor", &fn->con[r1.val], f);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			}
			return;

		case Oshl:
			/*
			 * 32-bit left shift.  load32_dxax clobbers AX/DX; the
			 * loop body clobbers CX (loop counter).  rega doesn't
			 * model any of these implicit scratch uses, so wrap
			 * with AX/DX save bracket + push/pop cx — same shape
			 * as the Oadd/Osub/Omul Kl fix
			 * ([[i8086-kl-add-sub-mul-r1-alias]]) plus a CX layer.
			 *
			 * Extra wrinkle: r1 (shift count) is Kw, not Kl.  If
			 * r1 RTmp lives in AX/DX, capture it into CX BEFORE
			 * load32_dxax clobbers AX/DX.  If r1 lives in CX, the
			 * standard "mov cx, r1" is a no-op.
			 */
			{
			int dst_in_ax_shl = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx_shl = (rtype(i->to) == RTmp && i->to.val == RDX);
			int dst_in_cx_shl = (rtype(i->to) == RTmp && i->to.val == RCX);
			int save_cx = !dst_in_cx_shl;
			int r1_in_axdx = (rtype(r1) == RTmp
			    && (r1.val == RAX || r1.val == RDX));

			if (save_cx) fprintf(f, "\tpush cx\n");
			if (r1_in_axdx)
				fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
			AxDxSave s_shl = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: low word becomes 0, high = low << (n-16) */
					fprintf(f, "\tmov dx, ax\n");
					fprintf(f, "\txor ax, ax\n");
					emit_shift_imm("shl", "dx", shift - 16, f);
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
				if (rtype(r1) == RTmp) {
					if (!r1_in_axdx
					    && strcmp(rname[r1.val], "cx") != 0)
						fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
					/* r1 in AX/DX: already captured to CX above.
					 * r1 in CX: mov cx, cx no-op, skip. */
				} else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_shl32_done_%p\n", (void*)i);
				fprintf(f, ".L_shl32_%p:\n", (void*)i);
				fprintf(f, "\tshl ax, 1\n");
				fprintf(f, "\trcl dx, 1\n");
				fprintf(f, "\tloop .L_shl32_%p\n", (void*)i);
				fprintf(f, ".L_shl32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				/* Move result low (AX) to dst BEFORE the pops
				 * restore the saved registers. */
				if (dst_in_dx_shl) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (!dst_in_ax_shl) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_shl, f);
			if (save_cx) fprintf(f, "\tpop cx\n");
			}
			return;

		case Oshr:
			/*
			 * 32-bit logical right shift (unsigned).  Same
			 * AX/DX/CX-clobber bracketing as Oshl.
			 */
			{
			int dst_in_ax_shr = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx_shr = (rtype(i->to) == RTmp && i->to.val == RDX);
			int dst_in_cx_shr = (rtype(i->to) == RTmp && i->to.val == RCX);
			int save_cx = !dst_in_cx_shr;
			int r1_in_axdx = (rtype(r1) == RTmp
			    && (r1.val == RAX || r1.val == RDX));

			if (save_cx) fprintf(f, "\tpush cx\n");
			if (r1_in_axdx)
				fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
			AxDxSave s_shr = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: high word becomes 0, low = high >> (n-16) */
					fprintf(f, "\tmov ax, dx\n");
					fprintf(f, "\txor dx, dx\n");
					emit_shift_imm("shr", "ax", shift - 16, f);
				} else if (shift > 0) {
					fprintf(f, "\tmov cx, %d\n", shift);
					fprintf(f, ".L_shr32_%p:\n", (void*)i);
					fprintf(f, "\tshr dx, 1\n");
					fprintf(f, "\trcr ax, 1\n");
					fprintf(f, "\tloop .L_shr32_%p\n", (void*)i);
				}
			} else {
				if (rtype(r1) == RTmp) {
					if (!r1_in_axdx
					    && strcmp(rname[r1.val], "cx") != 0)
						fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				} else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_shr32_done_%p\n", (void*)i);
				fprintf(f, ".L_shr32_%p:\n", (void*)i);
				fprintf(f, "\tshr dx, 1\n");
				fprintf(f, "\trcr ax, 1\n");
				fprintf(f, "\tloop .L_shr32_%p\n", (void*)i);
				fprintf(f, ".L_shr32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				if (dst_in_dx_shr) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (!dst_in_ax_shr) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_shr, f);
			if (save_cx) fprintf(f, "\tpop cx\n");
			}
			return;

		case Osar:
			/*
			 * 32-bit arithmetic right shift (signed).  Same
			 * AX/DX/CX-clobber bracketing as Oshl.
			 */
			{
			int dst_in_ax_sar = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx_sar = (rtype(i->to) == RTmp && i->to.val == RDX);
			int dst_in_cx_sar = (rtype(i->to) == RTmp && i->to.val == RCX);
			int save_cx = !dst_in_cx_sar;
			int r1_in_axdx = (rtype(r1) == RTmp
			    && (r1.val == RAX || r1.val == RDX));

			if (save_cx) fprintf(f, "\tpush cx\n");
			if (r1_in_axdx)
				fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
			AxDxSave s_sar = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);

			if (rtype(r1) == RCon) {
				int shift = (int)fn->con[r1.val].bits.i;
				if (shift >= 16) {
					/* Shift by 16+: low = high >> (n-16), sign-extend.
					 * `mov ax, dx; cwd` puts sign(dx) into DX:AX so DX
					 * becomes the sign mask (-1 or 0).  8086-safe — vs
					 * the prior `sar dx, 15` which is 80186+. */
					fprintf(f, "\tmov ax, dx\n");
					fprintf(f, "\tcwd\n");
					emit_shift_imm("sar", "ax", shift - 16, f);
				} else if (shift > 0) {
					fprintf(f, "\tmov cx, %d\n", shift);
					fprintf(f, ".L_sar32_%p:\n", (void*)i);
					fprintf(f, "\tsar dx, 1\n");
					fprintf(f, "\trcr ax, 1\n");
					fprintf(f, "\tloop .L_sar32_%p\n", (void*)i);
				}
			} else {
				if (rtype(r1) == RTmp) {
					if (!r1_in_axdx
					    && strcmp(rname[r1.val], "cx") != 0)
						fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				} else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, word [bp%+ld]\n", (long)slot(r1, fn));
				fprintf(f, "\tjcxz .L_sar32_done_%p\n", (void*)i);
				fprintf(f, ".L_sar32_%p:\n", (void*)i);
				fprintf(f, "\tsar dx, 1\n");
				fprintf(f, "\trcr ax, 1\n");
				fprintf(f, "\tloop .L_sar32_%p\n", (void*)i);
				fprintf(f, ".L_sar32_done_%p:\n", (void*)i);
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				if (dst_in_dx_sar) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (!dst_in_ax_sar) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_sar, f);
			if (save_cx) fprintf(f, "\tpop cx\n");
			}
			return;

		case Ocopy:
			/*
			 * 32-bit copy.
			 *
			 * The naive AX/DX scratch sequence (load value into DX:AX,
			 * then store) clobbers AX and DX, which rega doesn't know
			 * about because Ocopy's only declared operands are r0 and
			 * i->to.  In parallel-move blocks rega frequently emits a
			 * sequence like `R1 =w copy R<x>; ...; S<y> =l copy R3`
			 * — the second copy's scratch then wipes the first's
			 * destination, corrupting whatever value rega had placed
			 * in AX or DX before this op.
			 *
			 * Fast path for Con → Slot: `mov word [mem], imm16` doesn't
			 * need any register at all.
			 */
			if (rtype(r0) == RCon && rtype(i->to) == RSlot) {
				Con *pc = &fn->con[r0.val];
				if (pc->type == CAddr) {
					/* Far-symbol value: low half = offset (sym+addend),
					 * high half = seg sym.  Both relocations resolve
					 * via NASM's symbol/seg operators. */
					fprintf(f, "\tmov word [bp%+ld], ",
						(long)slot(i->to, fn));
					emitaddr(pc, f);
					fputc('\n', f);
					fprintf(f, "\tmov word [bp%+ld], seg ",
						(long)slot(i->to, fn) + 2);
					fputs(T.assym, f);
					fputs(str(pc->sym.id), f);
					fputc('\n', f);
				} else {
					int64_t val = pc->bits.i;
					fprintf(f, "\tmov word [bp%+ld], %d\n",
						(long)slot(i->to, fn), (int)(val & 0xFFFF));
					fprintf(f, "\tmov word [bp%+ld], %d\n",
						(long)slot(i->to, fn) + 2, (int)((val >> 16) & 0xFFFF));
				}
				return;
			}

			/* For every other shape we route through AX:DX.  Preserve
			 * whichever isn't already participating in the copy. */
			{
			int src_in_ax = (rtype(r0) == RTmp && r0.val == RAX);
			int src_in_dx = (rtype(r0) == RTmp && r0.val == RDX);
			int dst_in_ax = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx = (rtype(i->to) == RTmp && i->to.val == RDX);
			int save_ax = !src_in_ax && !dst_in_ax;
			int save_dx = !src_in_dx && !dst_in_dx;
			if (save_ax) fprintf(f, "\tpush ax\n");
			if (save_dx) fprintf(f, "\tpush dx\n");

			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				load32_axdx_con(&fn->con[r0.val], f);
			} else if (rtype(r0) == RTmp) {
				/* RTmp source for a Kl Ocopy: rega doesn't pair Kl
				 * temps, so only the low word lives in a register.
				 * The previous `cwd` sign-extension was wrong for
				 * the canonical use case (staging a DX:AX call
				 * return into a slot — DX already holds the high
				 * word and cwd would overwrite it).  Preserve DX as
				 * the high half; for non-call sources DX is
				 * undefined but the high word of an unpaired Kl
				 * temp is undefined anyway. */
				{ if (strcmp(rname[r0.val], "ax") != 0) fprintf(f, "\tmov ax, %s\n", rname[r0.val]); }
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			}

			if (save_dx) fprintf(f, "\tpop dx\n");
			if (save_ax) fprintf(f, "\tpop ax\n");
			}
			return;

		case Oload:
			/*
			 * 32-bit load from memory.  Uses AX:DX to stage the value;
			 * preserve them across so rega-allocated live tmps in AX/DX
			 * survive (they otherwise leak silently — the canonical Kl
			 * implicit-clobber bug).
			 *
			 * Phase B' fix: when r0 is RSlot, the slot HOLDS a
			 * pointer VALUE — same invariant as the Ostorel handler.
			 * Spill.c's force_kl_slot evicts every Kl temp to a slot
			 * AND its reload pass skips Kl temps entirely (see
			 * spill.c::reloads + the force_kl_slot guard), so any
			 * Oload Kl with arg[0]=RSlot in the emit stream is from
			 * the frontend and means "load through a pointer stored
			 * in this slot," NOT "reload a value from its spill
			 * slot."  Dereference via BX (and ES under far-data).
			 */
			{
			int dst_in_dx_ld = (rtype(i->to) == RTmp && i->to.val == RDX);
			int dst_in_bx_ld = (rtype(i->to) == RTmp && i->to.val == RBX);
			int addr_in_bx_ld =
			    (rtype(r0) == RTmp && r0.val == RBX) ||
			    (rtype(r0) == RMem && !req(fn->mem[r0.val].base, R)
			     && rtype(fn->mem[r0.val].base) == RTmp
			     && fn->mem[r0.val].base.val == RBX);
			/* Phase B' deref only when the slot is a spilled-tmp slot
			 * (slot index >= arg_slot_top OR < 0).  Slot index < 0 is
			 * a selpar incoming-param slot (above BP) — those are
			 * direct memory, not pointer-bearing.  Slot indices in
			 * [0, arg_slot_top) are selcall outgoing-arg slots — also
			 * direct memory.  Everything else (alloca-via-Oaddr-spill
			 * + spill.c-evicted Kl tmps) HOLDS a pointer.  Note that
			 * the only Oload Kl with RSlot from selpar uses Kl params
			 * (caller's Kl arg sitting in the param region) — those
			 * stay direct-read by the slot_index<0 gate. */
			int slot_src_ld = (rtype(r0) == RSlot);
			int slot_src_idx_ld = slot_src_ld ? rsval(r0) : 0;
			int slot_src_deref_ld = slot_src_ld
			                && slot_src_idx_ld >= 0
			                && slot_src_idx_ld >= fn->arg_slot_top;
			int far_data_ld = (T.memmodel == Mcompact ||
			                   T.memmodel == Mlarge ||
			                   T.memmodel == Mhuge);
			int needs_bx_ld = (rtype(r0) == RTmp || rtype(r0) == RMem
			                   || slot_src_deref_ld);
			int needs_es_ld = slot_src_deref_ld && far_data_ld;
			/* BX is used as the address-staging scratch.  Save/restore
			 * unless the address IS in BX (we use it directly) or the
			 * destination is BX (we'll overwrite it anyway). */
			int save_bx_ld = needs_bx_ld && !addr_in_bx_ld && !dst_in_bx_ld;
			AxDxSave s_ld = kl_save_axdx(i->to, f);
			if (save_bx_ld) fprintf(f, "\tpush bx\n");
			if (needs_es_ld) fprintf(f, "\tpush es\n");

			/* Memory address is in arg[0] */
			if (rtype(r0) == RSlot) {
				if (slot_src_deref_ld) {
					/* Spilled-Kl-ptr slot: slot HOLDS a pointer
					 * value.  Load it into BX (and ES under
					 * far-data) and read the 32-bit value through
					 * [ES:BX] / [BX]. */
					fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r0, fn));
					if (far_data_ld) {
						fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
						fprintf(f, "\tmov ax, word ptr es:[bx]\n");
						fprintf(f, "\tmov dx, word ptr es:[bx+2]\n");
					} else {
						fprintf(f, "\tmov ax, word [bx]\n");
						fprintf(f, "\tmov dx, word [bx+2]\n");
					}
				} else {
					/* ABI-direct slot (incoming Kl param or call-
					 * arg area): slot IS the source storage.  Read
					 * its 4 bytes directly. */
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
					fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				}
			} else if (rtype(r0) == RTmp) {
				/* Load from address in register */
				if (strcmp(rname[r0.val], "bx") != 0)
					fprintf(f, "\tmov bx, %s\n", rname[r0.val]);
				fprintf(f, "\tmov ax, word [bx]\n");
				fprintf(f, "\tmov dx, word [bx+2]\n");
			} else if (rtype(r0) == RMem) {
				/* Complex addressing mode */
				Mem *m = &fn->mem[r0.val];
				if (!req(m->base, R) && rtype(m->base) == RTmp) {
					if (strcmp(rname[m->base.val], "bx") != 0)
						fprintf(f, "\tmov bx, %s\n", rname[m->base.val]);
					if (m->offset.type == CBits) {
						fprintf(f, "\tmov ax, word [bx+%"PRIi64"]\n", m->offset.bits.i);
						fprintf(f, "\tmov dx, word [bx+%"PRIi64"]\n", m->offset.bits.i + 2);
					} else {
						fprintf(f, "\tmov ax, word [bx]\n");
						fprintf(f, "\tmov dx, word [bx+2]\n");
					}
				}
			} else if (rtype(r0) == RCon) {
				/* 32-bit load from a constant address.  For a symbol
				 * (CAddr) this is the canonical `loadl $glo` shape used
				 * by minic for global `long`s — emit direct memory
				 * reads.  For a numeric constant address, treat as an
				 * absolute address in the data segment. */
				Con *pc = &fn->con[r0.val];
				if (pc->type == CAddr) {
					fprintf(f, "\tmov ax, word [");
					emitaddr(pc, f);
					fprintf(f, "]\n");
					fprintf(f, "\tmov dx, word [");
					emitaddr(pc, f);
					fprintf(f, "+2]\n");
				} else {
					fprintf(f, "\tmov ax, word [%"PRIi64"]\n",
					        pc->bits.i);
					fprintf(f, "\tmov dx, word [%"PRIi64"]\n",
					        pc->bits.i + 2);
				}
			}

			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				if (dst_in_dx_ld) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			if (needs_es_ld) fprintf(f, "\tpop es\n");
			if (save_bx_ld) fprintf(f, "\tpop bx\n");
			kl_restore_axdx(s_ld, f);
			}
			return;

		case Ostorel:
			/*
			 * 32-bit store to memory
			 * arg[0] = value to store, arg[1] = destination address
			 *
			 * 32-bit stores on the 8086 unavoidably go through AX:DX
			 * (the only word-pair the architecture supports for
			 * adjacent loads/stores).  rega does not model that AX/DX
			 * are clobbered by every multi-word op, so we preserve
			 * them around the sequence — same pattern as the `imul`
			 * workaround above.  Skip the save if the source already
			 * lives in AX or DX (it's being read, not corrupted).
			 *
			 * Phase B' fix: when r1 is RSlot, the slot HOLDS a
			 * pointer VALUE — per spill.c's force_kl_slot invariant,
			 * every Kl temp the spill pass evicts to a slot stores
			 * its computed value there (a 4-byte far pointer under
			 * compact/large/huge, a 2-byte near pointer under
			 * tiny/small/medium).  Load that pointer from the slot
			 * into BX (and ES under far-data) and dereference
			 * through [ES:BX] / [BX].  Previously the handler wrote
			 * AX:DX directly to [bp+slot(r1)], which only matches
			 * "slot IS storage" — never true after isel's Oaddr
			 * rewrite of alloca tmps.  Latent because QBE constant-
			 * folded realistic `*p = q` shapes; surfaced by Phase B's
			 * opaque `_qbe_huge_add` insertions.  See [[huge-phase-b
			 * -storel-gap]] and minic/dos/examples/phase_bprime_probe.c.
			 */
			{
			int src_in_ax = (rtype(r0) == RTmp && r0.val == RAX);
			int src_in_dx = (rtype(r0) == RTmp && r0.val == RDX);
			int src_in_bx = (rtype(r0) == RTmp && r0.val == RBX);
			int addr_in_bx =
			    (rtype(r1) == RTmp && r1.val == RBX) ||
			    (rtype(r1) == RMem && !req(fn->mem[r1.val].base, R)
			     && rtype(fn->mem[r1.val].base) == RTmp
			     && fn->mem[r1.val].base.val == RBX);
			/* Phase B' deref only when the slot is a spilled-tmp slot.
			 * ABI's selcall writes call args directly into slot indices
			 * [0, fn->arg_slot_top); those slots ARE the destination
			 * memory.  Everything else with an RSlot dest is a Kl tmp
			 * that spill.c evicted to a slot — its slot HOLDS a
			 * pointer.  See arg_slot_top in all.h for the layout. */
			int slot_dest = (rtype(r1) == RSlot);
			int slot_dest_idx = slot_dest ? rsval(r1) : 0;
			int slot_dest_deref = slot_dest
			                && slot_dest_idx >= 0
			                && slot_dest_idx >= fn->arg_slot_top;
			int far_data = (T.memmodel == Mcompact ||
			                T.memmodel == Mlarge ||
			                T.memmodel == Mhuge);
			int needs_bx = (rtype(r1) == RTmp || rtype(r1) == RMem
			                || slot_dest_deref);
			/* RSlot deref dest needs ES under far-data: the slot holds
			 * a 4-byte far pointer; segment goes to ES so the
			 * dereference is `es:[bx]`. */
			int needs_es = slot_dest_deref && far_data;
			int save_ax = !src_in_ax;
			int save_dx = !src_in_dx;
			/* BX is used as the destination-address scratch register; if
			 * rega placed any live tmp in BX (and r1's reg isn't BX
			 * itself), we must save/restore it.  Skip if the source IS
			 * in BX (then it's being read, not corrupted by us). */
			int save_bx = needs_bx && !addr_in_bx && !src_in_bx;
			if (save_ax) fprintf(f, "\tpush ax\n");
			if (save_dx) fprintf(f, "\tpush dx\n");
			if (save_bx) fprintf(f, "\tpush bx\n");
			if (needs_es) fprintf(f, "\tpush es\n");

			if (slot_dest_deref) {
				/* Spilled-Kl-ptr slot dest: load r0 → AX:DX FIRST
				 * (in case r0 RTmp aliases BX), then load far
				 * pointer from slot(r1) into BX:ES.  `mov es, mem`
				 * is a single-instruction load from memory on the
				 * 8086, so no scratch register is needed for the
				 * segment word. */
				if (rtype(r0) == RSlot) {
					/* r0's slot itself: same arg_slot_top check —
					 * a value-source slot (alloca / spilled non-
					 * pointer Kl value) is direct-read; a spilled
					 * Kl ptr would need deref but storel's arg[0]
					 * is the VALUE, not a deref source, so direct
					 * read is correct here. */
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
					fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				} else if (rtype(r0) == RCon) {
					load32_axdx_con(&fn->con[r0.val], f);
				} else if (rtype(r0) == RTmp) {
					if (strcmp(rname[r0.val], "ax") != 0)
						fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
					fprintf(f, "\tcwd\n");
				}
				fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r1, fn));
				if (far_data) {
					fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r1, fn) + 2);
					fprintf(f, "\tmov word ptr es:[bx], ax\n");
					fprintf(f, "\tmov word ptr es:[bx+2], dx\n");
				} else {
					fprintf(f, "\tmov word [bx], ax\n");
					fprintf(f, "\tmov word [bx+2], dx\n");
				}
			} else {
				/* RTmp/RMem/RCon dest: existing path —
				 * capture destination address into BX BEFORE
				 * the value load clobbers AX/DX (otherwise, if
				 * rega placed r1's register in AX or DX, the
				 * value mov would destroy the address and the
				 * store would land at the wrong place).  Found
				 * by Stevie's `Fileend->linep->num = 0xffff`
				 * silently writing to address 0xFFFF instead
				 * of &num, leaving every line-number field as
				 * malloc-zero. */
				if (rtype(r1) == RTmp) {
					if (strcmp(rname[r1.val], "bx") != 0)
						fprintf(f, "\tmov bx, %s\n", rname[r1.val]);
				} else if (rtype(r1) == RMem) {
					Mem *m = &fn->mem[r1.val];
					if (!req(m->base, R) && rtype(m->base) == RTmp
					    && strcmp(rname[m->base.val], "bx") != 0)
						fprintf(f, "\tmov bx, %s\n", rname[m->base.val]);
				}

				/* Load value to store */
				if (rtype(r0) == RSlot) {
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
					fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				} else if (rtype(r0) == RCon) {
					load32_axdx_con(&fn->con[r0.val], f);
				} else if (rtype(r0) == RTmp) {
					if (strcmp(rname[r0.val], "ax") != 0)
						fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
					fprintf(f, "\tcwd\n");
				}

				/* Store to destination */
				if (slot_dest) {
					/* Direct slot dest (slot index in
					 * [0, arg_slot_top) — ABI's selcall write
					 * target).  The slot IS the destination
					 * memory; write 4 bytes into it. */
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(r1, fn));
					fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(r1, fn) + 2);
				} else if (rtype(r1) == RTmp) {
					fprintf(f, "\tmov word [bx], ax\n");
					fprintf(f, "\tmov word [bx+2], dx\n");
				} else if (rtype(r1) == RMem) {
					Mem *m = &fn->mem[r1.val];
					if (!req(m->base, R) && rtype(m->base) == RTmp) {
						if (m->offset.type == CBits) {
							fprintf(f, "\tmov word [bx+%"PRIi64"], ax\n", m->offset.bits.i);
							fprintf(f, "\tmov word [bx+%"PRIi64"], dx\n", m->offset.bits.i + 2);
						} else {
							fprintf(f, "\tmov word [bx], ax\n");
							fprintf(f, "\tmov word [bx+2], dx\n");
						}
					}
				} else if (rtype(r1) == RCon) {
					/* 32-bit store to a constant destination address.
					 * For a symbol (CAddr) this is `storel _, $glo` —
					 * minic emits it whenever a function writes a
					 * global `long`. */
					Con *pc = &fn->con[r1.val];
					if (pc->type == CAddr) {
						fprintf(f, "\tmov word [");
						emitaddr(pc, f);
						fprintf(f, "], ax\n");
						fprintf(f, "\tmov word [");
						emitaddr(pc, f);
						fprintf(f, "+2], dx\n");
					} else {
						fprintf(f, "\tmov word [%"PRIi64"], ax\n",
						        pc->bits.i);
						fprintf(f, "\tmov word [%"PRIi64"], dx\n",
						        pc->bits.i + 2);
					}
				}
			}

			if (needs_es) fprintf(f, "\tpop es\n");
			if (save_bx) fprintf(f, "\tpop bx\n");
			if (save_dx) fprintf(f, "\tpop dx\n");
			if (save_ax) fprintf(f, "\tpop ax\n");
			}
			return;

		/* 32-bit comparison operations.
		 *
		 * Each emits load32_dxax (clobbers AX/DX) then cmp32_high/low
		 * (read AX/DX) and finally writes the boolean 0/1 into AX which
		 * store_ax_to routes to the destination.  rega doesn't model the
		 * AX/DX clobber, so live tmps in AX/DX across the compare would
		 * be silently corrupted — wrap each case with kl_save_axdx /
		 * kl_restore_axdx (skips push/pop for whichever scratch reg IS
		 * the destination, since the result must remain there). */
		case Oceql:
			{
			AxDxSave s_ceql = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjne .L_ceql_ne_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjne .L_ceql_ne_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, "\tjmp .L_ceql_done_%p\n", (void*)i);
			fprintf(f, ".L_ceql_ne_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, ".L_ceql_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_ceql, f);
			}
			return;

		case Ocnel:
			{
			AxDxSave s_cnel = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjne .L_cnel_ne_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjne .L_cnel_ne_%p\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cnel_done_%p\n", (void*)i);
			fprintf(f, ".L_cnel_ne_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cnel_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_cnel, f);
			}
			return;

		case Ocsltl:
			{
			AxDxSave s_csltl = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjl .L_csltl_true_%p\n", (void*)i);
			fprintf(f, "\tjg .L_csltl_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjb .L_csltl_true_%p\n", (void*)i);
			fprintf(f, ".L_csltl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csltl_done_%p\n", (void*)i);
			fprintf(f, ".L_csltl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csltl_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_csltl, f);
			}
			return;

		case Ocslel:
			{
			AxDxSave s_cslel = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjl .L_cslel_true_%p\n", (void*)i);
			fprintf(f, "\tjg .L_cslel_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjbe .L_cslel_true_%p\n", (void*)i);
			fprintf(f, ".L_cslel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cslel_done_%p\n", (void*)i);
			fprintf(f, ".L_cslel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cslel_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_cslel, f);
			}
			return;

		case Ocsgtl:
			{
			AxDxSave s_csgtl = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjg .L_csgtl_true_%p\n", (void*)i);
			fprintf(f, "\tjl .L_csgtl_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tja .L_csgtl_true_%p\n", (void*)i);
			fprintf(f, ".L_csgtl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csgtl_done_%p\n", (void*)i);
			fprintf(f, ".L_csgtl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csgtl_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_csgtl, f);
			}
			return;

		case Ocsgel:
			{
			AxDxSave s_csgel = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjg .L_csgel_true_%p\n", (void*)i);
			fprintf(f, "\tjl .L_csgel_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjae .L_csgel_true_%p\n", (void*)i);
			fprintf(f, ".L_csgel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_csgel_done_%p\n", (void*)i);
			fprintf(f, ".L_csgel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_csgel_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_csgel, f);
			}
			return;

		case Ocultl:
			{
			AxDxSave s_cultl = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjb .L_cultl_true_%p\n", (void*)i);
			fprintf(f, "\tja .L_cultl_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjb .L_cultl_true_%p\n", (void*)i);
			fprintf(f, ".L_cultl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cultl_done_%p\n", (void*)i);
			fprintf(f, ".L_cultl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cultl_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_cultl, f);
			}
			return;

		case Oculel:
			{
			AxDxSave s_culel = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tjb .L_culel_true_%p\n", (void*)i);
			fprintf(f, "\tja .L_culel_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjbe .L_culel_true_%p\n", (void*)i);
			fprintf(f, ".L_culel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_culel_done_%p\n", (void*)i);
			fprintf(f, ".L_culel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_culel_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_culel, f);
			}
			return;

		case Ocugtl:
			{
			AxDxSave s_cugtl = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tja .L_cugtl_true_%p\n", (void*)i);
			fprintf(f, "\tjb .L_cugtl_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tja .L_cugtl_true_%p\n", (void*)i);
			fprintf(f, ".L_cugtl_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cugtl_done_%p\n", (void*)i);
			fprintf(f, ".L_cugtl_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cugtl_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_cugtl, f);
			}
			return;

		case Ocugel:
			{
			AxDxSave s_cugel = kl_save_axdx(i->to, f);
			load32_dxax(r0, fn, f);
			cmp32_high(r1, fn, f);
			fprintf(f, "\tja .L_cugel_true_%p\n", (void*)i);
			fprintf(f, "\tjb .L_cugel_false_%p\n", (void*)i);
			cmp32_low(r1, fn, f);
			fprintf(f, "\tjae .L_cugel_true_%p\n", (void*)i);
			fprintf(f, ".L_cugel_false_%p:\n", (void*)i);
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tjmp .L_cugel_done_%p\n", (void*)i);
			fprintf(f, ".L_cugel_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".L_cugel_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			kl_restore_axdx(s_cugel, f);
			}
			return;

		case Omkfar:
			/*
			 * Make far pointer from segment and offset
			 * arg[0] = segment (word), arg[1] = offset (word)
			 * Result: far pointer stored as segment:offset (DX:AX)
			 */
			/* Load segment to DX */
			if (rtype(r0) == RTmp)
				fprintf(f, "\tmov dx, %s\n", rname[r0.val]);
			else if (rtype(r0) == RSlot)
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn));
			else if (rtype(r0) == RCon)
				fprintf(f, "\tmov dx, %d\n", (int)(fn->con[r0.val].bits.i & 0xFFFF));
			/* Load offset to AX */
			if (rtype(r1) == RTmp)
				fprintf(f, "\tmov ax, %s\n", rname[r1.val]);
			else if (rtype(r1) == RSlot)
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r1, fn));
			else if (rtype(r1) == RCon)
				fprintf(f, "\tmov ax, %d\n", (int)(fn->con[r1.val].bits.i & 0xFFFF));
			/* Store to destination if slot */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			}
			return;

		case Oswap:
			/* rega emits Oswap to resolve parallel moves at block
			 * boundaries.  rega doesn't allocate pairs for Kl, so
			 * the temp lives in a single register (low half).  Swap
			 * just that register; the implicit high half (0 for
			 * pointers, sign-extended for longs) follows the value
			 * via the Oadd/Osub Kl handlers' xor/cwd. */
			if (rtype(i->arg[0]) == RTmp && rtype(i->arg[1]) == RTmp)
				fprintf(f, "\txchg %s, %s\n",
				    rname[i->arg[0].val], rname[i->arg[1].val]);
			return;

		case Odiv:
		case Oudiv:
		case Orem:
		case Ourem:
			/* 32-bit divide/remainder via libstub soft helper.
			 * The 8086 has no 32-bit DIV/IDIV; call out to
			 * _qbe_div32u/s / _qbe_rem32u/s instead.  These follow
			 * the cdecl convention used by the rest of libstub:
			 *
			 *   push denom_hi; push denom_lo
			 *   push num_hi  ; push num_lo
			 *   call _qbe_<op>32<u|s>
			 *   add sp, 8
			 *
			 * Helpers preserve BX/SI/DI; AX/CX/DX are caller-save.
			 * Result returns in DX:AX.
			 *
			 * rega doesn't model the call's implicit clobber of
			 * CX (or AX/DX), so save/restore them around the call
			 * — same pattern as kl_save_axdx, extended to CX.
			 */
			{
			int dst_in_ax = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx = (rtype(i->to) == RTmp && i->to.val == RDX);
			int dst_in_cx = (rtype(i->to) == RTmp && i->to.val == RCX);
			int save_ax = !dst_in_ax;
			int save_cx = !dst_in_cx;
			int save_dx = !dst_in_dx;
			const char *helper =
			    i->op == Odiv  ? "_qbe_div32s" :
			    i->op == Oudiv ? "_qbe_div32u" :
			    i->op == Orem  ? "_qbe_rem32s" :
			                     "_qbe_rem32u";
			int farcall = (T.memmodel == Mmedium ||
			               T.memmodel == Mcompact ||
			               T.memmodel == Mlarge  ||
			               T.memmodel == Mhuge);

			/* Save caller-save GPRs that aren't the destination.
			 * Order: push later → pop first; nest CX between
			 * AX/DX so kl_save_axdx idioms still line up. */
			if (save_ax) fprintf(f, "\tpush ax\n");
			if (save_cx) fprintf(f, "\tpush cx\n");
			if (save_dx) fprintf(f, "\tpush dx\n");

			/* Push arg1 (denominator) hi then lo, then arg0 (numerator)
			 * hi then lo, so the helper sees args in cdecl order
			 * (num at lower address). */
			emit_push_long(r1, fn, f);
			emit_push_long(r0, fn, f);

			fprintf(f, "\tcall%s %s\n", farcall ? " far" : "", helper);
			fprintf(f, "\tadd sp, 8\n");

			/* Result in DX:AX.  Move into dst BEFORE restoring
			 * whichever caller-save reg overlaps the dst (its
			 * pre-call value would otherwise overwrite ours).
			 * Helpers don't return the high word for now; truncate
			 * to 16 bits for register destinations and zero the
			 * high half for slot destinations (matches the Oadd Kl
			 * shape — see [[i8086-kl-add-sub-mul-r1-alias]]). */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n",
				    (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n",
				    (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp && !dst_in_ax) {
				if (dst_in_dx)
					fprintf(f, "\tmov dx, ax\n");
				else
					fprintf(f, "\tmov %s, ax\n",
					    rname[i->to.val]);
			}

			if (save_dx) fprintf(f, "\tpop dx\n");
			if (save_cx) fprintf(f, "\tpop cx\n");
			if (save_ax) fprintf(f, "\tpop ax\n");
			}
			return;

		case Oextsw:
		case Oextuw:
			/* Extend 16-bit (Kw) value to 32-bit (Kl) result.
			 * Oextsw: sign-extend (cwd: AX → DX:AX with sign bit).
			 * Oextuw: zero-extend (xor dx, dx).
			 * Load arg into AX first; result in DX:AX, then store.
			 * Preserve AX/DX (rega doesn't model the implicit clobber). */
			{
			int dst_in_ax_ext = (rtype(i->to) == RTmp && i->to.val == RAX);
			int dst_in_dx_ext = (rtype(i->to) == RTmp && i->to.val == RDX);
			AxDxSave s_ext = kl_save_axdx(i->to, f);

			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					/* Address constant — emit symbolic ref so the
					 * linker resolves the offset.  bits.i is an
					 * additive offset that emitaddr already handles. */
					fprintf(f, "\tmov ax, ");
					emitaddr(c, f);
					fprintf(f, "\n");
				} else {
					int64_t val = c->bits.i;
					fprintf(f, "\tmov ax, %d\n", (int)(val & 0xFFFF));
				}
			} else if (rtype(r0) == RTmp) {
				if (strcmp(rname[r0.val], "ax") != 0)
					fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
			}
			if (i->op == Oextsw)
				fprintf(f, "\tcwd\n");
			else
				fprintf(f, "\txor dx, dx\n");
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				/* dst is a register: take the low word (AX).
				 * If dst is DX, copy AX→DX BEFORE pop dx restores.
				 * If dst is AX, the result is already there. */
				if (dst_in_dx_ext) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (!dst_in_ax_ext) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_ext, f);
			}
			return;

		default:
			/* Unsupported 32-bit op.  Falling through to the generic
			 * format-string path would silently emit the 16-bit form
			 * (truncating to the low word) or, for ops without a `to`,
			 * produce malformed output like `xchg , ax` — so abort
			 * loudly instead.  No Kl op reaches this in stevie or any
			 * of the in-tree minic tests as of 2026-05-22; if you hit
			 * it, add an explicit case above (likely shapes:
			 * kl_save_axdx + kl_stage_arg + the AX/DX scratch idiom
			 * used by Oadd/Osub Kl).  See [[i8086-kl-add-sub-mul-r1-alias]]. */
			die("i8086: unsupported 32-bit op %d (cls Kl) — add a case "
			    "in i8086/emit.c's `i->cls == Kl` switch", i->op);
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
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				/* FP constant - need to load from memory */
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					/* Integer constant treated as FP bits */
					die("i8086: FP immediate arg0 unsupported (op %d) — minic should hoist FP literals to data segment", i->op);
				}
			}

			/* Load second operand to ST(0), first becomes ST(1) */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				Con *c = &fn->con[r1.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					die("i8086: FP immediate arg1 unsupported (op %d) — minic should hoist FP literals to data segment", i->op);
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
				fprintf(f, "\tfstp %s [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oneg:
			/*
			 * Unary negation: result = -arg0
			 * Load, change sign, store
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			}
			fprintf(f, "\tfchs\n");
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oload:
			/*
			 * Load FP value from memory to FP stack slot
			 * In register-less mode, we store immediately to destination
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				}
			}
			/* Store to destination slot */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Ostores:
		case Ostored:
			/*
			 * Store FP value from arg0 to memory location in arg1
			 */
			/* Load source to FPU stack */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else {
					/* Store FP constant bits directly */
					die("i8086: store of FP immediate unsupported — minic should hoist FP literals to data segment");
				}
			}
			/* Store to destination address */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\tfstp %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				Con *c = &fn->con[r1.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfstp %s [", szp);
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
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			} else if (rtype(r0) == RCon) {
				Con *c = &fn->con[r0.val];
				if (c->type == CAddr) {
					fprintf(f, "\tfld %s [", szp);
					emitaddr(c, f);
					fprintf(f, "]\n");
				} else if (c->type == CBits) {
					/* FP constant encoded as bits - store to temp then load */
					if (isdbl) {
						int64_t bits = c->bits.i;
						fprintf(f, "\tmov word [bp-2], %d\n", (int)(bits & 0xFFFF));
						fprintf(f, "\tmov word [bp-4], %d\n", (int)((bits >> 16) & 0xFFFF));
						fprintf(f, "\tmov word [bp-6], %d\n", (int)((bits >> 32) & 0xFFFF));
						fprintf(f, "\tmov word [bp-8], %d\n", (int)((bits >> 48) & 0xFFFF));
						fprintf(f, "\tfld qword [bp-8]\n");
					} else {
						int32_t bits = (int32_t)c->bits.i;
						fprintf(f, "\tmov word [bp-2], %d\n", (int)(bits & 0xFFFF));
						fprintf(f, "\tmov word [bp-4], %d\n", (int)((bits >> 16) & 0xFFFF));
						fprintf(f, "\tfld dword [bp-4]\n");
					}
				}
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp %s [bp%+ld]\n", szp, (long)slot(i->to, fn));
			}
			return;

		case Oexts:
			/*
			 * Extend float to double: load as float, store as double
			 * The 8087 internally uses 80-bit extended precision,
			 * so conversion is implicit in load/store sizes
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld dword [bp%+ld]\n", (long)slot(r0, fn));
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp qword [bp%+ld]\n", (long)slot(i->to, fn));
			}
			return;

		case Otruncd:
			/*
			 * Truncate double to float: load as double, store as float
			 */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tfld qword [bp%+ld]\n", (long)slot(r0, fn));
			}
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tfstp dword [bp%+ld]\n", (long)slot(i->to, fn));
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
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));

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

			store_ax_to(i->to, fn, f);
			return;

		case Ocnes:
		case Ocned:
			/* Not equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjne .Lcne_true_%p\n", (void*)i);
			fprintf(f, "\tjmp .Lcne_done_%p\n", (void*)i);
			fprintf(f, ".Lcne_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcne_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			return;

		case Ocgts:
		case Ocgtd:
			/* Greater than: ST(1) > ST(0) after loading arg0, arg1 */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
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
			store_ax_to(i->to, fn, f);
			return;

		case Ocges:
		case Ocged:
			/* Greater or equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjae .Lcge_true_%p\n", (void*)i);  /* above or equal */
			fprintf(f, "\tjmp .Lcge_done_%p\n", (void*)i);
			fprintf(f, ".Lcge_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcge_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			return;

		case Oclts:
		case Ocltd:
			/* Less than */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjb .Lclt_true_%p\n", (void*)i);  /* below = less than */
			fprintf(f, "\tjmp .Lclt_done_%p\n", (void*)i);
			fprintf(f, ".Lclt_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lclt_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			return;

		case Ocles:
		case Ocled:
			/* Less or equal */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			fprintf(f, "\tmov ax, 0\n");
			fprintf(f, "\tjbe .Lcle_true_%p\n", (void*)i);  /* below or equal */
			fprintf(f, "\tjmp .Lcle_done_%p\n", (void*)i);
			fprintf(f, ".Lcle_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, ".Lcle_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			return;

		case Ocos:
		case Ocod:
			/* Ordered (neither is NaN) */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
			fprintf(f, "\tfcompp\n");
			fprintf(f, "\tfstsw ax\n");
			fprintf(f, "\tsahf\n");
			/* Parity flag is set if unordered (NaN) */
			fprintf(f, "\tmov ax, 1\n");
			fprintf(f, "\tjnp .Lcord_done_%p\n", (void*)i);  /* not parity = ordered */
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, ".Lcord_done_%p:\n", (void*)i);
			store_ax_to(i->to, fn, f);
			return;

		case Ocuos:
		case Ocuod:
			/* Unordered (at least one is NaN) */
			if (rtype(r0) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r0, fn));
			if (rtype(r1) == RSlot)
				fprintf(f, "\tfld %s [bp%+ld]\n", szp, (long)slot(r1, fn));
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
			store_ax_to(i->to, fn, f);
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
			fprintf(f, "\tfild word [bp%+ld]\n", (long)slot(r0, fn));
		} else if (rtype(r0) == RTmp) {
			/* Need to store register to temp location first */
			fprintf(f, "\tpush %s\n", rname[r0.val]);
			fprintf(f, "\tfild word [sp]\n");
			fprintf(f, "\tadd sp, 2\n");
		}
		/* Store result based on destination class */
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword [bp%+ld]\n", (long)slot(i->to, fn));
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
			fprintf(f, "\tpush word [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tfild dword [sp]\n");
			fprintf(f, "\tadd sp, 4\n");
		} else if (rtype(r0) == RTmp) {
			fprintf(f, "\tpush word ptr 0\n");
			fprintf(f, "\tpush %s\n", rname[r0.val]);
			fprintf(f, "\tfild dword [sp]\n");
			fprintf(f, "\tadd sp, 4\n");
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Osltof:
		/*
		 * Signed long (32-bit) to float/double
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfild dword [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword [bp%+ld]\n", (long)slot(i->to, fn));
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
			fprintf(f, "\tpush word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			fprintf(f, "\tpush word [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tfild qword [sp]\n");
			fprintf(f, "\tadd sp, 8\n");
		}
		if (rtype(i->to) == RSlot) {
			if (i->cls == Kd)
				fprintf(f, "\tfstp qword [bp%+ld]\n", (long)slot(i->to, fn));
			else
				fprintf(f, "\tfstp dword [bp%+ld]\n", (long)slot(i->to, fn));
		}
		return;

	case Ostosi:
		/*
		 * Float to signed word
		 * fistp stores and pops FP stack as integer
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld dword [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tfistp word [bp%+ld]\n", (long)slot(i->to, fn));
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tsub sp, 2\n");
			fprintf(f, "\tfistp word [sp]\n");
			fprintf(f, "\tpop %s\n", rname[i->to.val]);
		}
		return;

	case Odtosi:
		/*
		 * Double to signed word
		 */
		r0 = i->arg[0];
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tfld qword [bp%+ld]\n", (long)slot(r0, fn));
		}
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tfistp word [bp%+ld]\n", (long)slot(i->to, fn));
		} else if (rtype(i->to) == RTmp) {
			fprintf(f, "\tsub sp, 2\n");
			fprintf(f, "\tfistp word [sp]\n");
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
			fprintf(f, "\tfld dword [bp%+ld]\n", (long)slot(r0, fn));
		}
		/* Store as dword to handle full unsigned range, take low word */
		fprintf(f, "\tsub sp, 4\n");
		fprintf(f, "\tfistp dword [sp]\n");
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tpop word [bp%+ld]\n", (long)slot(i->to, fn));
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
			fprintf(f, "\tfld qword [bp%+ld]\n", (long)slot(r0, fn));
		}
		fprintf(f, "\tsub sp, 4\n");
		fprintf(f, "\tfistp dword [sp]\n");
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tpop word [bp%+ld]\n", (long)slot(i->to, fn));
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
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			}
		} else if (i->cls == Kd) {
			/* Long to double bitcast */
			if (rtype(r0) == RSlot && rtype(i->to) == RSlot) {
				for (int j = 0; j < 4; j++) {
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + j*2);
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + j*2);
				}
			}
		} else if (i->cls == Kw) {
			/* Float to integer bitcast */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				if (rtype(i->to) == RSlot)
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				else if (rtype(i->to) == RTmp)
					{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				if (rtype(i->to) == RSlot)
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			}
		} else if (i->cls == Kl) {
			/* Double to long bitcast */
			if (rtype(r0) == RSlot && rtype(i->to) == RSlot) {
				for (int j = 0; j < 4; j++) {
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + j*2);
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + j*2);
				}
			}
		}
		return;
	}

	/*
	 * Far Pointer Operations (i8086 specific)
	 *
	 * Far pointers are 32-bit values stored as segment:offset pairs.
	 * In memory, they are stored as offset (low word) then segment (high word).
	 *
	 * To access memory through a far pointer:
	 * 1. Load segment into ES (or another segment register)
	 * 2. Load offset into a base register (BX, SI, DI, BP)
	 * 3. Use segment override: mov al, es:[bx]
	 *
	 * Far pointer layout in memory (little-endian):
	 *   [ptr+0]: offset low byte
	 *   [ptr+1]: offset high byte
	 *   [ptr+2]: segment low byte
	 *   [ptr+3]: segment high byte
	 */
	switch (i->op) {
	case Oloadfb:
		/*
		 * Load byte through far pointer
		 * arg[0] = far pointer (32-bit: segment:offset)
		 * result = byte value (zero-extended to word)
		 *
		 * The libstub ABI assumes ES = DS = DGROUP at every call (sprintf
		 * uses stosb against ES:DI; INT 21h handle writes don't, but ES
		 * stays caller-visible).  Push/pop ES around the far access so
		 * the caller's invariant survives.
		 *
		 * BX clobber: this handler uses BX as the address scratch; rega
		 * does not know about that, so a live SSA temp rega placed in BX
		 * would be silently overwritten.  Save/restore BX around the
		 * access (the final `mov dst, ax` happens after pop, so a dst=BX
		 * load still ends up with the loaded word in BX, the save just
		 * costs a push/pop pair).  Surfaced by huge-mode loops where
		 * `i` lived in BX across iterations.  See
		 * [[i8086-farptr-bx-clobber]].
		 *
		 * AX clobber: the load lands in AX/AL before any move to dst,
		 * and the RCon path's load_farptr_con also uses AX to stage the
		 * segment immediate.  rega has no visibility into this; two
		 * back-to-back narrow loads whose results both feed the same
		 * call would alias each other via AX.  Wrap with kl_save_axdx
		 * (same bracket used by Oloadfl) so any rega-placed live tmp in
		 * AX survives.  DX save is a wasted push/pop for the b/h/w
		 * widths (body doesn't touch DX) but keeps the helper signature
		 * uniform.  See [[i8086-compact-loadfb-aliases-ax]].
		 */
		r0 = i->arg[0];
		{
		AxDxSave s_loadfb = kl_save_axdx(i->to, f);
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		/* Load far pointer components into ES:BX */
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r0, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r0, fn) + 2);  /* segment */
		} else if (rtype(r0) == RCon) {
			load_farptr_con(&fn->con[r0.val], f);
		} else if (rtype(r0) == RTmp) {
			/* Far pointer in DX:AX (segment:offset) */
			fprintf(f, "\tmov bx, ax\n");  /* offset in AX -> BX */
			fprintf(f, "\tmov es, dx\n");  /* segment in DX -> ES */
		}
		/* Load byte through ES:BX */
		fprintf(f, "\tmov al, byte ptr es:[bx]\n");
		fprintf(f, "\txor ah, ah\n");  /* zero-extend to word */
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		/* Store result */
		if (rtype(i->to) == RTmp)
			{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		kl_restore_axdx(s_loadfb, f);
		}
		return;

	case Oloadfh:
	case Oloadfw:
		/*
		 * Load word through far pointer
		 * arg[0] = far pointer (32-bit: segment:offset)
		 * result = word value
		 *
		 * Push/pop ES + BX around the access — see Oloadfb for rationale.
		 * AX clobber save bracket too — same shape as Oloadfb.  See
		 * [[i8086-compact-loadfb-aliases-ax]].
		 */
		r0 = i->arg[0];
		{
		AxDxSave s_loadfw = kl_save_axdx(i->to, f);
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		/* Load far pointer components into ES:BX */
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r0, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r0, fn) + 2);  /* segment */
		} else if (rtype(r0) == RCon) {
			load_farptr_con(&fn->con[r0.val], f);
		} else if (rtype(r0) == RTmp) {
			/* Far pointer in DX:AX (segment:offset) */
			fprintf(f, "\tmov bx, ax\n");  /* offset in AX -> BX */
			fprintf(f, "\tmov es, dx\n");  /* segment in DX -> ES */
		}
		/* Load word through ES:BX */
		fprintf(f, "\tmov ax, word ptr es:[bx]\n");
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		/* Store result */
		if (rtype(i->to) == RTmp)
			{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		kl_restore_axdx(s_loadfw, f);
		}
		return;

	case Oloadfl:
		/*
		 * Load 32-bit long through far pointer.
		 * arg[0] = far pointer (Kl, segment:offset)
		 * result = 32-bit value (Kl)
		 *
		 * Both halves load to AX (low) / DX (high); result lands in the
		 * destination slot per spill.c's Kl-slot-resident invariant.  Use
		 * kl_save_axdx to preserve any rega-placed live tmps in AX/DX
		 * (Oloadf{b,h,w} only clobber AX, so they don't need DX-save;
		 * this handler clobbers both).  Push/pop ES + BX around the
		 * access — same rationale as Oloadfb.
		 */
		r0 = i->arg[0];
		{
		AxDxSave s_loadfl = kl_save_axdx(i->to, f);
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		/* Load far pointer components into ES:BX */
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r0, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r0, fn) + 2);  /* segment */
		} else if (rtype(r0) == RCon) {
			load_farptr_con(&fn->con[r0.val], f);
		} else if (rtype(r0) == RTmp) {
			/* Defensive: Kl-slot-resident invariant makes this unreachable
			 * for real workloads, but mirror Oloadf{b,h,w} just in case. */
			fprintf(f, "\tmov bx, ax\n");  /* offset in AX -> BX */
			fprintf(f, "\tmov es, dx\n");  /* segment in DX -> ES */
		}
		/* Load 32-bit value through ES:BX */
		fprintf(f, "\tmov ax, word ptr es:[bx]\n");
		fprintf(f, "\tmov dx, word ptr es:[bx+2]\n");
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		/* Store result into destination */
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
			fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
		} else if (rtype(i->to) == RTmp) {
			/* Defensive: Kl RTmp shouldn't appear post-spill; write low
			 * half only (matches the Oload Kl tmp path). */
			if (strcmp(rname[i->to.val], "ax") != 0)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		}
		kl_restore_axdx(s_loadfl, f);
		}
		return;

	case Ostorefb:
		/*
		 * Store byte through far pointer
		 * arg[0] = value to store (word, low byte used)
		 * arg[1] = far pointer (32-bit: segment:offset)
		 *
		 * Push/pop ES + BX around the access — see Oloadfb for rationale.
		 * CX is also used as the byte-staging scratch (mov cl, ...); save
		 * it too or any live SSA temp rega placed in CX is silently
		 * clobbered.  Surfaced by stevie's filetonext: nextra.35 lived
		 * in CX across the `*screenp = c` storefb, every loop iteration
		 * corrupted it via the `mov cl, bl` low-byte write.
		 *
		 * AX/DX clobber: when r1 is an RCon CAddr destination (a far store
		 * to a constant global address, e.g. `$g_sink`), load_farptr_con
		 * stages the segment via `mov ax, seg sym` — clobbering AX, which
		 * rega does NOT model.  A live SSA temp in AX across the store
		 * (e.g. a pointer-return value) is silently destroyed.  Wrap with
		 * kl_save_axdx (outermost, mirroring Oloadf* and Ostorefl) so the
		 * live value survives.  The value (r0) is staged into CX before
		 * the far-ptr load runs, so reading r0 from AX still sees the
		 * original.  See [[minic-far-data-segment]] bug 1.
		 */
		r0 = i->arg[0];  /* value */
		r1 = i->arg[1];  /* far pointer */
		{
		AxDxSave s_storefb = kl_save_axdx(i->to, f);
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		fprintf(f, "\tpush cx\n");
		/* Load value to store into CL (to preserve AX for far pointer).
		 * Only AX/CX/DX/BX have 8-bit subregister names; for SI/DI/BP/SP
		 * the rega-placed value lives in a register without a byte form,
		 * so go through the full-word `mov cx, reg16` and let CL pick up
		 * the low byte.  Mirror the [[i8086-compact-loadfb-aliases-ax]]
		 * fix for Oextsb at line ~3574. */
		if (rtype(r0) == RTmp) {
			if (r0.val <= RBX)
				fprintf(f, "\tmov cl, %s\n", rname8[r0.val]);
			else
				fprintf(f, "\tmov cx, %s\n", rname[r0.val]);
		} else if (rtype(r0) == RSlot)
			fprintf(f, "\tmov cl, byte [bp%+ld]\n", (long)slot(r0, fn));
		else if (rtype(r0) == RCon)
			fprintf(f, "\tmov cl, %d\n", (int)(fn->con[r0.val].bits.i & 0xFF));
		/* Load far pointer into ES:BX */
		if (rtype(r1) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r1, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r1, fn) + 2);  /* segment */
		} else if (rtype(r1) == RCon) {
			load_farptr_con(&fn->con[r1.val], f);
		} else if (rtype(r1) == RTmp) {
			/* Far pointer in DX:AX (segment:offset) */
			fprintf(f, "\tmov bx, ax\n");  /* offset in AX -> BX */
			fprintf(f, "\tmov es, dx\n");  /* segment in DX -> ES */
		}
		/* Store byte through ES:BX */
		fprintf(f, "\tmov byte ptr es:[bx], cl\n");
		fprintf(f, "\tpop cx\n");
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		kl_restore_axdx(s_storefb, f);
		}
		return;

	case Ostorefh:
	case Ostorefw:
		/*
		 * Store word through far pointer
		 * arg[0] = value to store (word)
		 * arg[1] = far pointer (32-bit: segment:offset)
		 *
		 * Push/pop ES + BX around the access — see Oloadfb for rationale.
		 * CX is the value-staging scratch (mov cx, ...); save it too or
		 * any live SSA temp rega placed in CX is silently clobbered.
		 * AX/DX save bracket (kl_save_axdx) for the RCon-CAddr-dest
		 * load_farptr_con AX clobber — same shape as Ostorefb.  Mirror of
		 * the Ostorefb fix.  See [[minic-far-data-segment]] bug 1.
		 */
		r0 = i->arg[0];  /* value */
		r1 = i->arg[1];  /* far pointer */
		{
		AxDxSave s_storefw = kl_save_axdx(i->to, f);
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		fprintf(f, "\tpush cx\n");
		/* Load value to store into CX (preserve AX for segment load) */
		if (rtype(r0) == RTmp)
			fprintf(f, "\tmov cx, %s\n", rname[r0.val]);
		else if (rtype(r0) == RSlot)
			fprintf(f, "\tmov cx, word [bp%+ld]\n", (long)slot(r0, fn));
		else if (rtype(r0) == RCon)
			fprintf(f, "\tmov cx, %d\n", (int)(fn->con[r0.val].bits.i & 0xFFFF));
		/* Load far pointer into ES:BX */
		if (rtype(r1) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r1, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r1, fn) + 2);  /* segment */
		} else if (rtype(r1) == RCon) {
			load_farptr_con(&fn->con[r1.val], f);
		} else if (rtype(r1) == RTmp) {
			/* Far pointer in DX:AX (segment:offset) */
			fprintf(f, "\tmov bx, ax\n");  /* offset in AX -> BX */
			fprintf(f, "\tmov es, dx\n");  /* segment in DX -> ES */
		}
		/* Store word through ES:BX */
		fprintf(f, "\tmov word ptr es:[bx], cx\n");
		fprintf(f, "\tpop cx\n");
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		kl_restore_axdx(s_storefw, f);
		}
		return;

	case Ostorefl:
		/*
		 * Store 32-bit long through far pointer.
		 * arg[0] = value to store (Kl, 32-bit)
		 * arg[1] = far pointer (Kl, segment:offset)
		 *
		 * Staging dance: the value needs DX:AX and the far-ptr load
		 * (load_farptr_con under RCon) also uses AX as a scratch.  Stage
		 * the value into DX:AX FIRST, then push it onto the stack, load
		 * the far ptr into ES:BX (free to clobber AX/DX), pop the value
		 * back into DX:AX, write through ES:BX.  Always save AX/DX (no
		 * destination to alias against — Ostorefl has no result reg).
		 * Push/pop ES + BX too, per [[i8086-farptr-es-clobber]] +
		 * [[i8086-farptr-bx-clobber]].
		 */
		r0 = i->arg[0];  /* value */
		r1 = i->arg[1];  /* far pointer */
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\tpush dx\n");
		fprintf(f, "\tpush es\n");
		fprintf(f, "\tpush bx\n");
		/* Stage value into DX:AX (read source BEFORE far-ptr load may
		 * clobber AX as scratch). */
		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
		} else if (rtype(r0) == RCon) {
			load32_axdx_con(&fn->con[r0.val], f);
		} else if (rtype(r0) == RTmp) {
			/* Defensive per spill.c Kl-slot-resident invariant. */
			if (strcmp(rname[r0.val], "ax") != 0)
				fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
			fprintf(f, "\tcwd\n");
		}
		/* Park value on stack so the far-ptr load can use AX freely. */
		fprintf(f, "\tpush dx\n");
		fprintf(f, "\tpush ax\n");
		/* Load far pointer into ES:BX */
		if (rtype(r1) == RSlot) {
			fprintf(f, "\tmov bx, word [bp%+ld]\n", (long)slot(r1, fn));      /* offset */
			fprintf(f, "\tmov es, word [bp%+ld]\n", (long)slot(r1, fn) + 2);  /* segment */
		} else if (rtype(r1) == RCon) {
			load_farptr_con(&fn->con[r1.val], f);
		} else if (rtype(r1) == RTmp) {
			/* Defensive: per Kl-slot-resident invariant, a Kl ptr RTmp
			 * shouldn't appear here.  If it ever does, AX/DX have been
			 * parked on the stack, so reading r1 from DX:AX is wrong —
			 * die rather than emit silently broken code. */
			die("Ostorefl: RTmp far ptr arg unreachable under Kl-slot-resident invariant");
		}
		/* Restore value into DX:AX */
		fprintf(f, "\tpop ax\n");
		fprintf(f, "\tpop dx\n");
		/* Store 32-bit value through ES:BX */
		fprintf(f, "\tmov word ptr es:[bx], ax\n");
		fprintf(f, "\tmov word ptr es:[bx+2], dx\n");
		fprintf(f, "\tpop bx\n");
		fprintf(f, "\tpop es\n");
		fprintf(f, "\tpop dx\n");
		fprintf(f, "\tpop ax\n");
		return;

	case Ofarseg:
		/*
		 * Extract segment from far pointer
		 * arg[0] = far pointer (32-bit)
		 * result = segment (word)
		 *
		 * Emit directly into the destination register when possible so
		 * we don't clobber AX as a scratch — selret pairs an Ofarseg
		 * RDX with an Ofaroff RAX to materialise a Kl return; if both
		 * used AX as scratch the second would overwrite the first.
		 */
		r0 = i->arg[0];
		{
		const char *dstn = NULL;
		if (rtype(i->to) == RTmp)
			dstn = rname[i->to.val];
		else
			dstn = "ax";  /* slot dest: stage through AX */

		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov %s, word [bp%+ld]\n",
			        dstn, (long)slot(r0, fn) + 2);
		} else if (rtype(r0) == RCon) {
			Con *pc = &fn->con[r0.val];
			if (pc->type == CAddr) {
				/* NASM `seg sym` emits a base-segment FIXUP
				 * that omf_link resolves at link time. */
				fprintf(f, "\tmov %s, seg ", dstn);
				fputs(T.assym, f);
				fputs(str(pc->sym.id), f);
				fputc('\n', f);
			} else {
				fprintf(f, "\tmov %s, %d\n", dstn,
				        (int)((pc->bits.i >> 16) & 0xFFFF));
			}
		} else if (rtype(r0) == RTmp) {
			/* Far pointer in DX:AX - segment is in DX */
			if (strcmp(dstn, "dx") != 0)
				fprintf(f, "\tmov %s, dx\n", dstn);
		}
		if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		}
		return;

	case Ofaroff:
		/*
		 * Extract offset from far pointer
		 * arg[0] = far pointer (32-bit)
		 * result = offset (word)
		 */
		r0 = i->arg[0];
		{
		const char *dstn = NULL;
		if (rtype(i->to) == RTmp)
			dstn = rname[i->to.val];
		else
			dstn = "ax";

		if (rtype(r0) == RSlot) {
			fprintf(f, "\tmov %s, word [bp%+ld]\n",
			        dstn, (long)slot(r0, fn));
		} else if (rtype(r0) == RCon) {
			Con *pc = &fn->con[r0.val];
			if (pc->type == CAddr) {
				fprintf(f, "\tmov %s, ", dstn);
				emitaddr(pc, f);
				fputc('\n', f);
			} else {
				fprintf(f, "\tmov %s, %d\n", dstn,
				        (int)(pc->bits.i & 0xFFFF));
			}
		} else if (rtype(r0) == RTmp) {
			/* Far pointer in DX:AX — offset is in AX.  If dst is
			 * already AX (common selret path), no move; otherwise
			 * copy to dst. */
			if (strcmp(dstn, "ax") != 0)
				fprintf(f, "\tmov %s, ax\n", dstn);
		}
		if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		}
		return;

	default:
		break;
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

		/* Perform signed division.  8086 idiv requires a reg/mem operand;
		 * an immediate is illegal.  Hoist constant divisors through BX. */
		if (rtype(r1) == RTmp)
			fprintf(f, "\tidiv %s\n", rname[r1.val]);
		else if (rtype(r1) == RCon) {
			fprintf(f, "\tpush bx\n");
			fprintf(f, "\tmov bx, %"PRIi64"\n", fn->con[r1.val].bits.i);
			fprintf(f, "\tidiv bx\n");
			fprintf(f, "\tpop bx\n");
		} else
			fprintf(f, "\tidiv ?\n");

		/* Move result to destination */
		if (i->op == Odiv) {
			/* Quotient is in AX */
			if (rtype(i->to) == RTmp && i->to.val != RAX)
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
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

		/* Perform unsigned division.  Same constant-hoist as idiv above. */
		if (rtype(r1) == RTmp) {
			fprintf(f, "\tdiv %s\n", rname[r1.val]);
		} else if (rtype(r1) == RCon) {
			fprintf(f, "\tpush bx\n");
			fprintf(f, "\tmov bx, %"PRIi64"\n", fn->con[r1.val].bits.i);
			fprintf(f, "\tdiv bx\n");
			fprintf(f, "\tpop bx\n");
		} else {
			fprintf(f, "\tdiv ?\n");
		}

		/* Move result to destination */
		if (i->op == Oudiv) {
			/* Quotient is in AX */
			if (rtype(i->to) == RTmp && i->to.val != RAX)
				{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		} else {
			/* Remainder is in DX */
			if (rtype(i->to) == RTmp)
				fprintf(f, "\tmov %s, dx\n", rname[i->to.val]);
		}
		return;
	}

	/* Special handling for Ocall (near function calls) */
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

	/* Special handling for Ocallfar (far function calls - inter-segment)
	 * Used in medium/large/huge memory models where code spans multiple segments.
	 * Far calls push both CS (code segment) and IP (instruction pointer) onto stack.
	 */
	if (i->op == Ocallfar) {
		Ref target = i->arg[0];

		if (rtype(target) == RTmp) {
			/* Indirect far call: target is a 32-bit (Kl) far pointer
			 * with offset in AX (low half) and segment in DX (high half).
			 * 8086 has no `call far reg:reg` form, so synthesize one
			 * with a retf trick — push a return frame plus the target,
			 * then retf jumps to dx:ax leaving cs:ret_label as the
			 * callee's saved return address:
			 *
			 *   mov cx, .Lfarcall_<n>      ; (8086: push imm16 is 186+,
			 *                              ;  so route through a reg)
			 *   push cs                    ; saved CS for callee's retf
			 *   push cx                    ; saved IP for callee's retf
			 *   push dx                    ; target segment
			 *   push ax                    ; target offset
			 *   retf                       ; jumps dx:ax
			 *  .Lfarcall_<n>:              ; resume here
			 *
			 * CX is caller-save in cdecl, so any live value the rega
			 * placed there is already dead at this call site. */
			fprintf(f, "\tmov cx, .Lfarcall_%p\n", (void*)i);
			fprintf(f, "\tpush cs\n");
			fprintf(f, "\tpush cx\n");
			fprintf(f, "\tpush dx\n");
			fprintf(f, "\tpush ax\n");
			fprintf(f, "\tretf\n");
			fprintf(f, ".Lfarcall_%p:\n", (void*)i);
		} else if (rtype(target) == RSlot) {
			/* Kl spilled to a stack slot — call far through it directly.
			 * NASM in `cpu 8086` mode rejects the `dword` size hint;
			 * `call far` already implies a 32-bit memory operand. */
			fprintf(f, "\tcall far [bp%+ld]\n", (long)slot(target, fn));
		} else if (rtype(target) == RCon) {
			/* Direct far call to function */
			Con *c = &fn->con[target.val];
			if (c->type == CAddr) {
				/* Far call with segment prefix - linker resolves the segment */
				fprintf(f, "\tcall far ");
				emitaddr(c, f);
				fputc('\n', f);
			} else {
				die("far call with non-address constant");
			}
		} else {
			die("invalid far call target type");
		}

		return;
	}

	/* Oswap: rega's pmgen emits Oswap with `to = R` and both args set to
	 * the two registers to exchange (resolves cycles in parallel moves at
	 * block boundaries).  The optab format `xchg %=, %0` would emit
	 * `xchg , ax` for these, so handle them explicitly here.  Without
	 * this, the swap is silently dropped and rega's parallel-move
	 * resolution produces incorrect code (e.g., screenp not migrating
	 * to its new register at l29→l33 in stevie's filetonext). */
	if (i->op == Oswap && req(i->to, R)) {
		if (rtype(i->arg[0]) == RTmp && rtype(i->arg[1]) == RTmp) {
			fprintf(f, "\txchg %s, %s\n",
				rname[i->arg[0].val], rname[i->arg[1].val]);
		}
		return;
	}

	/* Oextsb Kw: sign-extend the low byte of arg[0] to a 16-bit word.
	 * 8086 has no `movsx` (that's a 386 instruction), so route through
	 * AL/AX with CBW.  CBW operates on AL → AX, so when dst is not AX we
	 * push/pop AX to preserve any live caller-save value the rega placed
	 * there.  Source can be a register, slot, or constant. */
	if (i->op == Oextsb && i->cls == Kw) {
		Ref a0 = i->arg[0];
		int dst_is_ax = (rtype(i->to) == RTmp && i->to.val == RAX);
		if (!dst_is_ax)
			fprintf(f, "\tpush ax\n");
		/* Load the low byte of arg into AL.  When the source is in
		 * AX/CX/DX/BX (registers with byte forms), use the 8-bit
		 * register name.  For SI/DI/BP/SP and slots, take the byte
		 * directly from memory; for constants, mask to 8 bits. */
		if (rtype(a0) == RTmp) {
			if (a0.val == RAX) {
				/* AL already has the low byte. */
			} else if (a0.val <= RBX) {
				fprintf(f, "\tmov al, %s\n", rname8[a0.val]);
			} else {
				/* SI/DI/BP/SP — no 8-bit subregister.  Move the
				 * full word into AX and let CBW look at AL. */
				fprintf(f, "\tmov ax, %s\n", rname[a0.val]);
			}
		} else if (rtype(a0) == RSlot) {
			fprintf(f, "\tmov al, byte [bp%+ld]\n", (long)slot(a0, fn));
		} else if (rtype(a0) == RCon) {
			int64_t val = fn->con[a0.val].bits.i;
			fprintf(f, "\tmov al, %d\n", (int)(val & 0xFF));
		} else {
			die("Oextsb: invalid source ref type");
		}
		fprintf(f, "\tcbw\n");
		/* Move AX to dest (slot or non-AX register). */
		if (rtype(i->to) == RSlot) {
			fprintf(f, "\tmov word [bp%+ld], ax\n",
				(long)slot(i->to, fn));
		} else if (rtype(i->to) == RTmp && i->to.val != RAX) {
			fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		}
		if (!dst_is_ax)
			fprintf(f, "\tpop ax\n");
		return;
	}

	/* Omul (16-bit multiply): 286 added `imul reg, r/m`; 8086 only has
	 * the single-operand form `imul r/m` with implicit AX*r/m → DX:AX.
	 * We take the low 16 bits (AX).  Route through AX with save/restore
	 * when neither input nor output is AX. */
	if (i->op == Omul && i->cls != Kl && i->cls != Ks && i->cls != Kd) {
		Ref a0 = i->arg[0], a1 = i->arg[1];
		int dst_is_ax = (rtype(i->to) == RTmp && i->to.val == RAX);
		int dst_is_dx = (rtype(i->to) == RTmp && i->to.val == RDX);
		int save_ax = !dst_is_ax;
		/* `imul r/m` writes DX:AX — DX gets the high word, clobbering
		 * whatever rega had placed there.  rega does not know about this
		 * implicit clobber, so we must preserve DX around the imul
		 * (unless DX is the destination, in which case it's getting
		 * overwritten with the low word anyway). */
		int save_dx = !dst_is_dx;
		/* If a1 is AX but a0 isn't, swap them: `mov ax, a0` would clobber
		 * a1 before `imul a1` reads it, yielding AX*AX = a0*a0 instead of
		 * a0*a1.  16-bit mul is commutative for the low-word result. */
		if (rtype(a1) == RTmp && a1.val == RAX
		    && !(rtype(a0) == RTmp && a0.val == RAX)) {
			Ref tmp = a0;
			a0 = a1;
			a1 = tmp;
		}
		if (save_ax)
			fprintf(f, "\tpush ax\n");
		if (save_dx)
			fprintf(f, "\tpush dx\n");
		/* Load multiplicand into AX (skip if it's already there). */
		if (rtype(a0) == RTmp && strcmp(rname[a0.val], "ax") == 0) {
			/* nop — AX already holds a0 */
		} else {
			fprintf(f, "\tmov ax, ");
			if (rtype(a0) == RTmp) fprintf(f, "%s\n", rname[a0.val]);
			else if (rtype(a0) == RCon) fprintf(f, "%"PRIi64"\n", fn->con[a0.val].bits.i);
			else if (rtype(a0) == RSlot) fprintf(f, "word [bp%+ld]\n", (long)slot(a0, fn));
			else fprintf(f, "?\n");
		}
		/* imul takes a register/memory operand, never an immediate.
		 * Hoist a constant multiplier through BX. */
		if (rtype(a1) == RCon) {
			fprintf(f, "\tpush bx\n");
			fprintf(f, "\tmov bx, %"PRIi64"\n", fn->con[a1.val].bits.i);
			fprintf(f, "\timul bx\n");
			fprintf(f, "\tpop bx\n");
		} else if (rtype(a1) == RTmp) {
			fprintf(f, "\timul %s\n", rname[a1.val]);
		} else if (rtype(a1) == RSlot) {
			fprintf(f, "\timul word [bp%+ld]\n", (long)slot(a1, fn));
		}
		/* Result low word is in AX; copy to dst (if dst is DX, do this
		 * before restoring DX from the stack). */
		if (dst_is_dx) {
			fprintf(f, "\tmov dx, ax\n");
		} else if (rtype(i->to) == RTmp) {
			if (strcmp(rname[i->to.val], "ax") != 0)
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		} else if (rtype(i->to) == RSlot) {
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		}
		if (save_dx)
			fprintf(f, "\tpop dx\n");
		if (save_ax)
			fprintf(f, "\tpop ax\n");
		return;
	}

	/* Apply the addressing fixup for our custom-emitted handlers below
	 * (Oloaduh/Oloadsh/Oloadub/Oloadsb).  Their format-string-path
	 * counterparts would have got it automatically; since these return
	 * early, do it explicitly. */
	{
		int ld_bad = 0;
		if (i->op == Oloadub || i->op == Oloadsb
		    || i->op == Oloaduh || i->op == Oloadsh) {
			ld_bad = addr_fixup_reg(i->arg[0], fn);
			if (ld_bad) {
				fprintf(f, "\txchg bx, %s\n", rname[ld_bad]);
				swap_bx(&i->to, ld_bad, fn);
				swap_bx(&i->arg[0], ld_bad, fn);
			}
		}
		/* Sub-word loads — 8086 form, no movzx/movsx (386 only).
	 *
	 *   Oloadub  zero-extend byte → word: `xor reg, reg; mov reg8, [mem]`
	 *   Oloadsb  sign-extend byte → word: through AX with `cbw`
	 *   Oloaduh  zero-extend half → "long": just the 16-bit load (high
	 *            half lost to rega's lack of register pairs anyway)
	 *   Oloadsh  sign-extend half → "long": same; pairs aren't allocated
	 */
	if (i->op == Oloaduh || i->op == Oloadsh) {
		fprintf(f, "\tmov ");
		if (rtype(i->to) == RTmp)
			fprintf(f, "%s", rname[i->to.val]);
		else if (rtype(i->to) == RSlot)
			fprintf(f, "word [bp%+ld]", (long)slot(i->to, fn));
		fprintf(f, ", word ");
		emit_memref(i->arg[0], fn, f);
		fputc('\n', f);
		goto unwind_load;
	}
	if (i->op == Oloadub) {
		/* If dst has an 8-bit form, clear it first then load the byte.
		 * Skip this fast path when the address operand uses the same
		 * register as the destination — the `xor dst, dst` would
		 * destroy the address before we read from it.  This happens
		 * after addr_fixup_reg / swap_bx have already routed the
		 * address through BX and rega happened to allocate the dst
		 * to BX as well. */
		if (rtype(i->to) == RTmp && i->to.val <= RBX) {
			int dst = i->to.val;
			int addr_clash = 0;
			if (rtype(i->arg[0]) == RTmp && i->arg[0].val == dst)
				addr_clash = 1;
			else if (rtype(i->arg[0]) == RMem) {
				Mem *m = &fn->mem[i->arg[0].val];
				if (rtype(m->base) == RTmp && m->base.val == dst)
					addr_clash = 1;
				if (rtype(m->index) == RTmp && m->index.val == dst)
					addr_clash = 1;
			}
			if (!addr_clash) {
				fprintf(f, "\txor %s, %s\n", rname[dst], rname[dst]);
				fprintf(f, "\tmov %s, byte ", rname8[dst]);
				emit_memref(i->arg[0], fn, f);
				fputc('\n', f);
				goto unwind_load;
			}
		}
		/* Otherwise route through AL: AH := 0, AL := byte, then mov dst, ax.
		 * Save/restore AX if dst isn't AX. */
		if (rtype(i->to) == RTmp && i->to.val == RAX) {
			fprintf(f, "\txor ax, ax\n");
			fprintf(f, "\tmov al, byte ");
			emit_memref(i->arg[0], fn, f);
			fputc('\n', f);
			goto unwind_load;
		}
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\txor ax, ax\n");
		fprintf(f, "\tmov al, byte ");
		emit_memref(i->arg[0], fn, f);
		fputc('\n', f);
		if (rtype(i->to) == RTmp)
			{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		fprintf(f, "\tpop ax\n");
		goto unwind_load;
	}
	if (i->op == Oloadsb) {
		/* AL := byte; CBW; (mov dst, ax) — must go through AX for CBW. */
		if (rtype(i->to) == RTmp && i->to.val == RAX) {
			fprintf(f, "\tmov al, byte ");
			emit_memref(i->arg[0], fn, f);
			fputc('\n', f);
			fprintf(f, "\tcbw\n");
			goto unwind_load;
		}
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\tmov al, byte ");
		emit_memref(i->arg[0], fn, f);
		fputc('\n', f);
		fprintf(f, "\tcbw\n");
		if (rtype(i->to) == RTmp)
			{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		fprintf(f, "\tpop ax\n");
		goto unwind_load;
	}
	goto end_load_block;
unwind_load:
	if (ld_bad) {
		swap_bx(&i->to, ld_bad, fn);
		swap_bx(&i->arg[0], ld_bad, fn);
		fprintf(f, "\txchg bx, %s\n", rname[ld_bad]);
	}
	return;
end_load_block:
	(void)0;
	}

	/* 16-bit comparisons (Oc*w): the 386 sequence is `cmp; setcc; movzx`
	 * but setcc and movzx are 386-only.  Emit an 8086-compatible
	 * materialize: cmp, then conditional branch over a `mov dst, 0` so
	 * dst lands as 0 or 1.  Layout:
	 *
	 *   mov dst, 1
	 *   cmp arg0, arg1
	 *   j<cc> .Ldone_<id>      ; condition true → keep 1
	 *   mov dst, 0
	 * .Ldone_<id>:
	 */
	if (INRANGE(i->op, Oceqw, Ocultw)) {
		const char *jcc;
		switch (i->op) {
		case Oceqw:  jcc = "je";  break;
		case Ocnew:  jcc = "jne"; break;
		case Ocsltw: jcc = "jl";  break;
		case Ocsgtw: jcc = "jg";  break;
		case Ocslew: jcc = "jle"; break;
		case Ocsgew: jcc = "jge"; break;
		case Ocultw: jcc = "jb";  break;
		case Ocugtw: jcc = "ja";  break;
		case Oculew: jcc = "jbe"; break;
		case Ocugew: jcc = "jae"; break;
		default: jcc = "jmp"; break;
		}
		/* cmp arg0, arg1 first.  We used to materialize dst=1
		 * before the cmp, but that's incorrect when dst aliases
		 * arg0 (e.g. both end up in AX) — the `mov dst, 1` would
		 * clobber arg0 before the comparison reads it.  `mov`
		 * doesn't modify flags on 8086, so doing cmp first then
		 * `mov dst, 1` between cmp and jcc is safe.
		 *
		 * 8086 has no mem-mem cmp, so when both args are slots
		 * stage arg0 through AX (save/restore to preserve any
		 * live value rega assigned to AX).  AX is safe scratch
		 * because the immediately following `mov dst, 1` either
		 * targets a slot or a different register (or AX itself,
		 * in which case the staged value is dead by then). */
		{
		int both_mem = (rtype(i->arg[0]) == RSlot
		             && rtype(i->arg[1]) == RSlot);
		if (both_mem) {
			fprintf(f, "\tpush ax\n");
			fprintf(f, "\tmov ax, word [bp%+ld]\n",
				(long)slot(i->arg[0], fn));
		}
		fprintf(f, "\tcmp ");
		if (both_mem)
			fprintf(f, "ax");
		else if (rtype(i->arg[0]) == RTmp)
			fprintf(f, "%s", rname[i->arg[0].val]);
		else if (rtype(i->arg[0]) == RSlot)
			fprintf(f, "word [bp%+ld]", (long)slot(i->arg[0], fn));
		else if (rtype(i->arg[0]) == RCon) {
			Con *pc0 = &fn->con[i->arg[0].val];
			if (pc0->type == CAddr)
				emitaddr(pc0, f);
			else
				fprintf(f, "%"PRIi64, pc0->bits.i);
		}
		fprintf(f, ", ");
		if (rtype(i->arg[1]) == RTmp)
			fprintf(f, "%s", rname[i->arg[1].val]);
		else if (rtype(i->arg[1]) == RSlot)
			fprintf(f, "word [bp%+ld]", (long)slot(i->arg[1], fn));
		else if (rtype(i->arg[1]) == RCon) {
			Con *pc1 = &fn->con[i->arg[1].val];
			if (pc1->type == CAddr)
				emitaddr(pc1, f);
			else
				fprintf(f, "%"PRIi64, pc1->bits.i);
		}
		fprintf(f, "\n");
		if (both_mem)
			fprintf(f, "\tpop ax\n");
		}
		/* Materialize dst = 1 (assume condition true).  No flag impact. */
		fprintf(f, "\tmov ");
		if (rtype(i->to) == RTmp)
			fprintf(f, "%s", rname[i->to.val]);
		else if (rtype(i->to) == RSlot)
			fprintf(f, "word [bp%+ld]", (long)slot(i->to, fn));
		fprintf(f, ", 1\n");
		/* j<cc> .Ldone — if condition true, dst stays 1 */
		fprintf(f, "\t%s .Lcmp_done_%p\n", jcc, (void*)i);
		/* Condition false: clear dst to 0 */
		fprintf(f, "\tmov ");
		if (rtype(i->to) == RTmp)
			fprintf(f, "%s", rname[i->to.val]);
		else if (rtype(i->to) == RSlot)
			fprintf(f, "word [bp%+ld]", (long)slot(i->to, fn));
		fprintf(f, ", 0\n");
		fprintf(f, ".Lcmp_done_%p:\n", (void*)i);
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
		die("i8086: unsupported instruction (op %d cls %d) — no entry in omap[]", i->op, i->cls);
	}

	fmt = omap[o].fmt;

	/* Detect the three i8086 fixups that may apply to this instruction:
	 *
	 *  1. Addressing fixup (outer): if a memory operand (%M0/%M1) uses
	 *     AX/CX/DX as base, those aren't legal 8086 base regs — wrap with
	 *     `xchg bx, <reg>` and remap RBX↔bad in every Ref.
	 *  2. Byte-store fixup: Ostoreb's value (%B0) on SI/DI/BP/SP has no
	 *     8-bit form — copy through AX with push/pop.
	 *  3. setCC fixup: cmp dest (%B=) on SI/DI/BP/SP has no 8-bit form —
	 *     wrap cmp+setcc+movzx in push ax/pop ax.
	 *
	 * The addressing fixup must be the OUTERMOST wrap because it relies
	 * on the swap_bx mutation seeing the original operand registers.
	 * Byte-store fixup runs INSIDE addressing fixup (after swap_bx) so
	 * its `mov ax, <src>` doesn't fight with a swapped AX.  setCC fixup
	 * also runs inside addressing fixup (though Oc*w never have %M
	 * operands, so addressing fixup is a no-op for them in practice).
	 */
	{
		int bad = 0;
		int needs_byte_store = 0;
		int needs_setcc = 0;
		char *p;

		for (p = fmt; *p; p++)
			if (p[0] == '%' && p[1] == 'M' && (p[2] == '0' || p[2] == '1')) {
				bad = addr_fixup_reg(p[2] == '0' ? i->arg[0] : i->arg[1], fn);
				if (bad)
					break;
			}

		if (bad) {
			fprintf(f, "\txchg bx, %s\n", rname[bad]);
			swap_bx(&i->to, bad, fn);
			swap_bx(&i->arg[0], bad, fn);
			swap_bx(&i->arg[1], bad, fn);
		}

		/* Two-address fixup: format strings of the form `op %=, %1`
		 * encode the constraint that the destination register must
		 * already hold arg[0]'s value (since 8086 ops like add/sub/and
		 * read-modify-write the first operand).  rega's coalescer hints
		 * but doesn't guarantee this, so when arg[0] and to ended up in
		 * different physical registers we must materialize the copy.
		 *
		 * Detect the pattern by scanning fmt for both "%=" and "%1".
		 * If matched, and arg[0] is a tmp/slot/const distinct from to,
		 * emit a `mov to, arg[0]` first.  We also rewrite arg[0] to
		 * equal to so emitf prints the expected `op to, arg1`.
		 *
		 * When the op is commutative and arg[1] is the same reg as to
		 * (but arg[0] is not), swapping arg[0] and arg[1] first avoids
		 * a clobber: the `mov to, arg[0]` would otherwise overwrite
		 * arg[1]'s live value in to.  After the swap, arg[0] is what
		 * was already in to (the mov is a no-op or skipped) and arg[1]
		 * holds the other operand for the read-modify-write.
		 */
		{
			int has_eq = 0, has_1 = 0;
			for (p = fmt; *p; p++) {
				if (p[0] == '%' && p[1] == '=') has_eq = 1;
				if (p[0] == '%' && p[1] == '1') has_1 = 1;
			}
			if (has_eq && has_1
			    && rtype(i->to) == RTmp
			    && optab[i->op].commutes
			    && !req(i->arg[1], R)
			    && req(i->arg[1], i->to)
			    && !req(i->arg[0], i->to)) {
				Ref tmp = i->arg[0];
				i->arg[0] = i->arg[1];
				i->arg[1] = tmp;
			}
			if (has_eq && has_1
			    && rtype(i->to) == RTmp
			    && !req(i->arg[0], R)
			    && !req(i->arg[0], i->to)) {
				/* For non-commutative ops (sub, sar, shr, shl)
				 * we couldn't swap operands if arg[1] aliases to;
				 * the dst-mov below would clobber arg[1]'s live
				 * value.  Save it via BX with push/pop so the
				 * op consumes the preserved copy. */
				int rescue_arg1 = (!optab[i->op].commutes
				    && !req(i->arg[1], R)
				    && rtype(i->arg[1]) == RTmp
				    && req(i->arg[1], i->to));
				if (rescue_arg1) {
					fprintf(f, "\tpush bx\n");
					fprintf(f, "\tmov bx, %s\n",
						rname[i->arg[1].val]);
					i->arg[1] = TMP(RBX);
				}
				if (rtype(i->arg[0]) == RTmp
				    && i->arg[0].val != i->to.val) {
					fprintf(f, "\tmov %s, %s\n",
						rname[i->to.val], rname[i->arg[0].val]);
					i->arg[0] = i->to;
				} else if (rtype(i->arg[0]) == RCon) {
					Con *cc = &fn->con[i->arg[0].val];
					if (cc->type == CBits) {
						int64_t v = cc->bits.i;
						if (i->cls == Kw) v = (int64_t)(int16_t)v;
						fprintf(f, "\tmov %s, %"PRIi64"\n",
							rname[i->to.val], v);
					} else if (cc->type == CAddr) {
						fprintf(f, "\tmov %s, ", rname[i->to.val]);
						emitaddr(cc, f);
						fprintf(f, "\n");
					}
					i->arg[0] = i->to;
				} else if (rtype(i->arg[0]) == RSlot) {
					fprintf(f, "\tmov %s, word [bp%+ld]\n",
						rname[i->to.val],
						(long)slot(i->arg[0], fn));
					i->arg[0] = i->to;
				}
				/* If we pushed BX, restore it after emitf prints
				 * the op.  We mark that here so the deferred
				 * `pop bx` is emitted at the right point. */
				if (rescue_arg1) {
					/* emit the op now and pop bx after.  Easiest:
					 * call emitf directly here, then pop, then
					 * `return`.  We replicate the addressing-fixup
					 * unwind that the normal code path would do. */
					emitf(fmt, i, fn, f);
					fprintf(f, "\tpop bx\n");
					if (bad) {
						swap_bx(&i->to, bad, fn);
						swap_bx(&i->arg[0], bad, fn);
						swap_bx(&i->arg[1], bad, fn);
						fprintf(f, "\txchg bx, %s\n",
							rname[bad]);
					}
					return;
				}
			}
		}

		needs_byte_store = (i->op == Ostoreb
		    && rtype(i->arg[0]) == RTmp
		    && (i->arg[0].val == RSI || i->arg[0].val == RDI
		        || i->arg[0].val == RBP || i->arg[0].val == RSP
		        || i->arg[0].val == RES || i->arg[0].val == RDS));

		needs_setcc = (INRANGE(i->op, Oceqw, Ocultw)
		    && rtype(i->to) == RTmp
		    && (i->to.val == RSI || i->to.val == RDI
		        || i->to.val == RBP || i->to.val == RSP
		        || i->to.val == RES || i->to.val == RDS));

		/* 8086 mem-to-mem fixup: a store/copy whose value is a stack slot
		 * (rega spilled both ends) produces `mov [dst], [src]` which is
		 * illegal — 8086 has no memory-to-memory form.  Hoist through AX.
		 * Applies to Ostore[bhw] (slot source) and to Ocopy when both
		 * arg[0] and to ended up as slots. */
		{
			int is_slot_store = (i->op == Ostoreb || i->op == Ostoreh
			    || i->op == Ostorew) && rtype(i->arg[0]) == RSlot;
			int is_slot_copy = (i->op == Ocopy && i->cls == Kw
			    && rtype(i->arg[0]) == RSlot && rtype(i->to) == RSlot);

			if (is_slot_store || is_slot_copy) {
				Ref orig = i->arg[0];
				fprintf(f, "\tpush ax\n");
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(orig, fn));
				i->arg[0] = TMP(RAX);
				emitf(fmt, i, fn, f);
				i->arg[0] = orig;
				fprintf(f, "\tpop ax\n");
				if (bad) {
					swap_bx(&i->to, bad, fn);
					swap_bx(&i->arg[0], bad, fn);
					swap_bx(&i->arg[1], bad, fn);
					fprintf(f, "\txchg bx, %s\n", rname[bad]);
				}
				return;
			}
		}

		if (needs_byte_store) {
			Ref orig = i->arg[0];
			fprintf(f, "\tpush ax\n");
			fprintf(f, "\tmov ax, %s\n", rname[orig.val]);
			i->arg[0] = TMP(RAX);
			emitf(fmt, i, fn, f);
			i->arg[0] = orig;
			fprintf(f, "\tpop ax\n");
		} else if (needs_setcc) {
			fprintf(f, "\tpush ax\n");
			emitf(fmt, i, fn, f);
			fprintf(f, "\tpop ax\n");
		} else {
			emitf(fmt, i, fn, f);
		}

		if (bad) {
			swap_bx(&i->to, bad, fn);
			swap_bx(&i->arg[0], bad, fn);
			swap_bx(&i->arg[1], bad, fn);
			fprintf(f, "\txchg bx, %s\n", rname[bad]);
		}
	}
}

void
i8086_emitfn(Fn *fn, FILE *f)
{
	Blk *b;
	Ins *i;

	/* Emit memory model header (once per output file) */
	emit_model_header(f);

	/* Function header */
	fprintf(f, "\n");
	emitfnlnk(fn->name, &fn->lnk, f);

	/* MASM `proc far/near` directive removed.  emitfnlnk above already
	 * emitted the prefixed entry label; far calls are signaled by the
	 * `call far` instruction at the call site, not by a function-level
	 * directive.  NASM (and OMF linkers) don't need or want it.  RETF
	 * is emitted explicitly at the epilogue. */

	/* Function prologue.  Save callee-save registers per cdecl/8086:
	 * BX, SI, DI must be preserved across calls.  rega treats them as
	 * non-clobbered (per i8086_rclob in targ.c), so it freely keeps
	 * live values in them across function calls — which requires that
	 * EACH function save them at entry and restore at exit.  Without
	 * this, a callee that touches BX corrupts the caller's live value.
	 *
	 * We always emit the saves rather than tracking actual usage; the
	 * extra 3 push/pop pairs are tiny relative to the call overhead. */
	fprintf(f, "\tpush bp\n");
	fprintf(f, "\tmov bp, sp\n");
	fprintf(f, "\tpush bx\n");
	fprintf(f, "\tpush si\n");
	fprintf(f, "\tpush di\n");
	if (fn->slot > 0)
		fprintf(f, "\tsub sp, %d\n", 2 * fn->slot);

	/* Emit blocks */
	for (b = fn->start; b; b = b->link) {
		/* Skip empty-name blocks; these creep in at function epilogues
		 * and would emit a stray bare `:` line. */
		if (b != fn->start && b->name[0] != 0)
			fprintf(f, "%s:\n", b->name);

		for (i = b->ins; i < &b->ins[b->nins]; i++)
			emitins(i, fn, f);

		/* Emit jump */
		switch (b->jmp.type) {
		case Jret0:
		case Jretw:
		case Jretl:
			/* Near return - for tiny/small memory models */
			fprintf(f, "\tlea sp, [bp-6]\n");
			fprintf(f, "\tpop di\n");
			fprintf(f, "\tpop si\n");
			fprintf(f, "\tpop bx\n");
			fprintf(f, "\tpop bp\n");
			fprintf(f, "\tret\n");
			break;
		case Jretf0:
		case Jretfw:
		case Jretfl:
			/* Far return - for medium/large/huge memory models
			 * RETF pops both IP and CS from stack (4 bytes total)
			 */
			fprintf(f, "\tlea sp, [bp-6]\n");
			fprintf(f, "\tpop di\n");
			fprintf(f, "\tpop si\n");
			fprintf(f, "\tpop bx\n");
			fprintf(f, "\tpop bp\n");
			fprintf(f, "\tretf\n");
			break;
		case Jjmp:
			if (b->s1 != b->link && b->s1->name[0])
				fprintf(f, "\tjmp %s\n", b->s1->name);
			break;
		case Jjnz: {
			Ref jr = b->jmp.arg;
			if (rtype(jr) == RTmp
			    && jr.val >= 0
			    && jr.val < (int)(sizeof rname / sizeof rname[0])
			    && rname[jr.val] != 0) {
				fprintf(f, "\ttest %s, %s\n",
					rname[jr.val], rname[jr.val]);
			} else if (rtype(jr) == RSlot) {
				/* rega spilled the jjnz condition.  We MUST NOT route
				 * through any GPR — rega may place a live SSA temp in
				 * any caller-save reg at block end (the phi-edge moves
				 * in the successor's edge block expect those values).
				 * Use `cmp mem, 0` which sets ZF directly without
				 * touching any register. */
				fprintf(f, "\tcmp word [bp%+ld], 0\n",
					(long)slot(jr, fn));
			} else if (rtype(jr) == RCon) {
				/* Constant jjnz — fold at emit time. */
				Con *c = &fn->con[jr.val];
				if (c->type == CBits && c->bits.i == 0) {
					/* Always-false: only the s2 branch matters. */
					if (b->s2 != b->link && b->s2->name[0])
						fprintf(f, "\tjmp %s\n",
							b->s2->name);
					break;
				}
				if (c->type == CBits) {
					/* Non-zero constant: always-true. */
					if (b->s1->name[0])
						fprintf(f, "\tjmp %s\n",
							b->s1->name);
					break;
				}
				/* Address constant — non-zero at link time. */
				if (b->s1->name[0])
					fprintf(f, "\tjmp %s\n", b->s1->name);
				break;
			} else {
				fprintf(f, "\t; XXX bad jjnz operand: rtype=%d val=%d\n",
				        rtype(jr), jr.val);
				fprintf(f, "\ttest ax, ax\n");
			}
			if (b->s1->name[0])
				fprintf(f, "\tjnz %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		}
		/* Conditional jumps based on flags (from comparison) */
		case Jjfieq:
			if (b->s1->name[0])
				fprintf(f, "\tje %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfine:
			if (b->s1->name[0])
				fprintf(f, "\tjne %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfislt:
			if (b->s1->name[0])
				fprintf(f, "\tjl %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisgt:
			if (b->s1->name[0])
				fprintf(f, "\tjg %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisle:
			if (b->s1->name[0])
				fprintf(f, "\tjle %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfisge:
			if (b->s1->name[0])
				fprintf(f, "\tjge %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiult:
			if (b->s1->name[0])
				fprintf(f, "\tjb %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiugt:
			if (b->s1->name[0])
				fprintf(f, "\tja %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiule:
			if (b->s1->name[0])
				fprintf(f, "\tjbe %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		case Jjfiuge:
			if (b->s1->name[0])
				fprintf(f, "\tjae %s\n", b->s1->name);
			if (b->s2 != b->link && b->s2->name[0])
				fprintf(f, "\tjmp %s\n", b->s2->name);
			break;
		default:
			/* Unsupported jump type */
			die("i8086: unsupported jump type %d at end of @%s",
			    b->jmp.type, b->name);
		}
	}

	/* MASM `endp` directive removed (not used by NASM). */
}
