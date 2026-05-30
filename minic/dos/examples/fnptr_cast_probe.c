/*
 * fnptr_cast_probe.c -- cast to a function-pointer type in expression
 * context: `(RET (*)(PARAMS)) expr`.  Needed by py/parse.c's
 *   ctx.func = (void (*)(void *))(mp_lexer_free);
 * which stores a concrete function through a generic `void (*)(void *)`
 * field.  minic models a function pointer as IDIR(FUNC(rettype)); the new
 * `pref: '(' type '(' '*' ')' '(' fptpar0 ')' ')' pref` rule reinterprets
 * the operand with that type (0 new grammar conflicts).
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/fnptr_cast_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/fnptr_cast_probe/fnptr_cast_probe.exe \
 *             | diff - minic/dos/tests/fnptr_cast_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

static int g_seen;

static void
record(void *p)
{
	g_seen = *(int *)p;
}

static int
doubler(int x)
{
	return x * 2;
}

int
main(void)
{
	/* (a) store a concrete function through a generic void(*)(void*)
	 * field via the cast, then call it (the py/parse.c shape). */
	void (*cb)(void *);
	int v = 21;
	cb = (void (*)(void *))(record);
	cb(&v);
	printf("a=%d\r\n", g_seen);                 /* 21 */

	/* (b) cast then immediately call through the casted pointer. */
	printf("b=%d\r\n", ((int (*)(int))(doubler))(20));   /* 40 */

	return 0;
}
