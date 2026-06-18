/*
 * file_fnptr_probe.c -- FILE-SCOPE function-pointer VARIABLE declarations
 * (§9a, a general minic grammar hole).  minic accepted a fn-ptr type only via
 * a typedef (`typedef void (*fp_t)(void); fp_t g;`), an `extern` declaration
 * (`extern int (*cb)(int,int);`), or inside a function body / function-top
 * `dcls`.  A plain file-scope DEFINITION
 *
 *     void (*v)(void);
 *     static int (*cmp)(int, int) = adder;
 *
 * had no production at the `prog` level (typed_decl starts with `type IDENT`,
 * which a `type '(' '*' IDENT ')' ...` declarator can never match), so it was
 * a hard parse error.  The fix adds a `gfnptrdcl` nonterminal (plain + static,
 * each with and without an initializer) that emits a zero- or symbol-
 * initialized DATA global and records the fn-ptr prototype id (so an indirect
 * call coerces its arguments).
 *
 * Bug-loud: on the UNFIXED compiler the four file-scope fn-ptr declarations
 * below are a parse error, so the program does not even build.
 *
 * The values are computed (sums/dispatch results), not pointer addresses, so
 * the golden is model-independent (near-code small/compact vs far-code
 * medium/large/huge -- the far code-pointer static init `{ l $sym }` is split
 * into offset+segment words by asm_to_omf.py, the §6k/§7h path).  The static
 * forms also prove internal linkage emits as plain `data` (no `.globl`) and
 * still works.  Gated small + medium + compact + large + huge.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/file_fnptr_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/file_fnptr_probe/file_fnptr_probe.exe \
 *             | diff - minic/dos/tests/file_fnptr_probe.golden.txt
 */

#include <stdio.h>

static int adder(int a, int b) { return a + b; }
static int suber(int a, int b) { return a - b; }
static void marker(void)       { printf("marker ran\n"); }

/* (1) plain, uninitialized: assigned at run time. */
int (*g_op)(int, int);

/* (2) static (internal linkage), uninitialized. */
static int (*s_op)(int, int);

/* (3) plain, initialized with a function address. */
void (*g_void)(void) = marker;

/* (4) static, initialized with a function address. */
static int (*s_init)(int, int) = adder;

int main(void)
{
	/* (1) plain fn-ptr, run-time assignment + reassignment. */
	g_op = adder;
	printf("g_op add=%d\n", g_op(8, 5));
	g_op = suber;
	printf("g_op sub=%d\n", g_op(8, 5));

	/* (2) static fn-ptr, run-time assignment. */
	s_op = suber;
	printf("s_op sub=%d\n", s_op(20, 7));

	/* (3) plain fn-ptr initialized at file scope. */
	g_void();

	/* (4) static fn-ptr initialized at file scope. */
	printf("s_init add=%d\n", s_init(3, 4));

	printf("file_fnptr_probe done\n");
	return 0;
}
