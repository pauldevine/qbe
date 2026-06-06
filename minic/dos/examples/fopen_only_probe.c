/* fopen_only_probe.c — even more minimal than argv_fopen_probe.
 * Just open + immediate diag printf.  No dos.h, no int86, no buff.
 */

#include <stdio.h>

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;

	printf("argc=%d\r\n", argc);
	if (argc < 2) { printf("usage\r\n"); return 1; }
	printf("argv[1]=[%s] len=%d\r\n", argv[1], (int)strlen(argv[1]));

	f = fopen(argv[1], "r");
	printf("fopen ret=%p\r\n", f);
	if (f) {
		printf("ok\r\n");
		fclose(f);
	} else {
		printf("FAIL\r\n");
	}
	return 0;
}
