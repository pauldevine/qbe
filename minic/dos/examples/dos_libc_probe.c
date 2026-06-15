/*
 * dos_libc_probe.c -- gate for the §7r dos_libc.c libc fill (Phase-6 libstub
 * retirement, the stevie surface).
 *
 * §7q proved the libstub-free build-example path with printf + malloc.  §7r
 * widens dos_libc.c with the str/ctype/stdlib surface a normal minic program
 * (stevie) calls beyond that.  This probe exercises that whole fill, the same
 * way printf_nolibstub_probe gates the printf/heap path: it is built BOTH ways
 * in test-dos.sh — with libstub (libstub.asm's own str/ctype/atoi/getc/getenv/
 * system/signal/exit, the EQUIVALENCE ANCHOR) and libstub-free (dos_libc.c's
 * fill) — and both diff ONE shared golden.
 *
 * Because the libstub build is the anchor, the probe only prints results the
 * two runtimes MUST agree on: bucketed comparison signs (not raw return
 * magnitudes), boolean ctype as 1/0, the exact chars from toupper/tolower, and
 * the exact stub returns (atoi->0, getc->-1, getenv->NULL, system->0,
 * signal->0) that dos_libc.c matches to libstub byte-for-byte.  Bug-loud: a
 * wrong range, a non-matching stub, or a missing libc symbol (fails the link)
 * all diff the golden.
 *
 * main() is renamed newlibc_test_main by the libstub-free build's -Dmain=, so
 * dos_shim's main() runs vfs_init() before calling it (printf needs the VFS
 * console up); the libstub build keeps main() as-is.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

/* Not declared in minic/include — declare K&R (the way stevie does for its
 * own libc externs) so mktemp's char* return assigns cleanly. */
extern int chmod();
extern int remove();
extern char *mktemp();

static const char *sign(int v)
{
	if (v < 0)
		return "lt";
	if (v > 0)
		return "gt";
	return "eq";
}

int main(void)
{
	char buf[8];
	char *p;
	int i;

	printf("dos_libc_probe start\n");

	/* strncmp: print the comparison SIGN, impl-independent. */
	printf("strncmp %s %s %s\n",
		sign(strncmp("abc", "abd", 3)),
		sign(strncmp("abc", "abc", 3)),
		sign(strncmp("abd", "abc", 3)));

	/* strncmp honoring n: differ past the limit -> equal. */
	printf("strncmp-n %s\n", sign(strncmp("abXX", "abYY", 2)));

	/* strchr: char found / NUL terminator / not found. */
	p = strchr("Victor", 't');
	printf("strchr %c\n", p ? *p : '?');
	printf("strchr-end %s\n", strchr("abc", 'z') ? "found" : "null");

	/* strrchr: the LAST '.' in "a.b.c" -> suffix ".c". */
	p = strrchr("a.b.c", '.');
	printf("strrchr %s\n", p ? p : "null");

	/* strcat onto a populated buffer. */
	strcpy(buf, "ab");
	strcat(buf, "cd");
	printf("strcat %s\n", buf);

	/* strncpy: copy 2 of "xy" into a 5-wide field, NUL-pad the rest. */
	for (i = 0; i < 8; i++)
		buf[i] = '#';
	strncpy(buf, "xy", 5);
	printf("strncpy %c%c pad=%s\n", buf[0], buf[1],
		(buf[2] == 0 && buf[3] == 0 && buf[4] == 0 && buf[5] == '#')
			? "ok" : "BAD");

	/* ctype: 1/0 booleans over a fixed sample. */
	printf("ctype a=%d 5=%d sp=%d lo=%d up=%d\n",
		isalpha('Q') ? 1 : 0, isdigit('5') ? 1 : 0,
		isspace(' ') ? 1 : 0, islower('q') ? 1 : 0,
		isupper('Q') ? 1 : 0);
	printf("ctype-neg a=%d 5=%d sp=%d\n",
		isalpha('5') ? 1 : 0, isdigit('q') ? 1 : 0,
		isspace('x') ? 1 : 0);
	printf("case %c%c %c%c\n",
		(char)toupper('v'), (char)toupper('V'),
		(char)tolower('K'), (char)tolower('k'));

	/* stubs: dos_libc.c matches libstub (.COM and .EXE) byte-for-byte.
	 * getc is NOT probed here: the .EXE libstub gives it a real blocking
	 * stdin read (the .COM `mov ax,-1` stub is overridden), and dos_libc.c
	 * matches by delegating to fgetc — covered by the FAT/VFS tests. */
	printf("atoi %d\n", atoi("123"));
	printf("getenv %s\n", getenv("PATH") ? "set" : "null");
	printf("system %d\n", system("echo hi"));
	printf("signal %d\n", signal(SIGINT, 0));

	/* stevie-surface extras: strcspn is real (count up to first reject
	 * char); chmod/remove/mktemp match libstub's deterministic stubs.
	 * rename/sleep/delay are NOT probed (libstub leaves AX undefined). */
	printf("strcspn %d %d\n",
		(int)strcspn("abc123", "0123456789"),
		(int)strcspn("xyz", "0123456789"));
	printf("chmod %d remove %d\n", chmod("f", 0), remove("f"));
	strcpy(buf, "TMPXXX");
	p = mktemp(buf);
	printf("mktemp %s\n", (p == buf) ? "self" : "other");

	printf("dos_libc_probe done\n");
	exit(0);		/* gates exit() symbol resolution + clean DOS exit */
	return 0;
}
