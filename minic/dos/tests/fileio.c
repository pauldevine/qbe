/*
 * fileio.c -- regression smoke for libstub file I/O via INT 21h.
 *
 * Covers the libstub paths landed in commits fc8d2bc / 76c213e:
 *   - fopen(path, "w") -- open/create handle, AH=3C/3D
 *   - fputs / fwrite   -- INT 21h AH=40 with cdecl callee-save discipline
 *                         (the bug that ate the first line of stevie's
 *                         :w write -- see [[libstub-cdecl-callee-save]])
 *   - fclose
 *   - fopen(path, "r")
 *   - fgets / fread
 *
 * Goals:
 *   - End-to-end roundtrip of a small file (write then read back).
 *   - Validates that BX/SI/DI are preserved across libstub calls.
 *   - Stays well under the .COM 64 KB ceiling.
 *
 * Expected stdout (when run under DOSBox):
 *
 *   write-ok
 *   read=hello from .COM
 *   OK
 *
 * Note: leaves TEST.TXT in the current directory; the smoke test is
 * about codegen correctness, not test-fixture cleanup.
 *
 * Exit code: 0 on success.
 */

#include <stdio.h>
#include <string.h>

static char readbuf[64];
static const char *PAYLOAD = "hello from .COM";

int main(void)
{
	FILE *f;
	int n;

	f = fopen("TEST.TXT", "w");
	if (f == 0) {
		printf("FAIL: fopen-w\r\n");
		return 1;
	}
	if (fputs(PAYLOAD, f) < 0) {
		printf("FAIL: fputs\r\n");
		fclose(f);
		return 1;
	}
	fclose(f);
	printf("write-ok\r\n");

	f = fopen("TEST.TXT", "r");
	if (f == 0) {
		printf("FAIL: fopen-r\r\n");
		return 1;
	}
	n = fread(readbuf, 1, 63, f);
	fclose(f);
	if (n <= 0) {
		printf("FAIL: fread (n=%d)\r\n", n);
		return 1;
	}
	readbuf[n] = 0;
	if (strcmp(readbuf, PAYLOAD) != 0) {
		printf("FAIL: mismatch '%s'\r\n", readbuf);
		return 1;
	}
	printf("read=%s\r\n", readbuf);

	printf("OK\r\n");
	return 0;
}
