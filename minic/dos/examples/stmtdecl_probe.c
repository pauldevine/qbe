/*
 * stmtdecl_probe.c -- C-Kermit link campaign items 1b + 1c: declarations
 * in a NESTED block (statement scope).
 *
 * A nested block's leading declarations parse as statements, not through
 * the function-top `dcls` rules, and statement scope had only
 * `extern T name;`.  These were all parse errors there:
 *
 *     { extern char fnbuf[]; ... }        extern array        (C-Kermit G16)
 *     { extern int a, b, c; ... }         multi-name extern   (C-Kermit G13)
 *     { extern char *tab[], **lst; ... }  pointer-array extern
 *     { char *homedir(void); ... }        prototypes          (C-Kermit G11)
 *     { int f(int); extern int g(int); char *h(); ... }
 *
 * (and `extern int g(int);` at the function top).  The triage worked
 * around them by turning the array extern into a POINTER (loads garbage)
 * and deleting the prototypes -- so a call to a pointer-returning function
 * fell back to implicit int and lost the segment in the far-data models.
 *
 * Bug-loud: the pre-fix compiler rejects this file.  homedir()/nodir() are
 * defined after main, so the block prototypes are their only declarations
 * there; without them the far-data build fails ("invalid assignment" of an
 * implicit-int result to a pointer).  Output is values only: one golden for
 * all five models.
 */
#include <stdio.h>

char fnbuf[] = "filnam";
int ea = 1;
int eb = 2;
int ec = 3;
char *tab[] = { "t0", "t1", 0 };
char **lst = tab;

int twice(int n) { return 2 * n; }
int thrice(int n) { return 3 * n; }

static void ok(char *what, int cond)
{
	printf("%s %s\n", what, cond ? "ok" : "FAIL");
}

int main(void)
{
	extern int thrice(int);     /* function-top extern ANSI prototype */
	int y;

	y = 0;
	{
		extern char fnbuf[];
		ok("stmt extern array", fnbuf[3] == 'n' && sizeof(fnbuf[0]) == 1);
	}
	{
		extern int ea, eb, ec;
		y = ea + eb + ec;
		ok("stmt multi extern", y == 6);
	}
	{
		extern char *tab[], **lst;
		ok("stmt ptr-array extern", tab[1][1] == '1' && lst[0][0] == 't');
	}
	{
		char *homedir(void);
		char *nodir();
		int twice(int);
		char *h, *n;
		h = homedir();
		n = nodir();
		ok("stmt prototypes", h[0] == 'h' && h[3] == 'e' && n[1] == 'o' &&
		    twice(4) == 8);
	}
	{
		extern int twice(int);
		extern char *homedir(void);
		ok("stmt extern prototypes", twice(5) == 10 && homedir()[1] == 'o');
	}
	ok("fn-top extern prototype", thrice(3) == 9);
	for (y = 0; y < 2; y++) {
		extern char *tab[];
		char *nodir();
		ok("loop-body decls", tab[y][1] == '0' + y && nodir()[0] == 'n');
	}
	printf("stmtdecl_probe done\n");
	return 0;
}

/* Defined AFTER main (as in C-Kermit, where they live in another module):
 * main's only declarations of these are the block-scope prototypes. */
char *homedir(void) { return "home"; }
char *nodir() { return "none"; }
