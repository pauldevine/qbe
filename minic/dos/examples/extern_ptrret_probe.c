/* extern_ptrret_probe.c — §6a: `extern T *f(args);` ANSI prototype.
 *
 * Before the fix, minic's extern grammar had no production for a
 * pointer-returning prototype with a parameter list: ext_decl only knew
 * the K&R form `*f()`, so `extern char *f(int);` died with
 * `error:0: parse error`.  newlibc hits this on the very first line of
 * every TU that includes errno.h (`extern int *__errno(void);`).
 * Covers the file-scope walk and the block-local `dcls EXTERN` walk.
 */
#include <stdio.h>

extern char *pick(int which);
extern int *bump(int *p, int delta);

static char a[] = "alpha";
static char b[] = "beta";

char *pick(int which)
{
	return which ? a : b;
}

int *bump(int *p, int delta)
{
	*p += delta;
	return p;
}

static int viablock(void)
{
	extern char *pick(int which);
	return pick(1)[0];  /* 'a' = 97 */
}

int main()
{
	int v = 40;
	printf("%s\n", pick(1));
	printf("%s\n", pick(0));
	printf("%d\n", *bump(&v, 2));
	printf("%d\n", viablock());
	return 0;
}
