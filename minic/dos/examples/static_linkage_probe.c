/*
 * static_linkage_probe.c -- C `static` functions have internal linkage.
 *
 * minic used to emit `export function` for EVERY function definition, and
 * tools/asm_to_omf.py then auto-promoted every `_xxx:` code label to a public
 * OMF symbol.  So a `static inline` helper in a shared header (e.g.
 * MicroPython's utf8_get_char in py/misc.h) was exported from every TU that
 * included it, and the linker rejected the duplicate publics.
 *
 * The fix: minic emits a module-local `function` (no `.globl`) for a `static`
 * function, and asm_to_omf.py no longer auto-promotes code labels (it trusts
 * minic's `.globl` for functions; data still auto-promotes).
 *
 * This probe is the REGRESSION GUARD for the codegen half: a now-local static
 * function must still be reachable through the medium-model far-call path
 * (`call far _helper` to a non-global symbol), including nested static->static
 * calls and an indirect call through a function pointer to a static function.
 * If the local far-call fixup were broken, the answers would be wrong.
 *
 * (The duplicate-public-symbol half is validated by the MicroPython core link:
 * all 106 curated TUs that share static-inline header helpers now link without
 * collision.  That isn't reproducible in the single-TU gate harness.)
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/static_linkage_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/static_linkage_probe/static_linkage_probe.exe \
 *             | diff - minic/dos/tests/static_linkage_probe.golden.txt
 */

#include <stdio.h>

/* A plain static helper (internal linkage). */
static int
inc(int x)
{
	return x + 1;
}

/* A static helper that calls another static helper (static->static far call). */
static int
inc2(int x)
{
	return inc(inc(x));
}

/* Static recursion through the local symbol. */
static int
fact(int n)
{
	if (n <= 1)
		return 1;
	return n * fact(n - 1);
}

/* A name that would collide as a duplicate public if it were exported and the
 * same static-inline appeared in another TU; here it just proves the local
 * symbol is callable. */
static int
clampw(int x)
{
	if (x < 0)
		return 0;
	if (x > 255)
		return 255;
	return x;
}

typedef int (*ifn)(int);

/* A NON-static function — must stay exported (cross-TU callable). */
int
exported_double(int x)
{
	return x * 2;
}

int
main(void)
{
	ifn fp = clampw;          /* function pointer to a static function */

	printf("inc(41)=%d\n", inc(41));
	printf("inc2(40)=%d\n", inc2(40));
	printf("fact(5)=%d\n", fact(5));
	printf("clampw(300)=%d\n", clampw(300));
	printf("fp(-7)=%d\n", fp(-7));
	printf("fp(100)=%d\n", fp(100));
	printf("exported_double(21)=%d\n", exported_double(21));
	return 0;
}
