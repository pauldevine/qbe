/*
 * dos_file_probe.c -- gate for the §7s DOS-file backend (dos_vfs.c), the real
 * file I/O a DOS-hosted program (stevie) needs from the libstub-free stack.
 *
 * §7r got stevie LINKING + rendering libstub-free, but its file I/O was broken:
 * fopen/getc/fputs route through newlibc's stdio -> _read/_write -> vfs_*, and
 * the bare-metal vfs.c (FAT-on-disk) has no backend for a plain DOS path like
 * "config.sys".  dos_vfs.c replaces vfs.c on the DOS-program path with a DOS
 * INT 21h file backend (fd = DOS handle).  This probe exercises a full
 * round trip — fopen("w") + fputs/fputc, fclose, fopen("r") + getc to EOF,
 * remove — proving real DOS files work.
 *
 * Gated 4-way like dos_libc_probe: small + medium x {libstub anchor (libstub's
 * own INT 21h fopen/getc), libstub-free (dos_vfs.c)} against ONE golden.  Both
 * runtimes do real INT 21h file I/O, so the round-tripped bytes — hence the
 * output — are identical; bug-loud: a broken open/read/write diffs the golden.
 *
 * main() is renamed newlibc_test_main by the libstub-free build's -Dmain=, so
 * dos_shim's main() runs vfs_init() first; the libstub build keeps main().
 */

#include <stdio.h>
#include <string.h>

extern int remove();

int main(void)
{
	FILE *f;
	char buf[64];
	int c, i;

	printf("dos_file_probe start\n");

	/* Write two lines through the FILE write path (fputs/fputc). */
	f = fopen("DFPROBE.TXT", "w");
	if (f == NULL) {
		printf("FAIL: fopen w\n");
		return 1;
	}
	fputs("line one\n", f);
	fputc('a', f);
	fputc('b', f);
	fputc('\n', f);
	fclose(f);

	/* Read it back byte-by-byte through getc (-> fgetc -> _read -> vfs_read). */
	f = fopen("DFPROBE.TXT", "r");
	if (f == NULL) {
		printf("FAIL: fopen r\n");
		return 1;
	}
	i = 0;
	while ((c = getc(f)) != EOF && i < (int)sizeof(buf) - 1)
		buf[i++] = (char)c;
	buf[i] = '\0';
	fclose(f);

	printf("read %d bytes\n", i);
	printf("content:%s:end\n", buf);
	printf("remove %d\n", remove("DFPROBE.TXT"));

	/* Confirm it's gone (re-open should fail). */
	f = fopen("DFPROBE.TXT", "r");
	printf("reopen %s\n", (f == NULL) ? "gone" : "still-there");
	if (f != NULL)
		fclose(f);

	printf("dos_file_probe done\n");
	return 0;
}
