/*
 * static_data_probe.c -- C internal linkage for file-scope DATA (§6b).
 *
 * Two translation units (this file + static_data_def.c) each define:
 *   - a file-scope `static int dir_table` with DIFFERENT values (the
 *     newlibc shape: libgloss/dirent.c and vfs/vfs.c both declare a
 *     static `dir_table[]`, and the link died on a duplicate public);
 *   - a `static` function `bump()` containing a block-scope
 *     `static int counter` (mangled `_bump_counter` in BOTH TUs --
 *     the mangled name must also stay module-local);
 *   - this TU additionally exports `shared_global`, consumed by the
 *     other TU via `extern`, so the fix is proven not to have taken
 *     external linkage away from ordinary globals.
 *
 * Bug-loud: pre-fix, asm_to_omf.py auto-promoted every file-scope data
 * label to a public (minic never emitted `export data`), so linking the
 * two TUs failed with `duplicate public symbol '_dir_table'` before
 * anything ran.
 *
 * Build:  tools/build-example.sh --model=small \
 *             minic/dos/examples/static_data_probe.c \
 *             minic/dos/examples/static_data_def.c
 * Verify: tools/run-dos-exe.sh build/examples/static_data_probe/static_data_probe.exe \
 *             | diff - minic/dos/tests/static_data_probe.golden.txt
 */

#include <stdio.h>

static int dir_table = 100;

int shared_global = 41;

extern int def_table_value(void);
extern int def_read_shared(void);
extern int def_bump_twice(void);

static int bump(void)
{
	static int counter = 3;
	counter++;
	return counter;
}

int main()
{
	/* Each TU reads its own static. */
	printf("a_table=%d (want 100)\r\n", dir_table);
	printf("b_table=%d (want 200)\r\n", def_table_value());

	/* Block statics behind same-named static fns stay separate. */
	printf("a_bump=%d (want 4)\r\n", bump());
	printf("a_bump2=%d (want 5)\r\n", bump());
	printf("b_bump=%d (want 32)\r\n", def_bump_twice());

	/* Ordinary global still has external linkage. */
	printf("shared=%d (want 42)\r\n", def_read_shared());
	return 0;
}
