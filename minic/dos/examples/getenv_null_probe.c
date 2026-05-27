/*
 * getenv_null_probe.c — exercises stub NULL-return DX-clearing under
 * compact / large / huge.
 *
 * Pre-fix bug: `_getenv` and `_fgets` stubs in minic/dos/libstub.asm
 * only set AX=0 and left DX undefined.  Under far-data models a
 * `char *` is a 4-byte far pointer returned in DX:AX, so NULL must
 * have BOTH halves zero.  Pre-huge this was masked: under
 * tiny/small/medium AX alone is the pointer; under compact/large the
 * DX register typically held 0 at the call site because nothing else
 * had touched it.
 *
 * Under MHuge the bug surfaced: minic.y's `huge_ptr_binop` routes
 * Mhuge pointer add/sub through `_qbe_huge_add` (libstub helper),
 * which returns the segment in DX.  Any pointer arithmetic before a
 * stub call now leaves DX with a non-zero segment value, and the
 * unset DX in `_getenv` returned a fake non-NULL pointer.  Stevie's
 * `getenv("EXINIT")` consequently entered the EXINIT-handling block
 * with `initstr` pointing at (DGROUP:0) — sprintf then chased an
 * unterminated "string" through DGROUP and wedged DOSBox at boot.
 *
 * Fix: `_getenv` (and `_fgets`) now `xor ax, ax / xor dx, dx` before
 * `ret` so callers see a properly-zero NULL pointer in DX:AX.
 *
 * This probe dirties DX via a `_qbe_huge_add`-routed pointer add,
 * then calls `getenv` on a key that's guaranteed absent and asserts
 * the return is NULL.  Pre-fix under huge: prints `FAIL not-null`.
 * Post-fix all three models: prints `ok null`.
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/getenv_null_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *             build/examples/getenv_null_probe/getenv_null_probe.exe) \
 *             minic/dos/tests/getenv_null_probe.golden.txt
 */

#include <stdio.h>
#include <stdlib.h>

static char buf[64];

int
main(void)
{
	char *p;
	char *q;

	/* Dirty DX under huge by walking a pointer through `_qbe_huge_add`.
	 * Under compact/large this is a plain `add ax, 10`, which doesn't
	 * affect DX — but the post-fix invariant must hold there too. */
	p = buf;
	p = p + 10;
	/* Make sure the compiler can't fold the add away by hiding p
	 * behind a sink store. */
	buf[0] = (char)(unsigned)p;

	q = getenv("ABSENT_KEY_XYZQ");
	if (q == NULL)
		puts("ok null");
	else
		printf("FAIL not-null off=%u seg=...\n", (unsigned)q);
	return 0;
}
