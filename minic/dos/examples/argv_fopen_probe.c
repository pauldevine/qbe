/* argv_fopen_probe.c — exercise argv[1] → fopen path under far-data.
 * Mirrors stevie main.c's early startup: take a filename from argv[1],
 * open it, read it, print contents.  Pins both argv ABI + far-data
 * fopen/fread end-to-end.
 */

#include <stdio.h>

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buf[80];
	int n;

	printf("argc=%d\n", argc);
	if (argc < 2) {
		printf("usage: probe <filename>\n");
		return 1;
	}
	printf("argv[1]=%s\n", argv[1]);
	f = fopen(argv[1], "r");
	if (f == NULL) {
		printf("fopen failed for %s\n", argv[1]);
		return 1;
	}
	printf("fopen ok\n");
	while ((n = fread(buf, 1, 79, f)) > 0) {
		buf[n] = 0;
		printf("read %d: %s", n, buf);
	}
	fclose(f);
	printf("done\n");
	return 0;
}
