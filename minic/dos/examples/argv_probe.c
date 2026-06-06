/* argv_probe.c — verify crt0_exe builds argv correctly under far-data.
 *
 * Runtime probe: read argv via DOS PSP-tail tokenization (set up by
 * crt0_exe.asm), print argc + each argv[i] string + each pointer's
 * offset and segment.  Under compact/large/huge, argv is a far ptr to
 * an array of far ptrs (4-byte slots), each pointing to a NUL-
 * terminated string in DGROUP (_cmdbuf).
 *
 * Pinned via tools/run-dos-exe.sh: command-line args appear on the
 * DOSBox `c:\RUN.EXE` invocation as `RUN.EXE`, with no extras.  So we
 * just print argc (expect 1) and argv[0] (expect "program").
 */

#include <stdio.h>

int main(argc, argv)
int argc;
char **argv;
{
	int i;

	printf("argc=%d\n", argc);
	for (i = 0; i < argc; i++) {
		printf("argv[%d]=%s\n", i, argv[i]);
	}
	printf("done\n");
	return 0;
}
