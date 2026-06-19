/*
 * fnptr_multi_probe.c -- the §9b bounded follow-ons to §9a's file-scope
 * function-pointer VARIABLE grammar: the MULTI-DECLARATOR comma form and the
 * __far-QUALIFIED POINTEE.  §9a added only the single-declarator forms
 * (`void (*v)(void);`, plus static and function-address-initialized), so
 *
 *     int (*a)(int), (*b)(int);        // multi-declarator
 *     void __far (*v)(void);           // far-qualified pointee
 *
 * were still hard parse errors.  §9b refactors gfnptrdcl into a shared
 * `gfnptr_decllist` so the declaration's one return type applies to every
 * comma-separated declarator (each optionally function-address-initialized),
 * and admits a leading TFAR on the pointee (accepted and dropped, exactly as
 * the §8r fn-ptr parameter rule does -- the far calling convention is a
 * memory-model property on this toolchain, so the pointer type is identical
 * to the unqualified declarator regardless of model).
 *
 * Bug-loud: on the §9a compiler every declaration below except a plain single
 * fn-ptr is a parse error, so the program does not even build (confirmed by
 * git-stashing the §9b minic.y change).
 *
 * The values are computed (dispatch results), not pointer addresses, so the
 * golden is model-independent (near-code small/compact vs far-code
 * medium/large/huge).  Gated small + medium + compact + large + huge.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/fnptr_multi_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/fnptr_multi_probe/fnptr_multi_probe.exe \
 *             | diff - minic/dos/tests/fnptr_multi_probe.golden.txt
 */

#include <stdio.h>

static int inc(int x)   { return x + 1; }
static int dec(int x)   { return x - 1; }
static int dbl(int x)   { return x * 2; }
static int neg(int x)   { return -x; }
static void hello(void) { printf("hello ran\n"); }

/* (A) multi-declarator, plain, uninitialized -- the headline §9b form. */
int (*m1)(int), (*m2)(int);

/* (B) multi-declarator, static (internal linkage), uninitialized. */
static int (*sm1)(int), (*sm2)(int);

/* (C) multi-declarator with a per-item function-address initializer. */
int (*mi1)(int) = dbl, (*mi2)(int) = neg;

/* (D) __far-qualified pointee, plain + function-address-initialized. */
void __far (*fv)(void) = hello;
int  __far (*fc)(int)  = inc;

int main(void)
{
	/* (A) assigned at run time. */
	m1 = inc;
	m2 = dec;
	printf("m1=%d m2=%d\n", m1(10), m2(10));

	/* (B) static, assigned at run time -- distinct storage per declarator. */
	sm1 = dbl;
	sm2 = neg;
	printf("sm1=%d sm2=%d\n", sm1(7), sm2(7));

	/* (C) per-item initializers took effect independently. */
	printf("mi1=%d mi2=%d\n", mi1(5), mi2(5));

	/* (D) __far-qualified pointee, dispatched through the file-scope init. */
	fv();
	printf("fc=%d\n", fc(41));

	printf("fnptr_multi_probe done\n");
	return 0;
}
