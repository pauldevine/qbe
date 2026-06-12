/* proto_param_leak_probe.c — §6a: file-scope prototype params must not
 * leak into the global symbol table.
 *
 * Before the fix, par1/par0 registered each parameter name via varadd at
 * file scope and nothing removed it after a PROTOTYPE (definitions are
 * fine — init_ansi varclr()s first).  A later declaration reusing the
 * name with a different type then died with a bogus "double definition":
 *   int first(char *buf, unsigned size);          leaks buf, size
 *   extern long second(int fd, const void *buf);  buf as void* -> die
 *   extern int third(int x);
 *   int x;                                        x as global -> die
 * newlibc hits this constantly (every libgloss TU declares fd/buf/count
 * params across many prototypes).  Fixed by varclr() at the end of every
 * file-scope prototype-only reduction.
 */
#include <stdio.h>

int first(char *buf, unsigned int size);
extern long second(int fd, const void *buf, unsigned int count);
extern int third(int x);
int x = 7;

int first(char *buf, unsigned int size)
{
	return buf[0] + (int)size;
}

long second(int fd, const void *buf, unsigned int count)
{
	const char *p = buf;
	return (long)(fd + p[0] + (int)count);
}

int third(int q)
{
	return q * 2;
}

int main()
{
	char b[2];
	b[0] = 1;
	b[1] = 0;
	printf("%d\n", first(b, 4));
	printf("%ld\n", second(2, b, 3));
	printf("%d\n", third(x));
	return 0;
}
