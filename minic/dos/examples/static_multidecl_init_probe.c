/*
 * static_multidecl_init_probe.c -- function-local `static` MULTI-declarators
 * WITH per-item initializers (the bounded gap §8h left open).  §8h closed the
 * UNinitialized static multi-decl (`static int x, y;`); an INITIALIZED one
 *
 *   static int x = 1, y = 2;          (plain scalars, both initialized)
 *   static int a = 100, b;            (init-first, then uninitialized)
 *   static int p, q = 5;              (uninit-first, then initialized item)
 *   static char *s1 = "ab", *s2 = "x"; (uniform pointers, both initialized)
 *   static long n = 100000L, m = 1;   (long width)
 *   static int x = 1, arr[3];          (init scalar + uninitialized array item)
 *
 * was still a hard parse error: the `STATIC type IDENT ...` rules captured the
 * first declarator as a bare IDENT (no `= expr`), and the §8h rest-item helper
 * rejected any per-item initializer with a die.
 *
 * The fix adds two productions — `dcls STATIC type IDENT '=' expr ','
 * ext_decllist ';'` and its statement-scope twin — capturing the first
 * declarator's initializer, and teaches emit_static_local_rest_item to FOLD a
 * scalar/pointer rest-item initializer into its own mangled file-scope data
 * block via emit_static_local_init (the same const-folding the single
 * `static T v = init;` rule uses).  Bug-loud: on the UNFIXED compiler every
 * function below is a parse error, so the program will not even build.
 *
 * Each initialized `static` is exercised for (a) distinct storage,
 * (b) correct type/size, and (c) the initial value PLUS persistence across
 * calls (a folded static keeps its value between calls) — a mis-shared,
 * mis-sized, or non-folded slot would corrupt the running tallies.
 *
 * Model-independent (program output only); gated medium + compact + large.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/static_multidecl_init_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/static_multidecl_init_probe/static_multidecl_init_probe.exe \
 *             | diff - minic/dos/tests/static_multidecl_init_probe.golden.txt
 */

#include <stdio.h>

/* (1) plain scalars, both initialized: distinct storage, initial values,
 * persistence.  First call returns (1+1)+(2+10)=14, then 16, 18. */
static int
twoinit(void)
{
	static int x = 1, y = 2;        /* dcls STATIC init-first multi */
	x += 1;
	y += 1;
	return x + y;                   /* 5, 7, 9 over three calls */
}

/* (2) init-first then uninitialized: a starts 100, b starts 0. */
static int
initfirst_then_uninit(void)
{
	static int a = 100, b;          /* b is zero-initialized */
	a += 1;
	b += 5;
	return a + b;                   /* 106, 112, 118 */
}

/* (3) uninit-first then an initialized rest item (the §8h rest helper now
 * folds the init instead of dying): p starts 0, q starts 5. */
static int
uninit_then_init(void)
{
	static int p, q = 5;
	p += 2;
	q += 3;
	return p + q;                   /* 10, 15, 20 */
}

/* (4) uniform pointers, both initialized: both char*, distinct strings. */
static int
twoptrs_init(void)
{
	static char *s1 = "victor", *s2 = "9000";
	return (int)s1[0] + (int)s2[0]; /* 'v'(118) + '9'(57) = 175 */
}

/* (5) long width, both initialized. */
static long
twolong(void)
{
	static long n = 100000L, m = 1L;
	n += 1;
	m += 100000L;
	return n + m;                   /* 200002, 300003, 400004 */
}

/* (6) initialized scalar + uninitialized array item in one multi-decl. */
static int
init_plus_array(void)
{
	static int x = 7, arr[3];       /* x folds to 7; arr is 3 zero ints */
	x += 1;
	arr[0] = x;
	arr[1] = x * 2;
	arr[2] = x * 3;
	return x + arr[0] + arr[1] + arr[2];   /* x + 6x = 7x -> 56, 63 */
}

/* (7) statement-scope (mid-block) initialized static multi-decl. */
static int
stmtscope_init(void)
{
	int seed = 3;
	if (seed > 0) {
		static int u = 7, v = 8;
		u += 1;
		v += 1;
		return u + v;           /* 17, 19 */
	}
	return -1;
}

int
main(void)
{
	printf("twoinit=%d,%d,%d\n", twoinit(), twoinit(), twoinit());
	printf("initfirst=%d,%d,%d\n",
	       initfirst_then_uninit(), initfirst_then_uninit(),
	       initfirst_then_uninit());
	printf("uninitfirst=%d,%d,%d\n",
	       uninit_then_init(), uninit_then_init(), uninit_then_init());
	printf("twoptrs=%d\n", twoptrs_init());
	printf("twolong=%ld,%ld,%ld\n", twolong(), twolong(), twolong());
	printf("initarr=%d,%d\n", init_plus_array(), init_plus_array());
	printf("stmtscope=%d,%d\n", stmtscope_init(), stmtscope_init());
	return 0;
}
