/*
 * fprintf_far_probe.c — exercises `_far_fprintf` arg layout under
 * compact / large / huge.
 *
 * Pre-fix bug: _far_fprintf in tools/libstub_to_exe.py treated `FILE *`
 * as 2 bytes on the stack instead of 4 bytes, so:
 *   - fmt was read from [bp+8]:[bp+10] instead of [bp+10]:[bp+12]
 *   - varargs started at [bp+12] instead of [bp+14]
 * Under far-data, FILE* is 4 bytes (off:seg), so every caller of
 * fprintf passed args shifted by one word.  The fmt-seg field of
 * _far_sprintf ended up holding fmt.lo (a small offset value used as
 * a segment), making the format-string fetch land in CS or wherever
 * (fmt.lo * 16 + DGROUP_value) happened to point.  Symptom: stevie's
 * `:w` wrote code-segment bytes to the output file instead of file
 * content.
 *
 * Fix: bumped every offset in _far_fprintf by +2.
 *
 * Probe writes a few fprintf lines to a temp file then reads them
 * back with fgets and prints to stdout.  Pre-fix the temp file
 * contains garbage code bytes; post-fix it contains the expected
 * text and the probe golden matches.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/fprintf_far_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/fprintf_far_probe/fprintf_far_probe.exe) \
 *              minic/dos/tests/fprintf_far_probe.golden.txt
 */

#include <stdio.h>

int main(void)
{
	FILE *f;
	char buf[80];
	int  rc;

	f = fopen("FPRTOUT.TMP", "w");
	if (f == 0) {
		printf("open fail\r\n");
		return 1;
	}

	/* Plain %s — exercises far fmt + 1 far vararg. */
	fprintf(f, "plain=%s\r\n", "hello");

	/* %d + %s — int (2 bytes) + far ptr (4 bytes) varargs. */
	fprintf(f, "mixed=%d %s\r\n", 42, "world");

	/* Multiple %s — chained vararg consumption. */
	fprintf(f, "trio=%s %s %s\r\n", "alpha", "beta", "gamma");

	fclose(f);

	/* Read it back to confirm the file contains what we wrote. */
	f = fopen("FPRTOUT.TMP", "r");
	if (f == 0) {
		printf("reopen fail\r\n");
		return 1;
	}
	while (fgets(buf, 80, f) != 0) {
		/* fgets keeps the trailing newline; printf via %s passes it
		 * through.  golden expects exact echo. */
		printf("%s", buf);
	}
	fclose(f);

	/* Cleanup. */
	remove("FPRTOUT.TMP");

	printf("done\r\n");
	return 0;
}
