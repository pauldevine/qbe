/*
 * fnptrprobe.c — function-pointer coverage for compact memory model.
 *
 * Background:
 *   Pre-fix, compact mode encoded function pointers as Kw (CODEPTR_T='w',
 *   2 bytes) because NEAR_CODE() included MCompact.  But i8086 selcall
 *   unconditionally emits Ocallfar when uses_far_code() (compact/medium/
 *   large/huge), and the indirect-call path expects DX:AX to hold the
 *   target.  A Kw fn-ptr only fills one register; the other half of the
 *   call target is whatever leftover sat in DX → retf to garbage → hang.
 *
 *   QBE constant-folds `fp = adder; fp(x,y)` into a direct `call far`,
 *   so the bug only manifested through real indirect calls (a fn-ptr
 *   parameter or table lookup that the optimizer can't fold).
 *
 * Fix (this session): drop MCompact from NEAR_CODE() in minic.y so
 * CODEPTR_T() returns 'l' for compact, matching the far-code ABI.
 *
 * Validation rule: one helper-returning value per printf line
 * ([[i8086-compact-loadfb-aliases-ax]]).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/fnptrprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/fnptrprobe/fnptrprobe.exe
 */

#include <stdio.h>
#include <string.h>

static int adder(int a, int b)   { return a + b; }
static int subtr(int a, int b)   { return a - b; }
static int mulr (int a, int b)   { return a * b; }

typedef int (*binop_t)(int, int);
typedef char *(*scan_t)(char *);

/* In compact, the str param is a 4-byte __far char *.  This proves the
 * indirect-call ABI agrees on Kl pointer args. */
static char *skip_space(char *s)
{
	while (*s == ' ')
		++s;
	return s;
}

static int strlen_local(char *s)
{
	int n;
	n = 0;
	while (*s) {
		++n;
		++s;
	}
	return n;
}

/* Parameter `op` is opaque to constant propagation — apply must emit a
 * real indirect call. */
static int apply(binop_t op, int x, int y)
{
	return op(x, y);
}

/* Indirect call returning a far data pointer — the call result is Kl
 * (DX:AX).  Confirms that the indirect-call path forwards the wide
 * result correctly. */
static int scan_first(scan_t f, char *buf)
{
	char *p;
	p = f(buf);
	return *p;
}

/* (4) Struct with inline fn-ptr member (literal syntax, no typedef). */
struct dispatch {
	int (*op)(int, int);
	int tag;
};

int main()
{
	int r;
	char buf[16];

	/* (1) Indirect call via parameter — defeats constant folding. */
	r = apply(adder, 40, 2);
	printf("apply_add=%d (want 42)\r\n", r);

	r = apply(subtr, 50, 8);
	printf("apply_sub=%d (want 42)\r\n", r);

	r = apply(mulr, 6, 7);
	printf("apply_mul=%d (want 42)\r\n", r);

	/* (2) Indirect call whose target returns a far data pointer.
	 * Confirms the far-ptr return ABI works across the indirect edge. */
	strcpy(buf, "    hello");
	r = scan_first(skip_space, buf);
	printf("scan_first=%d (want 104)\r\n", r);   /* 'h' */

	/* (3) Local fn-ptr variable, written then read.  QBE may inline
	 * across straight-line code, but copy-through-storel exercises the
	 * 4-byte slot store/load. */
	{
		binop_t fp;
		fp = adder;
		r = apply(fp, 10, 32);
		printf("local_fp=%d (want 42)\r\n", r);
	}

	/* (4) Declarator with initializer:  int (*fp)(int,int) = adder; */
	{
		int (*fp)(int, int) = adder;
		r = apply(fp, 20, 22);
		printf("init_decl=%d (want 42)\r\n", r);
	}

	/* (5) table[i](args) — indirect call via array subscript. */
	{
		binop_t table[3];
		table[0] = adder;
		table[1] = subtr;
		table[2] = mulr;
		r = table[0](40, 2);
		printf("table0=%d (want 42)\r\n", r);
		r = table[1](50, 8);
		printf("table1=%d (want 42)\r\n", r);
		r = table[2](7, 6);
		printf("table2=%d (want 42)\r\n", r);
	}

	/* (6) Struct with inline fn-ptr member literal. */
	{
		struct dispatch d;
		d.op = adder;
		d.tag = 0;
		r = d.op(38, 4);
		printf("struct_fp=%d (want 42)\r\n", r);
	}

	return 0;
}
