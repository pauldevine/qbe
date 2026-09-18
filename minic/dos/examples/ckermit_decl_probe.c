/* C-Kermit triage probe: frontend forms found compiling ~/projects/ckermit.
 * Bug-loud: on the pre-fix compiler every construct below is a parse error
 * or a "double definition" die, so the build fails.
 *   - comma-operator expression statement      a++, b--;
 *   - comma operator in while / if conditions  while (++v, --c > 0)
 *   - block-scope extern redeclaring an INITIALIZED file-scope global
 *   - block-scope array shadowing a file-scope array of the same name
 */
#include <stdio.h>

char *gname = "kermit";
static char fullname[8] = "global";

static int
count_args(char **v, int c)
{
	int n;
	n = 0;
	while (++v, --c > 0)
		n++;
	return n;
}

static int
pick(int x)
{
	int t;
	t = 0;
	if (t++, x)
		return t + 10;
	return t;
}

static int
shadow(void)
{
	char fullname[4];
	fullname[0] = 'L';
	fullname[1] = 0;
	return fullname[0];
}

int
main(void)
{
	int a, b;
	char *argv[5];
	a = 1; b = 10;
	a++, b--;
	printf("comma_stmt a=%d b=%d\n", a, b);
	printf("while_comma n=%d\n", count_args(argv, 5));
	printf("if_comma %d %d\n", pick(1), pick(0));
	{
		extern char *gname;
		printf("block_extern %s\n", gname);
	}
	printf("shadow local=%c global=%s\n", shadow(), fullname);
	return 0;
}
