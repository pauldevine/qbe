/*
 * typedef_shadow_probe.c -- declarator/parameter name shadowing a typedef.
 *
 * Pins the lexer/parser disambiguation added 2026-05-29 (see
 * NEXT_SESSION.md / MICROPYTHON_PORT.md).  C allows an ordinary
 * identifier (parameter, variable, struct member) to have the same
 * spelling as a typedef name: once a type-specifier has been consumed,
 * the next identifier is in the declarator (ordinary-identifier)
 * namespace, not the type namespace.  minic's lexer used to return
 * TNAME unconditionally for any identifier matching a typedef, so it
 * saw `qstr qstr` (the real MicroPython `py/scope.h` shape:
 * `id_info_t *scope_find_or_add_id(scope_t *scope, qstr qstr, ...)`) as
 * two type-names and the grammar had no production for it -> parse error.
 *
 * The fix is two-fold, both in the lexer:
 *   (a) after a complete type-specifier token (a type keyword or a
 *       TNAME), an identifier that also names a typedef is lexed as
 *       IDENT (it begins a declarator), not TNAME;
 *   (b) once such a name is in scope as a local/parameter, *uses* of it
 *       in the function body also lex as IDENT (var_islocal()).  The
 *       previous function's locals are dropped when its body's closing
 *       '}' is consumed (deferred one token so the body's last statement
 *       still resolves), so a typedef-named parameter type in the *next*
 *       function isn't shadowed by a stale local.
 *
 * Exercises (codegen + runtime, not just parse):
 *   1. A parameter named after a typedef, used in the body.
 *   2. A local variable named after a typedef, assigned and read.
 *   3. A struct member named after a typedef, written and read.
 *   4. Two consecutive functions each with a typedef-named parameter
 *      (the cross-function stale-local case the deferred varclr fixes).
 *   5. A plain (unshadowed) typedef use still works as a type.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/typedef_shadow_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/typedef_shadow_probe/typedef_shadow_probe.exe \
 *             | diff - minic/dos/tests/typedef_shadow_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (medium + large).
 */

#include <stdio.h>

typedef unsigned short qstr;

struct holder {
	qstr qstr;   /* member named after the typedef */
	int k;
};

/* parameter named after the typedef, used in the body */
int
use_param(qstr qstr, int add)
{
	return (int)qstr + add;
}

/* second function, also with a typedef-named parameter: catches the
 * cross-function stale-local case (the first qstr here must lex as a
 * type, not as use_param's now-out-of-scope local). */
int
use_param2(qstr qstr)
{
	return (int)qstr * 2;
}

/* local variable named after the typedef.  Once `qstr` is declared as a
 * local it shadows the typedef for the rest of the scope (so a plain
 * typedef use as a *type* must precede it -- exercised by the `q` decl
 * below, which is lexed as a type because no local `qstr` is in scope
 * yet). */
int
use_local(int seed)
{
	qstr q;        /* plain typedef use as a type (not yet shadowed) */
	qstr qstr;     /* local shadowing the typedef from here on */
	q = (unsigned short)(seed + 100);
	qstr = (unsigned short)(seed + 1);
	return (int)qstr + (int)q;
}

int
main(void)
{
	struct holder h;

	h.qstr = 41;       /* write the shadow-named member */
	h.k = 1;
	printf("member=%d (want 42)\r\n", (int)h.qstr + h.k);

	printf("param=%d (want 17)\r\n", use_param(10, 7));
	printf("param2=%d (want 30)\r\n", use_param2(15));
	printf("local=%d (want 105)\r\n", use_local(2));

	return 0;
}
