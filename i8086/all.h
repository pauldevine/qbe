#include "../all.h"

typedef struct I8086Op I8086Op;

/* 8086/286/386 16-bit registers */
enum I8086Reg {
	/* General purpose 16-bit registers */
	/* caller-save (by convention) */
	RAX = RXX + 1,  /* AX - accumulator */
	RCX,            /* CX - counter */
	RDX,            /* DX - data */

	/* callee-save (by convention) */
	RBX,            /* BX - base */
	RSI,            /* SI - source index */
	RDI,            /* DI - destination index */

	/* globally live */
	RBP,            /* BP - base pointer (frame pointer) */
	RSP,            /* SP - stack pointer */

	/* No FPU registers initially - can add 8087 support later */

	NFPR = 0,       /* no floating point registers */
	NGPR = RSP - RAX + 1,
	NGPS = RDX - RAX + 1,  /* caller-save GPRs */
	NFPS = 0,
	NCLR = RDI - RBX + 1,  /* callee-save GPRs */
};
MAKESURE(reg_not_tmp, RSP < (int)Tmp0);

struct I8086Op {
	char nmem;  /* number of memory operands allowed */
	char zflag; /* sets zero flag */
	char wide;  /* word vs byte operation */
};

/* targ.c */
extern int i8086_rsave[];
extern int i8086_rclob[];
extern I8086Op i8086_op[];

/* abi.c */
bits i8086_retregs(Ref, int[2]);
bits i8086_argregs(Ref, int[2]);
void i8086_abi(Fn *);

/* isel.c */
void i8086_isel(Fn *);

/* emit.c */
void i8086_emitfn(Fn *, FILE *);
