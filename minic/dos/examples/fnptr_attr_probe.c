/*
 * fnptr_attr_probe.c -- the §9c bounded follow-on to §9a/§9b's file-scope
 * function-pointer VARIABLE grammar: an __attribute__-QUALIFIED pointee.
 * §9a added the single-declarator forms and §9b the multi-declarator comma
 * form plus a leading __far, but
 *
 *     void __attribute__((interrupt)) (*v)(void);          // attribute
 *     void __far __attribute__((interrupt)) (*v)(void);    // far + attribute
 *     void __attribute__((interrupt)) __far (*v)(void);    // attribute + far
 *
 * were still hard parse errors (§9b deliberately admitted ONLY TFAR at file
 * scope, because reusing the §8r fp_attr nonterminal -- whose separate empty
 * save-marker collided reduce/reduce with attrreset -- was the path it
 * avoided).  This is also a real latent consumer: newlibc's interrupts.h
 * spells a far ISR `void __far __attribute__((interrupt)) ...`, and §8w had to
 * sidestep the fn-ptr-VARIABLE equivalent in test_timer_dos by declaring its
 * handler as a plain `static void __far *`.
 *
 * §9c restructures gfnptr_decl around a non-nullable `gfnptr_quals` run that
 * subsumes TFAR and adds a `gfnptr_attr` reusing attropt's OWN `attrreset`
 * empty marker, so the attribute is parsed by the same item sequence as
 * attropt and distinguished only by the token after the closing `))` (IDENT
 * continues typed_decl, `(` continues this fn-ptr declarator).  Every
 * qualifier is accepted and DROPPED: __far is a memory-model property and an
 * interrupt/weak attribute on a pointer VARIABLE has no codegen meaning (the
 * ISR ABI lives on a function DEFINITION's linkage, not a pointee type).
 *
 * Bug-loud: on the §9b compiler the first attribute declaration below is a
 * parse error, so the program does not even build (confirmed by git-stashing
 * the §9c minic.y change).  The values are dispatch results, not pointer
 * addresses, so the golden is model-independent (near-code small/compact vs
 * far-code medium/large/huge).  Gated small + medium + compact + large + huge.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/fnptr_attr_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/fnptr_attr_probe/fnptr_attr_probe.exe \
 *             | diff - minic/dos/tests/fnptr_attr_probe.golden.txt
 */

#include <stdio.h>

static int inc(int x)   { return x + 1; }
static int dec(int x)   { return x - 1; }
static int dbl(int x)   { return x * 2; }
static int neg(int x)   { return -x; }
static void hello(void) { printf("hello ran\n"); }

/* (A) plain attribute -- the headline §9c form. */
void __attribute__((interrupt)) (*a_isr)(void);

/* (B) static attribute (internal linkage). */
static int __attribute__((weak)) (*a_sw)(int);

/* (C) __far + __attribute__ (the interrupts.h far-ISR spelling). */
void __far __attribute__((interrupt)) (*a_fisr)(void);

/* (D) __attribute__ + __far (the qualifiers in the other order). */
int __attribute__((weak)) __far (*a_afc)(int);

/* (E) multi-declarator sharing one attributed return type. */
int __attribute__((weak)) (*a_w1)(int), (*a_w2)(int);

/* (F) attribute pointee with a per-declarator function-address initializer. */
int __attribute__((weak)) (*a_mi)(int) = dbl;

/*
 * (G) the interrupt attribute on the VARIABLE above must not leak to a
 * following real interrupt function (which keeps its ISR linkage) nor to a
 * plain function (which stays plain) -- a §8r-class concern.  We can't observe
 * linkage at run time, but exercising both keeps the build coherent and proves
 * the attribute state did not corrupt the surrounding definitions.
 */
static int after_attr(int x) { return x + 100; }

int main(void)
{
	/* (A) assigned at run time, dispatched. */
	a_isr = hello;
	a_isr();

	/* (B) static, distinct storage. */
	a_sw = inc;
	printf("a_sw=%d\n", a_sw(20));

	/* (C) far + attribute. */
	a_fisr = hello;
	a_fisr();

	/* (D) attribute + far. */
	a_afc = dec;
	printf("a_afc=%d\n", a_afc(20));

	/* (E) multi-declarator, distinct per-declarator storage. */
	a_w1 = dbl;
	a_w2 = neg;
	printf("a_w1=%d a_w2=%d\n", a_w1(7), a_w2(7));

	/* (F) per-declarator initializer took effect. */
	printf("a_mi=%d\n", a_mi(5));

	/* (G) surrounding definitions undisturbed. */
	printf("after_attr=%d\n", after_attr(1));

	printf("fnptr_attr_probe done\n");
	return 0;
}
