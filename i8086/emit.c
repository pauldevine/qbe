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

	/* Single-precision soft-float (Ks) and all double (Kd) operations are
	 * lowered to _sf_* helper calls / die() in i8086_emitins below — the
	 * target has no 8087, so there are NO floating-point op-table rows.
	 * See [[softfloat-spike]]. */

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

/* Materialize a Kw shift's VALUE operand (arg[0]) into register `reg`.
 * The shift handler operates on a single destination register, and used to
 * assume the value was already there — true only when arg[0] is an RTmp the
 * register allocator placed in the destination.  A constant or slot-resident
 * value (e.g. `1 << count`, where the count was just computed into the dest)
 * was silently never loaded, so the shift ran on the leftover dest contents
 * (typically the count itself).  Callers must emit this AFTER securing the
 * count into CL, so a count living in `reg` is read before being overwritten. */
static void
emit_shift_val(const char *reg, Ref r0, Fn *fn, FILE *f)
{
	Con *pc;
	if (rtype(r0) == RTmp) {
		if (strcmp(reg, rname[r0.val]) != 0)
			fprintf(f, "\tmov %s, %s\n", reg, rname[r0.val]);
	} else if (rtype(r0) == RCon) {
		pc = &fn->con[r0.val];
		if (pc->type == CAddr) {
			fprintf(f, "\tmov %s, ", reg);
			emitaddr(pc, f);
			fputc('\n', f);
		} else
			fprintf(f, "\tmov %s, %"PRIi64"\n", reg, pc->bits.i);
	} else if (rtype(r0) == RSlot)
		fprintf(f, "\tmov %s, word [bp%+ld]\n", reg, (long)slot(r0, fn));
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

/* Conservative per-instruction physical-register liveness for AX and DX,
 * used to suppress the dead `push ax/dx … pop dx/ax` save brackets around
 * Kl ops and 32-bit copies.  The brackets exist to protect a live value
 * rega placed in AX/DX from the implicit scratch clobber of these ops (see
 * the many i8086-kl-* feedback memories); they are only NEEDED when AX/DX
 * actually hold a value used after the op.  At function/argument boundaries
 * — which dominate the image — AX/DX hold nothing live, so the bracket is
 * pure overhead (~25% of the generated MicroPython image is these pushes).
 *
 * SAFETY (this MUST NOT reintroduce the clobber bugs): the analysis is a
 * strict over-approximation of liveness — it never claims a register dead
 * when it might be live.  (a) Every block is entered (scanning backward)
 * with AX/DX assumed LIVE at exit, covering Jretw/Jretl return values, Jnz
 * conditions, and all cross-block liveness.  (b) A USE is recorded for every
 * appearance of AX/DX as an operand AND for every Kl instruction with a
 * register operand (a register-resident Kl value carries its high word in
 * DX implicitly).  (c) A KILL is recorded only for definite overwrites: a
 * result written to AX/DX, or a call (caller-save clobber).  Extra uses and
 * missing kills both only ADD liveness, keeping the save in place.  These
 * globals default to 1 (= always save, the original behaviour) and are set
 * per-instruction by i8086_emitfn before each emitins call. */
static int g_live_ax_after = 1;
static int g_live_dx_after = 1;
static int g_live_bx_after = 1;
static signed char *la_ax_buf, *la_dx_buf, *la_bx_buf;  /* per-instruction AX/DX/BX live-after */
static uint la_cap;                          /* capacity of la_*_buf */

static void
compute_axdx_liveafter(Blk *b, Fn *fn, signed char *la_ax, signed char *la_dx, signed char *la_bx)
{
	int n, a, ax, dx, bx;
	Ins *i;
	Ref r, bse, idx;

	ax = 1;  /* conservative: AX/DX/BX live at block exit */
	dx = 1;
	bx = 1;
	for (n = b->nins - 1; n >= 0; n--) {
		la_ax[n] = (signed char)ax;
		la_dx[n] = (signed char)dx;
		la_bx[n] = (signed char)bx;
		i = &b->ins[n];
		/* KILLs — definite overwrites only. */
		if (rtype(i->to) == RTmp && i->to.val == RAX) ax = 0;
		if (rtype(i->to) == RTmp && i->to.val == RDX) dx = 0;
		if (rtype(i->to) == RTmp && i->to.val == RBX) bx = 0;
		/* AX/DX are caller-save, so a call kills any value there; BX is
		 * callee-save (i8086_rclob) — a value placed in BX SURVIVES the
		 * call, so it must NOT be killed here. */
		if (iscall(i->op)) { ax = 0; dx = 0; }
		/* USEs — every AX/DX/BX operand (register, or memref base/index). */
		for (a = 0; a < 2; a++) {
			r = i->arg[a];
			if (rtype(r) == RTmp) {
				if (r.val == RAX) ax = 1;
				if (r.val == RDX) dx = 1;
				if (r.val == RBX) bx = 1;
			} else if (rtype(r) == RMem) {
				bse = fn->mem[r.val].base;
				idx = fn->mem[r.val].index;
				if (rtype(bse) == RTmp && bse.val == RAX) ax = 1;
				if (rtype(bse) == RTmp && bse.val == RDX) dx = 1;
				if (rtype(bse) == RTmp && bse.val == RBX) bx = 1;
				if (rtype(idx) == RTmp && idx.val == RAX) ax = 1;
				if (rtype(idx) == RTmp && idx.val == RDX) dx = 1;
				if (rtype(idx) == RTmp && idx.val == RBX) bx = 1;
			}
		}
		/* Implicit AX:DX read of a register-resident Kl value (its high
		 * word lives in DX without appearing as an operand).  BX is not
		 * part of any register pair, so it is not affected. */
		if (i->cls == Kl
		 && (rtype(i->to) == RTmp || rtype(i->arg[0]) == RTmp
		     || rtype(i->arg[1]) == RTmp)) {
			ax = 1;
			dx = 1;
		}
	}
}

/* Far load/store handlers use BX as the offset scratch for the ES:BX
 * access (see [[i8086-farptr-bx-clobber]]).  rega doesn't model that
 * clobber, so the body is wrapped with push/pop bx to preserve any live
 * SSA temp rega placed in BX.  Drop the save when BX holds nothing live
 * across the op (g_live_bx_after, computed conservatively above) or when
 * `to` IS BX (the handler writes the result there after the restore, so
 * the saved value would be overwritten anyway).  Returns whether a
 * `push bx` was emitted; pass that to farptr_restore_bx. */
static int
farptr_save_bx(Ref to, FILE *f)
{
	int dst_in_bx = (rtype(to) == RTmp && to.val == RBX);
	int save = !dst_in_bx && g_live_bx_after;
	if (save) fprintf(f, "\tpush bx\n");
	return save;
}

static void
farptr_restore_bx(int saved, FILE *f)
{
	if (saved) fprintf(f, "\tpop bx\n");
}

/* Preserve AX/DX across a Kl op that uses them as scratch.  rega doesn't
 * model the implicit clobber, so we save/restore the caller's AX/DX
 * unless the op's destination is one of them (in which case the op writes
 * it and restoring would overwrite the result).  Push/pop is cheaper than
 * the alternative — letting rega spill via memory — for typical 8086 code
 * pressure.  Skip the save when the destination is the register being
 * saved, since the result must remain there.  Also skip when the register
 * is not live across the op (see compute_axdx_liveafter). */
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
	s.save_ax = !dst_in_ax && g_live_ax_after;
	s.save_dx = !dst_in_dx && g_live_dx_after;
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

/* Helpers for the Kl (32-bit) Omul handler's 32x32->32 multiply.
 * The 8086 only has a 16x16->32 mul, so a 32-bit product's low 32 bits
 * are formed from three 16-bit partials:
 *   result = a_lo*b_lo + ((a_lo*b_hi + a_hi*b_lo) << 16)   (mod 2^32)
 * Unsigned `mul` is used for every partial: the low 32 bits of a*b are
 * identical for signed and unsigned operands, so summing unsigned
 * partials over the FULL operand words is correct regardless of whether
 * the operands were sign- or zero-extended.  (The old handler did a
 * single 16x16 `imul` of the low words, which corrupted the high result
 * word for a zero-extended operand whose low word had bit 15 set — e.g.
 * (U32)0xCCCD * x — because imul sign-extended it.)
 * Operands are RSlot or RCon only (spill.c force_kl_slot evicts every Kl
 * temp to a slot, so an RTmp Kl operand cannot occur). */
static void
klmul_movax(Ref r, int hi, Fn *fn, FILE *f)
{
	int64_t v;
	if (rtype(r) == RSlot)
		fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r, fn) + (hi ? 2 : 0));
	else if (rtype(r) == RCon) {
		v = fn->con[r.val].bits.i;
		fprintf(f, "\tmov ax, %d\n", (int)((hi ? (v >> 16) : v) & 0xFFFF));
	} else
		die("i8086: Omul Kl operand is not slot/const");
}

