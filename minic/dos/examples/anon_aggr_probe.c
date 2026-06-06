/*
 * anon_aggr_probe.c -- anonymous struct/union used directly as a type, and
 * a function-local anonymous enum.  Needed by the MicroPython port:
 *   - py/binary.c   inline offsetof cast `(size_t)&((struct{…}*)0)->t`
 *   - py/objlist.c  anon-struct-typed local `struct{…} args;`
 *   - py/modbuiltins.c  local `enum{…};` + anon-union-typed local `union{…} u;`
 *
 * Grammar: `STRUCT '{'` / `UNION '{'` now has exactly ONE reduce action
 * (the unified nested_s_begin / nested_u_begin marker), and an anonymous
 * aggregate reduces to a `type` via `nested_s_begin smembers '}'`.  A
 * *named* nested member `struct{…} name;` flows through `smembers type
 * IDENT ';'`, and `typedef struct{…} T;` through `TYPEDEF type IDENT ';'`,
 * so no second empty marker for `STRUCT '{'` exists (0 reduce/reduce).
 * Function-local `enum{…};` is a new `dcls` rule mirroring file-scope edcl.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/anon_aggr_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/anon_aggr_probe/anon_aggr_probe.exe \
 *             | diff - minic/dos/tests/anon_aggr_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

/* typedef of an anonymous struct -- now parses via TYPEDEF type IDENT ';' */
typedef struct { int x; int y; } point_t;

/* a tagged struct WITH a nested named anon-struct member: exercises the
 * `smembers type IDENT ';'` path that replaced the dedicated nested rule. */
struct outer {
	int tag;
	struct { int lo; int hi; } range;   /* named nested member */
};

int
main(void)
{
	long total;

	/* (a) anonymous-struct-typed local variable (py/objlist shape). */
	struct { int a, b; } pair;
	pair.a = 10;
	pair.b = 32;
	printf("a=%d\r\n", pair.a + pair.b);          /* 42 */

	/* (b) anonymous-union-typed local variable (py/modbuiltins shape). */
	{
		union { int i; char c[4]; } u;
		u.i = 0;
		u.c[0] = 5;
		u.c[1] = 1;
		printf("b=%d\r\n", u.i);               /* 0x0105 = 261 */
	}

	/* (c) function-local anonymous enum (py/modbuiltins shape). */
	{
		enum { ARG_sep, ARG_end, ARG_file };
		printf("c=%d,%d,%d\r\n", ARG_sep, ARG_end, ARG_file);  /* 0,1,2 */
	}

	/* (d) typedef of an anonymous struct, instantiated. */
	{
		point_t p;
		p.x = 7;
		p.y = 100;
		printf("d=%d\r\n", p.x + p.y);          /* 107 */
	}

	/* (e) named nested anon-struct member inside a tagged struct. */
	{
		struct outer o;
		o.tag = 1;
		o.range.lo = 3;
		o.range.hi = 9;
		printf("e=%d\r\n", o.tag + o.range.lo + o.range.hi);  /* 13 */
	}

	/* (f) inline offsetof through an anonymous-struct cast (py/binary
	 * shape).  minic packs {char; short} with no padding, so the short
	 * member sits at offset 1. */
	total = (long)&((struct { char c; short t; } *)0)->t;
	printf("f=%ld\r\n", total);                    /* 1 */

	return 0;
}
