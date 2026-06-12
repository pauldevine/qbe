/* array_param_probe.c — §6a: array parameter declarators.
 *
 * Before the fix, par1 had no '[' ']' forms, so any of:
 *   int f(uint8_t out[11]);          (newlibc fat.c make_83_name)
 *   int g(char buf[]);
 *   int h(char *const argv[]);       (newlibc libgloss _execve)
 * died with a parse error.  All decay to pointers per C; a sized
 * dimension is documentation only.
 */
#include <stdio.h>

static int sum11(const char raw[11], unsigned char out[11])
{
	int i, s = 0;
	for (i = 0; i < 11; i++)
		s += raw[i] + out[i];
	return s;
}

static int firstlen(char *const argv[])
{
	int n = 0;
	while (argv[0][n])
		n++;
	return n;
}

static void fill(unsigned char out[], int v)
{
	out[0] = (unsigned char)v;
	out[1] = (unsigned char)(v + 1);
}

int main()
{
	char raw[11];
	unsigned char out[11];
	char *args[2];
	int i;

	for (i = 0; i < 11; i++) {
		raw[i] = (char)i;
		out[i] = (unsigned char)(10 - i);
	}
	printf("%d\n", sum11(raw, out));
	args[0] = "victor";
	args[1] = 0;
	printf("%d\n", firstlen(args));
	fill(out, 41);
	printf("%d %d\n", out[0], out[1]);
	return 0;
}
