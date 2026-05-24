/*
 * stdio_far_probe.c -- runtime gate for the far-pointer FILE* + stdio
 * helpers added in session 2026-05-24 (t).
 *
 * Under --model=large (and huge), `FILE *` is a 4-byte far pointer.  The
 * unmangled _fopen/_fclose/_fputs/_fputc/_fgets in FILEIO_EXE consume
 * 2-byte near pointers — calling them directly from far-data callers
 * misaligns subsequent stack args and reads garbage handle/name/string
 * data.
 *
 * minic mangles fopen/fclose/fputs/fputc/fgets to `_far_X` in far-data
 * memory models; this probe exercises that path end-to-end.
 *
 * Mirrors the file I/O section of mediumprobe.c but uses fputs/fputc/
 * fgets instead of fputs+fread (the far variant of fread isn't part of
 * this session — `fread` is not yet listed in far_stdlib[]).
 *
 * Validation pattern: each assertion reduces to a single boolean,
 * printed as `name=%d (want 1)` to match the dos_far_probe shape.  The
 * helper sgn() collapses strcmp's 3-valued return to a sign.
 *
 * Build:  tools/build-example.sh --model=large minic/dos/examples/stdio_far_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/stdio_far_probe/stdio_far_probe.exe \
 *             | diff - minic/dos/tests/stdio_far_probe.golden.txt
 */

#include <stdio.h>
#include <string.h>

static int sgn(int n) {
	if (n < 0) return -1;
	if (n > 0) return 1;
	return 0;
}

int main()
{
	FILE *f;
	char buf[64];
	char *p;

	/* === stdio sentinels: stdout points at a 4-byte FILE struct == */
	printf("stdout_nn=%d (want 1)\r\n", stdout != 0);
	printf("stderr_nn=%d (want 1)\r\n", stderr != 0);

	/* === fopen(w) -> fputs/fputc -> fclose === */
	f = fopen("SFAR.TXT", "w");
	printf("open_w_nn=%d (want 1)\r\n", f != 0);
	if (f != 0) {
		printf("fputs_ok=%d (want 1)\r\n", fputs("far-stdio", f) == 0);
		printf("fputc_a=%d (want 1)\r\n", fputc('\n', f) == '\n');
		printf("fputs2_ok=%d (want 1)\r\n", fputs("line2", f) == 0);
		printf("fputc_b=%d (want 1)\r\n", fputc('\n', f) == '\n');
		fclose(f);
	}

	/* === fopen(r) -> fgets -> fclose === */
	f = fopen("SFAR.TXT", "r");
	printf("open_r_nn=%d (want 1)\r\n", f != 0);
	if (f != 0) {
		memset(buf, 0, 64);
		p = fgets(buf, 64, f);
		printf("fgets1_nn=%d (want 1)\r\n", p != 0);
		printf("fgets1_cmp=%d (want 0)\r\n", sgn(strcmp(buf, "far-stdio\n")));
		memset(buf, 0, 64);
		p = fgets(buf, 64, f);
		printf("fgets2_nn=%d (want 1)\r\n", p != 0);
		printf("fgets2_cmp=%d (want 0)\r\n", sgn(strcmp(buf, "line2\n")));
		memset(buf, 0, 64);
		p = fgets(buf, 64, f);
		printf("fgets_eof=%d (want 1)\r\n", p == 0);
		fclose(f);
	}

	return 0;
}
