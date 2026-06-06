/*
 * block_scope_probe.c -- inner-block lexical scope via alpha-renaming,
 * needed by py/{mpprint,runtime,sequence,vm,parse}.c (MicroPython port;
 * see NEXT_SESSION.md / [[minic-inner-block-scope]]).
 *
 * minic has a single flat local symbol table and emits function bodies
 * lazily (a variable USE is resolved by name at emit time via varget),
 * so a name reused across distinct blocks with *different* types used to
 * die with "double definition" — a single shared %name/type would
 * miscompile the earlier block's uses.  The frontend now alpha-renames
 * the colliding declarator to a unique `name$N` and stamps that mangled
 * name into every subsequent use of the source name, popping the binding
 * when the block ends.  Renaming fires only on a *different-type*
 * collision; a same-typed re-declaration still folds to one slot
 * (stevie's sibling for-bodies), which case (b) checks for regressions.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/block_scope_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/block_scope_probe/block_scope_probe.exe \
 *             | diff - minic/dos/tests/block_scope_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

int
main(void)
{
	long total;

	/* (a) Sibling blocks reuse one name with DIFFERENT types — the
	 * core case (sequence.c's swap idiom, mpprint.c's switch-case
	 * locals).  Each block's `t` must keep its own type. */
	total = 0;
	{
		const char *t = "abcd";   /* pointer */
		total += (long)t[0];      /* 'a' = 97 */
	}
	{
		long t = 1000;            /* integer, same name */
		total += t;               /* 1000 */
	}
	printf("a=%ld\r\n", total);       /* 1097 */

	/* (b) Sibling blocks reuse one name with the SAME type — must still
	 * fold to a single slot (stevie for-body behaviour, unchanged). */
	total = 0;
	{
		int s = 5;
		total += s;
	}
	{
		int s = 7;
		total += s;
	}
	printf("b=%ld\r\n", total);       /* 12 */

	/* (c) parse.c pattern: an inner-block local leaks into the flat
	 * symbol table, then a later *function-scope* declaration reuses
	 * the name with a different type (inner mp_parse_node_t `pn`, then
	 * function-level mp_parse_node_struct_t *`pn`). */
	{
		char pn = 9;
		total = pn;               /* 9 */
	}
	{
		long pn = 42;             /* different type, must not collide */
		total += pn;              /* 51 */
	}
	printf("c=%ld\r\n", total);       /* 51 */

	/* (d) Genuine shadow: an outer variable, an inner block redeclares
	 * the same name with a different type, and the outer is used again
	 * afterwards — it must still see its original value/type (the
	 * deferred rename-pop at block exit). */
	{
		long v = 100;
		{
			const char *v = "Z"; /* shadows; different type */
			total = (long)v[0];  /* 'Z' = 90 */
		}
		total += v;               /* outer v = 100 -> 190 */
	}
	printf("d=%ld\r\n", total);       /* 190 */

	/* (e) The renamed binding is used across several statements and a
	 * deref, exercising use-stamping past the first use. */
	total = 0;
	{
		long arr[3];
		long *p;
		arr[0] = 11; arr[1] = 22; arr[2] = 33;
		p = arr;
		total = p[0] + p[1] + p[2]; /* 66 */
	}
	{
		char p = 4;               /* same name, different type */
		total += p;               /* 70 */
	}
	printf("e=%ld\r\n", total);       /* 70 */

	return 0;
}