static void
klmul_byword(Ref r, int hi, Fn *fn, FILE *f)
{
	int64_t v;
	if (rtype(r) == RSlot)
		fprintf(f, "\tmul word [bp%+ld]\n", (long)slot(r, fn) + (hi ? 2 : 0));
	else if (rtype(r) == RCon) {
		v = fn->con[r.val].bits.i;
		fprintf(f, "\tmov bx, %d\n", (int)((hi ? (v >> 16) : v) & 0xFFFF));
		fprintf(f, "\tmul bx\n");
	} else
		die("i8086: Omul Kl operand is not slot/const");
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

/* True when this exe model uses far code (so soft-float helpers, like the
 * div32 helpers, are reached with `call far`). */
static int
sf_farcall(void)
{
	return T.memmodel == Mmedium || T.memmodel == Mcompact
	    || T.memmodel == Mlarge  || T.memmodel == Mhuge;
}

/* Soft-float (no 8087): a binary op `to = a <helper> b` where the result is
 * a 32-bit single-precision bit pattern stored to the Ks slot `to`.  Pushes
 * the two 32-bit operands cdecl (a at the lower address), far-calls the
 * libstub-style helper (result in DX:AX), and stores DX:AX to the slot.
 * AX/CX/DX are caller-save and preserved around the call (AX/DX gated on
 * liveness; the destination is always a slot so it never aliases them).
 * Mirrors the _qbe_div32* sequence ([[softfloat-spike]]). */
static void
emit_sf_binop(const char *helper, Ref r0, Ref r1, Ref to, Fn *fn, FILE *f)
{
	int save_ax = g_live_ax_after;
	int save_dx = g_live_dx_after;

	if (rtype(to) != RSlot)
		die("i8086: soft-float result must be slot-resident (op %s)", helper);

	if (save_ax) fprintf(f, "\tpush ax\n");
	fprintf(f, "\tpush cx\n");
	if (save_dx) fprintf(f, "\tpush dx\n");

	emit_push_long(r1, fn, f);   /* arg b: higher address */
	emit_push_long(r0, fn, f);   /* arg a: lower address = first cdecl arg */

	fprintf(f, "\tcall%s %s\n", sf_farcall() ? " far" : "", helper);
	fprintf(f, "\tadd sp, 8\n");

	fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(to, fn));
	fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(to, fn) + 2);

	if (save_dx) fprintf(f, "\tpop dx\n");
	fprintf(f, "\tpop cx\n");
	if (save_ax) fprintf(f, "\tpop ax\n");
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

		/* Move an RTmp value to destination if needed.  This must
		 * happen before any CX/CL load, because when dst==CX the
		 * dst-mov would otherwise clobber the loaded count.  A
		 * non-RTmp value (RCon / RSlot) is NOT in any register, so it
		 * is loaded into the destination later, after the count is
		 * secured (see emit_shift_val), via need_val_load. */
		if (rtype(i->to) == RTmp && r0.val != i->to.val && rtype(r0) == RTmp) {
			fprintf(f, "\tmov %s, %s\n", rname[i->to.val], rname[r0.val]);
			r0 = i->to;
		}
		/* True when the value operand still needs materializing into
		 * the destination register (it is a constant or slot, not an
		 * already-placed RTmp).  Without this the shift ran on whatever
		 * the destination happened to hold — e.g. `1 << count` shifted
		 * the count by itself, and `1 << 0` (count divisible by the ATB
		 * stride in gc_alloc) produced 0, silently dropping the mark. */
		int need_val_load = (rtype(r0) == RCon || rtype(r0) == RSlot);

		/* Pick the destination register name we'll print in `shl/shr/sar`. */
		const char *dstname =
		    (rtype(r0) == RTmp)        ? rname[r0.val] :
		    (rtype(i->to) == RTmp)     ? rname[i->to.val] : "?";
		int dst_is_cx = (rtype(i->to) == RTmp && i->to.val == RCX) ||
		                (rtype(r0)    == RTmp && r0.val    == RCX);

		if (imm_cnt == 1) {
			if (need_val_load) emit_shift_val(dstname, r0, fn, f);
			fprintf(f, "\t%s %s, 1\n", shiftop, dstname);
		} else if (imm_cnt == 0) {
			/* Count 0: result is the value unchanged; just ensure
			 * the value is in the destination. */
			if (need_val_load) emit_shift_val(dstname, r0, fn, f);
			else fprintf(f, "\t%s %s, 0\n", shiftop, dstname);
		} else if (imm_cnt > 1 && imm_cnt <= 8) {
			/* Small immediate count: unroll into repeated
			 * `shl dst, 1`.  This avoids touching CX/CL entirely,
			 * which is critical because the i8086 backend doesn't
			 * tell rega that shifts clobber CL — without unrolling,
			 * `mov cl, imm` would corrupt any live Kw value rega
			 * happened to keep in CX across the shift. */
			int64_t k;
			if (need_val_load) emit_shift_val(dstname, r0, fn, f);
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
				if (need_val_load) emit_shift_val("bx", r0, fn, f);
				else fprintf(f, "\tmov bx, %s\n", dstname);
				fprintf(f, "\tmov cl, %"PRIi64"\n", imm_cnt);
				fprintf(f, "\t%s bx, cl\n", shiftop);
				fprintf(f, "\tmov %s, bx\n", dstname);
				fprintf(f, "\tpop bx\n");
			} else {
				fprintf(f, "\tpush cx\n");
				fprintf(f, "\tmov cl, %"PRIi64"\n", imm_cnt);
				if (need_val_load) emit_shift_val(dstname, r0, fn, f);
				fprintf(f, "\t%s %s, cl\n", shiftop, dstname);
				fprintf(f, "\tpop cx\n");
			}
		} else {
			/* Non-immediate count: must come through CL.  Save CX
			 * around the shift to preserve any unrelated live value
			 * (and use BX as scratch when dst==CX).  The value is
			 * materialized AFTER the count is loaded into CX, so a
			 * count register that aliases the destination is read
			 * before the value overwrites it. */
			if (dst_is_cx) {
				fprintf(f, "\tpush bx\n");
				/* When the value is an RTmp it lives in CX (the
				 * dst); save it to BX before the count overwrites
				 * CX.  When it is a constant/slot it is not in a
				 * register, so load the count into CX first, then
				 * materialize the value into BX. */
				if (!need_val_load)
					fprintf(f, "\tmov bx, %s\n", dstname);
				if (rtype(r1) == RTmp && r1.val != RCX)
					fprintf(f, "\tmov cx, %s\n", rname[r1.val]);
				else if (rtype(r1) == RSlot)
					fprintf(f, "\tmov cx, [bp%+ld]\n",
						(long)slot(r1, fn));
				if (need_val_load) emit_shift_val("bx", r0, fn, f);
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
				if (need_val_load) emit_shift_val(dstname, r0, fn, f);
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
		/* Round a >=4-byte fast-alloc address up to a 4-byte boundary so
		 * its low 2 bits are clear — required when the address is used as
		 * a tagged pointer (MicroPython mp_obj_t).  BP is only 2-byte
		 * aligned, so the bare lea offset can land at &3==2.  The slot was
		 * reserved with 2 bytes of headroom in isel.  Rounding is
		 * deterministic (BP fixed within the call), so every
		 * materialisation of this address agrees. */
		if (rtype(i->arg[0]) == RSlot) {
			int sa = rsval(i->arg[0]);
			if (sa >= 0 && sa < fn->nsalign4 && fn->salign4
			    && fn->salign4[sa]) {
				fprintf(f, "\tadd ax, 3\n");
				fprintf(f, "\tand ax, 0xFFFC\n");
			}
		}
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

	/* Oaddr of a >=4-byte fast-alloc whose address materialises into a
	 * register (near pointer: medium model, or a far address narrowed back
	 * to Kw because it feeds a near deref).  Same 4-byte rounding as the
	 * Kl->slot case above so a self-deref and an escaped tagged pointer to
	 * the SAME slot address agree.  Falls through to the generic
	 * `lea %=, %M0` template when the slot is not align-flagged. */
	if (i->op == Oaddr && rtype(i->to) == RTmp && isreg(i->to)
	    && rtype(i->arg[0]) == RSlot) {
		int sa = rsval(i->arg[0]);
		if (sa >= 0 && sa < fn->nsalign4 && fn->salign4
		    && fn->salign4[sa]) {
			const char *dr = rname[i->to.val];
			fprintf(f, "\tlea %s, ", dr);
			emit_memref(i->arg[0], fn, f);
			fputc('\n', f);
			fprintf(f, "\tadd %s, 3\n", dr);
			fprintf(f, "\tand %s, 0xFFFC\n", dr);
			return;
		}
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
	/* Single-precision soft-float (Ks) values are 32-bit bit patterns
	 * carried exactly like Kl (slot-resident DX:AX pairs), so their
	 * load / copy / store reuse the Kl 32-bit move handlers below
	 * (Ostores shares the Ostorel case).  Their arithmetic, comparison,
	 * and conversion are lowered to _sf_* helper calls further down
	 * ([[softfloat-spike]]). */
	if ((i->cls == Kl && i->op != Oaddr && i->op != Oloadfl
	     && i->op != Ostosi && i->op != Ostoui) || i->op == Ostorel
	    || i->op == Ovargp
	    || INRANGE(i->op, Oceql, Ocultl)
	    || (i->cls == Ks && (i->op == Oload || i->op == Ocopy))
	    || i->op == Ostores) {
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
			int save_ax = !dst_in_ax && g_live_ax_after;
			int save_dx = !dst_in_dx && g_live_dx_after;
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

		case Oaddfo:
		case Osubfo:
			/*
			 * Far-pointer OFFSET add/sub: result = far_ptr ± offset,
			 * with the 16-bit OFFSET word wrapping and the SEGMENT word
			 * preserved (NO adc/sbb into the segment).  On 8086
			 * compact/large a far pointer's segment is fixed per object
			 * (objects <= 64 KB) and arithmetic stays in-segment, so
			 * adding only arg1's low 16 bits to the offset is the correct
			 * lowering — and is correct for BOTH a true large offset
			 * (>= 0x8000, e.g. gc_alloc's start_block*16 on a >32 KB heap)
			 * AND a 16-bit-wrapped "negative" size_t delta (off + 0xFFFF
			 * -> off-1), which a flat 32-bit add of a sign- or
			 * zero-extended index gets wrong in opposite directions.
			 * See [[project-far-ptr-unsigned-index-bug]].
			 *
			 * arg1's HIGH word is deliberately ignored: that is exactly
			 * the part that would (wrongly) carry into the segment.  AX/DX
			 * are scratch (rega doesn't model the clobber, so bracket with
			 * kl_save_axdx); stage an AX/DX-resident RTmp arg1 like Osub.
			 */
			{
			const char *fopc = (i->op == Oaddfo) ? "add" : "sub";
			int dst_in_dx_fo = (rtype(i->to) == RTmp && i->to.val == RDX);
			ArgStage r1s = kl_stage_arg(r1, r0, i->to, f);
			AxDxSave s_fo = kl_save_axdx(i->to, f);

			/* Load the far pointer (arg0) into DX:AX (DX=segment, AX=offset). */
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else if (rtype(r0) == RCon) {
				load32_axdx_con(&fn->con[r0.val], f);
			} else if (rtype(r0) == RTmp) {
				if (strcmp(rname[r0.val], "ax") != 0) fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
				fprintf(f, "\txor dx, dx\n");
			}

			/* Add/subtract ONLY arg1's low word to/from AX (the offset);
			 * leave DX (the segment) untouched — no adc/sbb. */
			if (rtype(r1) == RSlot) {
				fprintf(f, "\t%s ax, word [bp%+ld]\n", fopc, (long)slot(r1, fn));
			} else if (rtype(r1) == RCon) {
				Con *pc = &fn->con[r1.val];
				if (pc->type == CAddr)
					die("i8086: addfo/subfo offset is an address — far-pointer index must be a plain integer");
				fprintf(f, "\t%s ax, %d\n", fopc, (int)(pc->bits.i & 0xFFFF));
			} else if (rtype(r1) == RTmp) {
				const char *r1n = r1s.scratch_reg ? r1s.scratch_reg : rname[r1.val];
				fprintf(f, "\t%s ax, %s\n", fopc, r1n);
			}

			/* Store result DX:AX (segment word unchanged). */
			if (rtype(i->to) == RSlot) {
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
			} else if (rtype(i->to) == RTmp) {
				if (dst_in_dx_fo)
					fprintf(f, "\tmov dx, ax\n");
				else if (strcmp(rname[i->to.val], "ax") != 0)
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			}

			kl_restore_axdx(s_fo, f);
			kl_unstage_arg(r1s, f);
			}
			return;

		case Omul:
			/*
			 * 32-bit multiplication: dest = src0 * src1 (low 32 bits).
			 * Proper 32x32->32 via three 16x16 unsigned partials (see
			 * klmul_movax/klmul_byword).  CAddr is rejected: multiplying
			 * a pointer is C-illegal, so it's unreachable from realistic
			 * frontend output, and bits.i for a CAddr is only the addend
			 * (segment lives in the relocation) — die() makes any such
			 * bug loud rather than silently dropping the segment word.
			 * mul writes DX:AX, and we use CX (plus BX for a const
			 * multiplier) as scratch; rega models none of these, so
			 * preserve AX/DX (kl_save_axdx) and push/pop CX (+BX).
			 */
			{
			int dst_in_dx_mul = (rtype(i->to) == RTmp && i->to.val == RDX);
			int need_bx = (rtype(r1) == RCon);
			AxDxSave s_mul;

			if ((rtype(r0) == RCon && fn->con[r0.val].type == CAddr)
			 || (rtype(r1) == RCon && fn->con[r1.val].type == CAddr))
				die("i8086: Omul Kl with CAddr arg — pointer multiplication is not a valid C operation");

			s_mul = kl_save_axdx(i->to, f);
			fprintf(f, "\tpush cx\n");
			if (need_bx) fprintf(f, "\tpush bx\n");

			/* cx = (a_hi*b_lo + a_lo*b_hi) low 16 — the high cross sum */
			klmul_movax(r0, 1, fn, f);   /* ax = a_hi */
			klmul_byword(r1, 0, fn, f);  /* dx:ax = a_hi*b_lo */
			fprintf(f, "\tmov cx, ax\n");
			klmul_movax(r0, 0, fn, f);   /* ax = a_lo */
			klmul_byword(r1, 1, fn, f);  /* dx:ax = a_lo*b_hi */
			fprintf(f, "\tadd cx, ax\n");
			/* dx:ax = a_lo*b_lo (full low product) */
			klmul_movax(r0, 0, fn, f);
			klmul_byword(r1, 0, fn, f);
			fprintf(f, "\tadd dx, cx\n");  /* result high word */

			if (need_bx) fprintf(f, "\tpop bx\n");
			fprintf(f, "\tpop cx\n");

			/* Store result DX:AX */
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
			}
			return;

		case Oand:
			/*
			 * 32-bit bitwise AND: dest = src0 & src1.
			 * load32_dxax + the op use AX/DX as scratch; rega doesn't
			 * model that implicit clobber, so preserve the caller's
			 * AX/DX across the op — same bracketing as Oadd/Osub Kl
			 * ([[i8086-kl-add-sub-mul-r1-alias]]).  Without this, a
			 * live value rega parked in AX/DX (e.g. a loop-carried temp
			 * live across this OR-chain) is silently corrupted.  Slot
			 * operands are bp-relative, so the push/pop of AX/DX (which
			 * move SP, not BP) leaves their offsets valid.
			 */
			{
			int dst_in_dx_and = (rtype(i->to) == RTmp && i->to.val == RDX);
			AxDxSave s_and = kl_save_axdx(i->to, f);
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
				if (dst_in_dx_and) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_and, f);
			}
			return;

		case Oor:
			/*
			 * 32-bit bitwise OR: dest = src0 | src1.  AX/DX-clobber
			 * preservation, same shape as Oand Kl above.
			 */
			{
			int dst_in_dx_or = (rtype(i->to) == RTmp && i->to.val == RDX);
			AxDxSave s_or = kl_save_axdx(i->to, f);
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
				if (dst_in_dx_or) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_or, f);
			}
			return;

		case Oxor:
			/*
			 * 32-bit bitwise XOR: dest = src0 ^ src1.  AX/DX-clobber
			 * preservation, same shape as Oand Kl above.
			 */
			{
			int dst_in_dx_xor = (rtype(i->to) == RTmp && i->to.val == RDX);
			AxDxSave s_xor = kl_save_axdx(i->to, f);
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
				if (dst_in_dx_xor) {
					fprintf(f, "\tmov dx, ax\n");
				} else if (strcmp(rname[i->to.val], "ax") != 0) {
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
				}
			}

			kl_restore_axdx(s_xor, f);
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

		case Ocast:
			/*
			 * Bitwise reinterpret with a 32-bit (Kl) result.  On i8086
			 * both Kl (long) and Ks (float) are 32 bits, so a float<->long
			 * `cast` (e.g. produced when load-forwarding folds a union
			 * pun) is just a 32-bit move — share the Ocopy path.  (A Ks
			 * result `cast` doesn't enter this Kl block; it is handled in
			 * the conversion switch below.)
			 */
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
			int save_ax = !src_in_ax && !dst_in_ax && g_live_ax_after;
			int save_dx = !src_in_dx && !dst_in_dx && g_live_dx_after;
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
			/* Elide a no-op self-copy: an incoming-Kl-param temp that
			 * selpar aliased to its own ABI slot (see i8086/abi.c)
			 * lowers to Oload Kl SLOT(s) <- SLOT(s) with s < 0 (the
			 * param region above BP, a direct-read slot).  Reading and
			 * writing the same memory is a no-op — emit nothing.  The
			 * s < 0 gate keeps this away from the spilled-Kl-ptr deref
			 * case (slot index >= arg_slot_top), which is NOT a no-op. */
			if (rtype(i->to) == RSlot && rtype(r0) == RSlot
			 && rsval(i->to) == rsval(r0) && rsval(r0) < 0)
				return;
			}
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

		case Ostores:   /* soft-float store: 32-bit, same as Ostorel */
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
			int save_ax = !src_in_ax && g_live_ax_after;
			int save_dx = !src_in_dx && g_live_dx_after;
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

		case Ovargp: {
			/* va_start: pointer to the first variadic argument.  The
			 * caller pushed varargs just past our named params, so the
			 * first one lives at [bp+fn->vararg_off] in SS.  Far-data:
			 * the result is a far ptr SS:(bp+off) in DX:AX (seg in DX,
			 * off in AX), stored to the Kl result slot like Omkfar.
			 * Near-data (DS==SS): the result is the bare offset
			 * (bp+off) in AX.  AX/DX are scratch here; bracket with
			 * kl_save_axdx so a live caller value rega placed in AX/DX
			 * (e.g. a named param like `count') survives.  See
			 * [[project-minic-vararg-stub]]. */
			int vargp_far = (T.memmodel == Mcompact ||
			                 T.memmodel == Mlarge ||
			                 T.memmodel == Mhuge);
			AxDxSave s_va = kl_save_axdx(i->to, f);
			fprintf(f, "\tlea ax, [bp%+d]\n", fn->vararg_off);
			if (vargp_far) {
				fprintf(f, "\tmov dx, ss\n");
				if (rtype(i->to) == RSlot) {
					fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
					fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
				} else if (rtype(i->to) == RTmp && i->to.val != RAX)
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			} else if (rtype(i->to) == RTmp) {
				if (i->to.val != RAX)
					fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
			} else if (rtype(i->to) == RSlot)
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
			kl_restore_axdx(s_va, f);
			return;
		}

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
			/* Gate AX/DX save brackets on physical-reg liveness, same
			 * conservative over-approximation as §2w / kl_save_axdx: if
			 * AX (resp. DX) holds nothing live after this op, the
			 * push/pop pair is dead.  CX has no liveness tracker (§2w
			 * only models AX/DX), so leave save_cx gated on dst only. */
			int save_ax = !dst_in_ax && g_live_ax_after;
			int save_cx = !dst_in_cx;
			int save_dx = !dst_in_dx && g_live_dx_after;
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
	 * ===== Single-precision software floating point (no 8087) =====
	 *
	 * The Victor 9000 / target has no 8087, so Ks (single-precision,
	 * IEEE-754 binary32) values are 32-bit bit patterns carried exactly
	 * like Kl (slot-resident DX:AX pairs).  Their load / copy / store were
	 * handled by the Kl 32-bit move handlers above; here we lower the
	 * arithmetic, comparison, and conversion ops to far calls to the
	 * libstub-style _sf_* helpers (built by minic from minic/dos/softfloat.c,
	 * linked via build-example.sh --softfloat).  Each helper takes its
	 * 32-bit arg(s) cdecl on the stack and returns a 32-bit result in DX:AX
	 * (sf_cmp returns -1/0/1, or 2 for unordered/NaN, in AX).
	 *
	 * Double precision (Kd) is NOT implemented — every double op dies loudly.
	 * See [[softfloat-spike]].
	 */

	/* (1) Float comparisons.  These carry result class Kw but compare Ks
	 * operands, so they are detected by op-range (Ocmps..Ocmps1), not cls.
	 * isel routes them here via selfp.  Lower to `call far _sf_cmp` then
	 * map the -1/0/1/2 result to the op's boolean. */
	if (INRANGE(i->op, Ocmpd, Ocmpd1))
		die("i8086: double (Kd) compare not implemented — single-precision soft-float only");
	if (INRANGE(i->op, Ocmps, Ocmps1)) {
		int dst_in_ax_c = (rtype(i->to) == RTmp && i->to.val == RAX);
		int dst_in_dx_c = (rtype(i->to) == RTmp && i->to.val == RDX);
		int save_ax_c = !dst_in_ax_c && g_live_ax_after;
		int save_dx_c = !dst_in_dx_c && g_live_dx_after;
		r0 = i->arg[0];
		r1 = i->arg[1];

		if (save_ax_c) fprintf(f, "\tpush ax\n");
		fprintf(f, "\tpush cx\n");
		if (save_dx_c) fprintf(f, "\tpush dx\n");

		emit_push_long(r1, fn, f);   /* b: higher address */
		emit_push_long(r0, fn, f);   /* a: first cdecl arg */
		fprintf(f, "\tcall%s _sf_cmp\n", sf_farcall() ? " far" : "");
		fprintf(f, "\tadd sp, 8\n");

		/* AX holds sf_cmp's signed result; move to CX (scratch, saved)
		 * and build the 0/1 boolean in AX per the comparison op. */
		fprintf(f, "\tmov cx, ax\n");
		fprintf(f, "\txor ax, ax\n");
		switch (i->op) {
		case Oceqs:
			fprintf(f, "\tcmp cx, 0\n\tjne .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocnes:
			fprintf(f, "\tcmp cx, 0\n\tje .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Oclts:
			fprintf(f, "\tcmp cx, 0\n\tjge .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocles:
			fprintf(f, "\tcmp cx, 0\n\tjg .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tcmp cx, 2\n\tje .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocgts:
			fprintf(f, "\tcmp cx, 1\n\tjne .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocges:
			/* true iff cmp in {0,1} (ordered and a>=b); NaN(2) is false */
			fprintf(f, "\tcmp cx, 0\n\tje .Lsfc_true_%p\n", (void*)i);
			fprintf(f, "\tcmp cx, 1\n\tjne .Lsfc_done_%p\n", (void*)i);
			fprintf(f, ".Lsfc_true_%p:\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocos:   /* ordered: neither operand is NaN */
			fprintf(f, "\tcmp cx, 2\n\tje .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		case Ocuos:  /* unordered: some operand is NaN */
			fprintf(f, "\tcmp cx, 2\n\tjne .Lsfc_done_%p\n", (void*)i);
			fprintf(f, "\tmov ax, 1\n");
			break;
		default:
			die("i8086: unexpected soft-float compare op %d", i->op);
		}
		fprintf(f, ".Lsfc_done_%p:\n", (void*)i);

		store_ax_to(i->to, fn, f);
		if (save_dx_c) fprintf(f, "\tpop dx\n");
		fprintf(f, "\tpop cx\n");
		if (save_ax_c) fprintf(f, "\tpop ax\n");
		return;
	}

	/* (2) Ks arithmetic / negation.  (Ks load/copy/store already returned
	 * via the Kl 32-bit handlers above.)  Kd value ops die.  Oloadfs (a Ks
	 * far load) is excluded: it is a 32-bit far-pointer load, handled with
	 * the other far ops in the main switch below (shares Oloadfl). */
	if ((i->cls == Ks && i->op != Oloadfs) || i->cls == Kd) {
		if (i->cls == Kd)
			die("i8086: double (Kd) soft-float not implemented — single-precision only (op %d)", i->op);
		r0 = i->arg[0];
		r1 = i->arg[1];
		switch (i->op) {
		case Oadd:
			emit_sf_binop("_sf_add", r0, r1, i->to, fn, f);
			return;
		case Osub:
			emit_sf_binop("_sf_sub", r0, r1, i->to, fn, f);
			return;
		case Omul:
			emit_sf_binop("_sf_mul", r0, r1, i->to, fn, f);
			return;
		case Odiv:
			emit_sf_binop("_sf_div", r0, r1, i->to, fn, f);
			return;
		case Oneg:
			/* Negate = flip the IEEE sign bit (bit 31 = bit 15 of the
			 * high word).  Pure memory op, no helper, no AX/DX clobber
			 * of any concern beyond the scratch we save. */
			if (rtype(r0) != RSlot || rtype(i->to) != RSlot)
				die("i8086: soft-float neg operands must be slot-resident");
			{
			int save_ax_n = g_live_ax_after;
			if (save_ax_n) fprintf(f, "\tpush ax\n");
			fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
			fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			fprintf(f, "\txor ax, 0x8000\n");
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			if (save_ax_n) fprintf(f, "\tpop ax\n");
			}
			return;
		case Oexts:
		case Otruncd:
			die("i8086: float<->double conversion not implemented — single-precision soft-float only");
		case Oswtof:
		case Ouwtof:
		case Osltof:
		case Oultof:
		case Ocast:
			/* int->float / bitcast: result class is Ks but the lowering
			 * lives in the conversion switch below — fall through. */
			break;
		default:
			die("i8086: unsupported soft-float (Ks) op %d", i->op);
		}
	}

	/*
	 * (3) Integer <-> floating point conversions.
	 *   int -> float   : _sf_from_int (signed 32-bit -> Ks, result DX:AX)
	 *   float -> int   : _sf_to_int   (Ks -> signed 32-bit, low word to Kw)
	 * Double-precision conversions die.
	 */
	switch (i->op) {
	case Oswtof:   /* signed 16-bit -> float */
	case Ouwtof:   /* unsigned 16-bit -> float */
	case Osltof:   /* signed 32-bit -> float */
		r0 = i->arg[0];
		if (i->cls == Kd)
			die("i8086: int->double not implemented — single-precision soft-float only");
		if (rtype(i->to) != RSlot)
			die("i8086: soft-float conversion result must be slot-resident");
		{
		int save_ax_f = g_live_ax_after;
		int save_dx_f = g_live_dx_after;
		if (save_ax_f) fprintf(f, "\tpush ax\n");
		fprintf(f, "\tpush cx\n");
		if (save_dx_f) fprintf(f, "\tpush dx\n");

		/* Build the signed 32-bit argument in DX:AX (saved copies of the
		 * caller's AX/DX are already on the stack, so AX/DX are free). */
		if (i->op == Osltof) {
			if (rtype(r0) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov dx, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
			} else
				die("i8086: sltof source must be slot-resident");
		} else {
			if (rtype(r0) == RSlot)
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			else if (rtype(r0) == RTmp) {
				if (strcmp(rname[r0.val], "ax") != 0)
					fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
			} else
				die("i8086: wtof source must be slot or reg");
			if (i->op == Oswtof)
				fprintf(f, "\tcwd\n");          /* sign-extend AX -> DX:AX */
			else
				fprintf(f, "\txor dx, dx\n");   /* zero-extend (unsigned) */
		}
		fprintf(f, "\tpush dx\n");
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\tcall%s _sf_from_int\n", sf_farcall() ? " far" : "");
		fprintf(f, "\tadd sp, 4\n");
		fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);

		if (save_dx_f) fprintf(f, "\tpop dx\n");
		fprintf(f, "\tpop cx\n");
		if (save_ax_f) fprintf(f, "\tpop ax\n");
		}
		return;

	case Oultof:
		die("i8086: unsigned-32->float not implemented (sf_from_int is signed) — single-precision soft-float only");

	case Ostosi:   /* float -> signed int (low word) */
	case Ostoui:   /* float -> unsigned int (low word); truncation matches */
		r0 = i->arg[0];
		{
		int dst_in_ax_t = (rtype(i->to) == RTmp && i->to.val == RAX);
		int dst_in_dx_t = (rtype(i->to) == RTmp && i->to.val == RDX);
		int save_ax_t = !dst_in_ax_t && g_live_ax_after;
		int save_dx_t = !dst_in_dx_t && g_live_dx_after;
		if (save_ax_t) fprintf(f, "\tpush ax\n");
		fprintf(f, "\tpush cx\n");
		if (save_dx_t) fprintf(f, "\tpush dx\n");

		emit_push_long(r0, fn, f);   /* the Ks operand (slot) */
		fprintf(f, "\tcall%s _sf_to_int\n", sf_farcall() ? " far" : "");
		fprintf(f, "\tadd sp, 4\n");

		/* Result S32 in DX:AX.  A Kw destination takes the low word (AX);
		 * a Kl destination (float -> 32-bit long, e.g. mp_float_hash's
		 * (mp_int_t)val) takes the full DX:AX into its slot.  Move into dst
		 * BEFORE restoring the overlapping caller-save. */
		if (i->cls == Kl) {
			/* Kl float->long results are slot-resident (Kl invariant). */
			if (rtype(i->to) != RSlot)
				die("i8086: stosi/stoui Kl result must be slot-resident");
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
			fprintf(f, "\tmov word [bp%+ld], dx\n", (long)slot(i->to, fn) + 2);
		} else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		else if (rtype(i->to) == RTmp && !dst_in_ax_t) {
			if (dst_in_dx_t)
				fprintf(f, "\tmov dx, ax\n");
			else
				fprintf(f, "\tmov %s, ax\n", rname[i->to.val]);
		}

		if (save_dx_t) fprintf(f, "\tpop dx\n");
		fprintf(f, "\tpop cx\n");
		if (save_ax_t) fprintf(f, "\tpop ax\n");
		}
		return;

	case Odtosi:
	case Odtoui:
		die("i8086: double->int not implemented — single-precision soft-float only");

	case Ocast:
		/*
		 * Bitwise reinterpret between integer and single-precision float
		 * (32-bit) — pure byte copy, no FPU.  Kd/Kl (64-bit) bitcasts are
		 * unsupported (double).
		 */
		r0 = i->arg[0];
		if (i->cls == Ks || i->cls == Kw) {
			if (rtype(r0) == RSlot && rtype(i->to) == RSlot) {
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
				fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn) + 2);
				fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn) + 2);
			} else if (i->cls == Kw && rtype(r0) == RSlot && rtype(i->to) == RTmp) {
				if (strcmp(rname[i->to.val], "ax") != 0)
					fprintf(f, "\tmov %s, word [bp%+ld]\n", rname[i->to.val], (long)slot(r0, fn));
				else
					fprintf(f, "\tmov ax, word [bp%+ld]\n", (long)slot(r0, fn));
			} else
				die("i8086: unsupported soft-float bitcast operand shape");
		} else {
			die("i8086: double (Kd/Kl) bitcast not implemented — single-precision soft-float only");
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
		int bxsv_loadfb;
		fprintf(f, "\tpush es\n");
		bxsv_loadfb = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_loadfb, f);
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
		int bxsv_loadfw;
		fprintf(f, "\tpush es\n");
		bxsv_loadfw = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_loadfw, f);
		fprintf(f, "\tpop es\n");
		/* Store result */
		if (rtype(i->to) == RTmp)
			{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
		else if (rtype(i->to) == RSlot)
			fprintf(f, "\tmov word [bp%+ld], ax\n", (long)slot(i->to, fn));
		kl_restore_axdx(s_loadfw, f);
		}
		return;

	case Oloadfs:
		/*
		 * Load single-precision float (Ks) through a far pointer.  A Ks
		 * value is a 32-bit IEEE bit pattern, carried exactly like Kl
		 * (slot-resident DX:AX pair), so share the Oloadfl handler — the
		 * load is class-agnostic 32 bits.  See [[storefar-lacks-storefl]]
		 * extended to Ks ([[softfloat-spike]]).
		 */
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
		int bxsv_loadfl;
		fprintf(f, "\tpush es\n");
		bxsv_loadfl = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_loadfl, f);
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
		int bxsv_storefb;
		fprintf(f, "\tpush es\n");
		bxsv_storefb = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_storefb, f);
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
		int bxsv_storefw;
		fprintf(f, "\tpush es\n");
		bxsv_storefw = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_storefw, f);
		fprintf(f, "\tpop es\n");
		kl_restore_axdx(s_storefw, f);
		}
		return;

	case Ostorefs:
		/*
		 * Store single-precision float (Ks) through a far pointer.  A Ks
		 * value is a 32-bit IEEE bit pattern, written exactly like Kl, so
		 * share the Ostorefl handler — the store is class-agnostic 32 bits.
		 * See [[storefar-lacks-storefl]] extended to Ks ([[softfloat-spike]]).
		 */
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
		{
		int bxsv_storefl;
		fprintf(f, "\tpush ax\n");
		fprintf(f, "\tpush dx\n");
		fprintf(f, "\tpush es\n");
		bxsv_storefl = farptr_save_bx(i->to, f);
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
		farptr_restore_bx(bxsv_storefl, f);
		fprintf(f, "\tpop es\n");
		fprintf(f, "\tpop dx\n");
		fprintf(f, "\tpop ax\n");
		}
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

		/* HAZARD: idiv/div implicitly use DX:AX as the dividend, so a divisor
		 * that rega placed in AX or DX is destroyed by the `mov ax, dividend`
		 * / `cwd` setup below (it would silently divide by 0 or by AX).  Stage
		 * such a divisor into BX (push/pop preserved) before touching AX/DX. */
		if (rtype(r1) == RTmp && (r1.val == RAX || r1.val == RDX)) {
			fprintf(f, "\tpush bx\n");
			if (rtype(r0) == RTmp && r0.val == RBX) {
				/* dividend in BX, divisor in AX/DX: get both into place
				 * without clobbering either source */
				if (r1.val == RAX)
					fprintf(f, "\txchg ax, bx\n");   /* ax<-dividend, bx<-divisor */
				else {
					fprintf(f, "\tmov ax, bx\n");    /* ax<-dividend */
					fprintf(f, "\tmov bx, dx\n");    /* bx<-divisor */
				}
			} else {
				fprintf(f, "\tmov bx, %s\n", rname[r1.val]);  /* capture divisor first */
				if (!(rtype(r0) == RTmp && r0.val == RAX)) {
					if (rtype(r0) == RTmp)
						fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
					else if (rtype(r0) == RCon)
						fprintf(f, "\tmov ax, %"PRIi64"\n", fn->con[r0.val].bits.i);
				}
			}
			fprintf(f, "\tcwd\n");
			fprintf(f, "\tidiv bx\n");
			fprintf(f, "\tpop bx\n");
			if (i->op == Odiv) {
				if (rtype(i->to) == RTmp && i->to.val != RAX)
					{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			} else {
				if (rtype(i->to) == RTmp)
					fprintf(f, "\tmov %s, dx\n", rname[i->to.val]);
			}
			return;
		}

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

		/* HAZARD (see signed path above): a divisor in AX/DX is destroyed by
		 * the `mov ax, dividend` / `xor dx, dx` DX:AX setup.  Stage it to BX. */
		if (rtype(r1) == RTmp && (r1.val == RAX || r1.val == RDX)) {
			fprintf(f, "\tpush bx\n");
			if (rtype(r0) == RTmp && r0.val == RBX) {
				if (r1.val == RAX)
					fprintf(f, "\txchg ax, bx\n");   /* ax<-dividend, bx<-divisor */
				else {
					fprintf(f, "\tmov ax, bx\n");    /* ax<-dividend */
					fprintf(f, "\tmov bx, dx\n");    /* bx<-divisor */
				}
			} else {
				fprintf(f, "\tmov bx, %s\n", rname[r1.val]);  /* capture divisor first */
				if (!(rtype(r0) == RTmp && r0.val == RAX)) {
					if (rtype(r0) == RTmp)
						fprintf(f, "\tmov ax, %s\n", rname[r0.val]);
					else if (rtype(r0) == RCon)
						fprintf(f, "\tmov ax, %"PRIi64"\n", fn->con[r0.val].bits.i);
				}
			}
			fprintf(f, "\txor dx, dx\n");
			fprintf(f, "\tdiv bx\n");
			fprintf(f, "\tpop bx\n");
			if (i->op == Oudiv) {
				if (rtype(i->to) == RTmp && i->to.val != RAX)
					{ if (strcmp(rname[i->to.val], "ax") != 0) fprintf(f, "\tmov %s, ax\n", rname[i->to.val]); }
			} else {
				if (rtype(i->to) == RTmp)
					fprintf(f, "\tmov %s, dx\n", rname[i->to.val]);
			}
			return;
		}

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

		/* Precompute AX/DX live-after for each instruction so the save
		 * brackets can be dropped where the register is dead (see
		 * compute_axdx_liveafter).  Buffers grow as needed across blocks. */
		if (b->nins > la_cap) {
			la_cap = b->nins;
			la_ax_buf = realloc(la_ax_buf, la_cap);
			la_dx_buf = realloc(la_dx_buf, la_cap);
			la_bx_buf = realloc(la_bx_buf, la_cap);
			if (!la_ax_buf || !la_dx_buf || !la_bx_buf)
				die("emit: out of memory for liveness buffers");
		}
		compute_axdx_liveafter(b, fn, la_ax_buf, la_dx_buf, la_bx_buf);

		for (i = b->ins; i < &b->ins[b->nins]; i++) {
			int idx = (int)(i - b->ins);
			g_live_ax_after = la_ax_buf[idx];
			g_live_dx_after = la_dx_buf[idx];
			g_live_bx_after = la_bx_buf[idx];
			emitins(i, fn, f);
		}
		g_live_ax_after = 1;
		g_live_dx_after = 1;
		g_live_bx_after = 1;

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
