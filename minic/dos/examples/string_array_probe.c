/*
 * string_array_probe.c -- char-array initialisation from a string
 * literal (MicroPython port, §1i; see NEXT_SESSION.md).
 *
 * minic had no `T arr[] = "string"` form at all -- only the pointer
 * init `char *p = "string"`.  So both file-scope and function-local
 *   static const byte whitespace[] = " \t\n\r\v\f";
 * (py/objstr.c) parse-errored.  Added file-scope and static-local
 * `char NAME[] = "...";` productions that emit the literal's bytes as a
 * NUL-terminated char array (not a pointer), with sizeof() counting the
 * decoded byte length (escape-aware) plus the terminator.
 *
 * Exercises runtime:
 *   1. file-scope char array from a literal: sizeof, byte reads, NUL.
 *   2. static-local char array with escape sequences: sizeof counts
 *      each escape as one byte (the objstr whitespace idiom).
 *   3. indexing past via a pointer to confirm contiguous bytes.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/string_array_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/string_array_probe/string_array_probe.exe \
 *             | diff - minic/dos/tests/string_array_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

typedef unsigned char byte;

char greeting[] = "hello";          /* 5 chars + NUL = 6 */

int
main(void)
{
	static const byte ws[] = " \t\n\r\v\f";  /* 6 bytes + NUL = 7 */
	const byte *p;
	int n;

	printf("g_sz=%d g0=%d g4=%d gnul=%d\r\n",
	       (int)sizeof(greeting), greeting[0], greeting[4],
	       greeting[5]);                       /* 6 104 111 0 */

	printf("ws_sz=%d ws0=%d ws1=%d ws5=%d\r\n",
	       (int)sizeof(ws), ws[0], ws[1], ws[5]);
	/* 7 32 9 12  (space, tab, formfeed) */

	/* contiguous bytes via pointer walk; count non-NUL of greeting. */
	n = 0;
	for (p = (const byte *)greeting; *p; p++)
		n++;
	printf("glen=%d\r\n", n);                  /* 5 */

	return 0;
}
