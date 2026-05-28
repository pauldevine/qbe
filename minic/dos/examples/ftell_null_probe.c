/*
 * ftell_null_probe.c — exercises the `long`-return DX-clearing in the
 * `_ftell` libstub stub under compact / large / huge.
 *
 * Same bug class as getenv_null_probe.c, but for a `long` rather than a
 * `char *`.  A C `long` is ALWAYS 4 bytes on i8086 (returned in DX:AX),
 * in every memory model — so a 0 return needs BOTH halves zero.  The
 * `_ftell` stub used to `mov ax, 0 / ret`, leaving DX undefined; a
 * caller reading the full long then saw a garbage high word.
 *
 * Pre-fix this was masked when DX happened to be 0 at the call site.
 * Under MHuge it surfaces hard: minic.y's `huge_ptr_binop` routes
 * pointer add/sub through `_qbe_huge_add`, which returns the new
 * segment in DX.  Any pointer arithmetic before the `ftell` call leaves
 * DX holding a real segment, so the unset DX leaked into ftell's
 * return as a non-zero high word.
 *
 * Fix: `_ftell` now `xor ax, ax / xor dx, dx` before `ret`.  (The same
 * one-line guard was also applied defensively to `_signal`, whose
 * standard prototype is a 4-byte function pointer.)
 *
 * This probe dirties DX via a `_qbe_huge_add`-routed pointer add, then
 * calls `ftell(stdin)` (the stub ignores its arg) and asserts the full
 * 32-bit return is zero.  Pre-fix under huge: prints `FAIL ftell-nonzero`.
 * Post-fix all three models: prints `ok ftell-zero`.
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/ftell_null_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *             build/examples/ftell_null_probe/ftell_null_probe.exe) \
 *             minic/dos/tests/ftell_null_probe.golden.txt
 */

#include <stdio.h>
#include <stdlib.h>

static char buf[64];

int
main(void)
{
	char *p;
	long t;

	/* Dirty DX under huge by walking a pointer through `_qbe_huge_add`.
	 * Under compact/large this is a plain `add ax, 10`, which doesn't
	 * touch DX — but the post-fix invariant must hold there too. */
	p = buf;
	p = p + 10;
	/* Sink store so the compiler can't fold the add away. */
	buf[0] = (char)(unsigned)p;

	t = ftell(stdin);
	if (t == 0)
		puts("ok ftell-zero");
	else
		puts("FAIL ftell-nonzero");
	return 0;
}
