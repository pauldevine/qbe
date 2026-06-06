/* bios_write_probe.c — write HELLO.TXT to BIOS teletype (INT 10h AH=0E)
 * the same way stevie's flushbuf does.  If we see correct content on
 * the screen, stevie's BIOS path is broken downstream.  If we see
 * blank/garbage, the bug is in this exact pattern.
 *
 * Drives BIOS via int86, same as stevie/dos.c:flushbuf.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buff[256];
	int i, c;
	union REGS inregs, outregs;

	printf("[1] argc=%d\r\n", argc);
	if (argc < 2) { printf("usage\r\n"); return 1; }
	printf("[2] argv[1]=%s\r\n", argv[1]);
	f = fopen(argv[1], "r");
	printf("[3] fopen returned %p\r\n", f);
	if (f == NULL) { printf("fopen fail\r\n"); return 1; }

	i = 0;
	while ((c = getc(f)) != EOF) {
		buff[i++] = c;
		if (i >= 255) break;
	}
	buff[i] = 0;
	fclose(f);
	printf("[4] read %d bytes: [%s]\r\n", i, buff);

	/* Teletype every byte via AH=0E.  No screen clear — we want to
	 * see the printf diagnostics above stay visible. */
	printf("[5] writing via BIOS AH=0E ...\r\n");
	inregs.h.ah = 0x0E;
	inregs.h.bh = 0;
	inregs.h.bl = 7;
	for (i = 0; buff[i]; i++) {
		inregs.h.al = buff[i];
		int86(0x10, &inregs, &outregs);
	}
	printf("\r\n[6] done.  press a key.\r\n");

	/* Wait for keypress. */
	inregs.h.ah = 0;
	int86(0x16, &inregs, &outregs);
	return 0;
}
