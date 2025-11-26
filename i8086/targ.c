#include "all.h"

I8086Op i8086_op[NOp] = {
#define O(op, t, x) [O##op] =
#define X(nm, zf, wf) { nm, zf, wf },
	#include "../ops.h"
};

/* Caller-save registers (can be clobbered by function calls) */
int i8086_rsave[] = {
	RAX, RCX, RDX,
	-1
};

/* Callee-save registers (must be preserved across function calls) */
int i8086_rclob[] = {
	RBX, RSI, RDI,
	-1
};

#define RGLOB (BIT(RBP) | BIT(RSP))

static int
i8086_memargs(int op)
{
	/* Most x86 instructions can have one memory operand */
	return (op >= Oload && op <= Ostorel) ? 1 : i8086_op[op].nmem;
}

Target T_i8086 = {
	.name = "i8086",
	.memmodel = Msmall,  /* Default to small memory model */
	.gpr0 = RAX,
	.ngpr = NGPR,
	.fpr0 = 0,  /* no FPU initially */
	.nfpr = NFPR,
	.rglob = RGLOB,
	.nrglob = 2,
	.rsave = i8086_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = i8086_retregs,
	.argregs = i8086_argregs,
	.memargs = i8086_memargs,
	.abi0 = elimsb,
	.abi1 = i8086_abi,
	.isel = i8086_isel,
	.emitfn = i8086_emitfn,
	.emitfin = elf_emitfin,  /* TODO: maybe need custom output format */
	.asloc = ".L",
	.assym = "_",  /* DOS/OMF conventionally prefixes symbols with _ */
};

MAKESURE(rsave_size_ok, sizeof i8086_rsave == (NGPS+NFPS+1) * sizeof(int));
MAKESURE(rclob_size_ok, sizeof i8086_rclob == (NCLR+1) * sizeof(int));
