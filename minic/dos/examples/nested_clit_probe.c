/*
 * nested_clit_probe.c -- compound literal with a NESTED brace initializer,
 * including assignment through a deref.  Needed by py/objtype.c's
 *   *o = (mp_obj_super_t) {{type_in}, args[0], args[1]};
 * where the first member is a sub-struct (mp_obj_base_t) filled by `{…}`.
 *
 * `inititem` now accepts `'{' initlist '}'` (and `.field = '{' … '}'`), and
 * both the expr() and lval() compound-literal paths fill members through the
 * shared recursive emit_clit_aggr(), which descends into a sub-struct/union
 * member on a nested-brace item.  The lval() path matters because a struct
 * compound literal on the RHS of `*p = …` is re-materialised via lval() to
 * obtain its address for the struct copy.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/nested_clit_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/nested_clit_probe/nested_clit_probe.exe \
 *             | diff - minic/dos/tests/nested_clit_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

typedef struct { int t; } base_t;
typedef struct { base_t base; int a; int b; } super_t;

/* deeper nesting + a designated nested brace */
typedef struct { int lo; int hi; } range_t;
typedef struct { int tag; range_t r; int z; } box_t;

static void
fill_super(super_t *o, int x)
{
	/* nested brace through a deref assignment (the objtype shape) */
	*o = (super_t){{x}, x + 1, x + 2};
}

int
main(void)
{
	super_t s;
	box_t b;
	super_t direct;

	/* (a) nested brace via deref assignment. */
	fill_super(&s, 10);
	printf("a=%d,%d,%d\r\n", s.base.t, s.a, s.b);          /* 10,11,12 */

	/* (b) nested brace as a direct local compound-literal initializer. */
	direct = (super_t){{7}, 8, 9};
	printf("b=%d,%d,%d\r\n", direct.base.t, direct.a, direct.b);  /* 7,8,9 */

	/* (c) designated nested brace + trailing scalar. */
	b = (box_t){.tag = 1, .r = {4, 5}, .z = 6};
	printf("c=%d,%d,%d,%d\r\n", b.tag, b.r.lo, b.r.hi, b.z);  /* 1,4,5,6 */

	return 0;
}
