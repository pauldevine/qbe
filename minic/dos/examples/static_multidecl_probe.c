/*
 * static_multidecl_probe.c -- function-local `static` MULTI-declarators
 * (the last carried bounded grammar gap after §8g closed the aoa instance
 * sites).  This is NOT aoa-specific: the `dcls STATIC type IDENT ...` and
 * statement-scope `STATIC type IDENT ...` rules carried only single-
 * declarator productions, so EVERY multi-declarator static was a hard
 * parse error, plain `int` included:
 *
 *   static int x, y;          (plain scalars)
 *   static int a[3], b;       (array-FIRST, then scalar)
 *   static int a, b[3];       (scalar-first, then array item)
 *   static char *p, *q;       (uniform pointers)
 *   static char *p, c;        (pointer + peeled scalar)
 *   static jmp_buf a, b;      (array-typedef instances)
 *
 * The fix adds plain-first (`, ext_decllist`) and array-first
 * (`[expr] , ext_decllist`) multi-decl productions to both the dcls and
 * statement-scope STATIC rules; each declarator is emitted as its own
 * mangled file-scope data global via emit_static_local, with uniform-*
 * peeling for the trailing items and aoa-awareness for array-typedef
 * bases.  Bug-loud: on the UNFIXED compiler every function below is a
 * parse error, so the program will not even build.
 *
 * Each `static` is exercised for (a) distinct storage, (b) correct type
 * and size, and (c) persistence across calls — a mis-shared or mis-sized
 * slot would corrupt the running tallies or the setjmp env.
 *
 * Model-independent (program output only); gated medium + compact + large.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/static_multidecl_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/static_multidecl_probe/static_multidecl_probe.exe \
 *             | diff - minic/dos/tests/static_multidecl_probe.golden.txt
 */

#include <stdio.h>
#include <setjmp.h>

/* (1) plain scalars: distinct storage + persistence across calls. */
static int
tick(void)
{
	static int a, b;                /* top-of-block dcls STATIC multi */
	a += 1;
	b += 10;
	return a + b;                   /* 11, 22, 33 over three calls */
}

/* (2) array-FIRST then scalar. */
static int
arrfirst(void)
{
	static int arr[3], n;           /* arr is 3 ints; n is one int */
	n += 1;
	arr[0] = n;
	arr[1] = n * 2;
	arr[2] = n * 3;
	return arr[0] + arr[1] + arr[2] + n;   /* 6n + n = 7n -> 7,14 */
}

/* (3) scalar-first then array item. */
static int
scalfirst(void)
{
	static int s, vec[3];           /* s is one int; vec is 3 ints */
	s += 100;
	vec[0] = 1;
	vec[1] = 2;
	vec[2] = 3;
	return s + vec[0] + vec[1] + vec[2];   /* 106, 206 */
}

/* (4) uniform pointers: both char*, distinct. */
static int
twoptrs(void)
{
	static char *p, *q;             /* both char *, NOT char */
	p = "victor";
	q = "9000";
	return (int)(p[0]) + (int)(q[0]);      /* 'v'(118) + '9'(57) = 175 */
}

/* (5) pointer + peeled scalar: p is char*, c is char. */
static int
ptrscalar(void)
{
	static char *p, c;              /* p is char *, c is a single char */
	p = "hi";
	c = (char)(c + 1);              /* persists: 1, 2, 3 */
	return (int)(p[0]) + (int)c;    /* 'h'(104) + 1/2/3 */
}

/* (6) array-typedef instances in one multi-decl. */
static int
jmpmulti(int useb)
{
	static jmp_buf ja, jb;          /* each is int[8] = 16 bytes */
	if (useb) {
		int r = setjmp(jb);
		if (r == 0)
			longjmp(jb, 72);
		return r;               /* 72 */
	} else {
		int r = setjmp(ja);
		if (r == 0)
			longjmp(ja, 71);
		return r;               /* 71 */
	}
}

/* (7) statement-scope (mid-block) static multi-decl. */
static int
stmtscope(void)
{
	int seed = 5;
	if (seed > 0) {
		static int m, n;        /* statement-scope STATIC multi */
		m += 2;
		n += 3;
		return m + n;           /* 5, 10 */
	}
	return -1;
}

int
main(void)
{
	printf("tick=%d,%d,%d\n", tick(), tick(), tick());
	printf("arrfirst=%d,%d\n", arrfirst(), arrfirst());
	printf("scalfirst=%d,%d\n", scalfirst(), scalfirst());
	printf("twoptrs=%d\n", twoptrs());
	printf("ptrscalar=%d,%d,%d\n", ptrscalar(), ptrscalar(), ptrscalar());
	printf("jmpmulti=%d,%d\n", jmpmulti(0), jmpmulti(1));
	printf("stmtscope=%d,%d\n", stmtscope(), stmtscope());
	return 0;
}
