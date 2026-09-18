/*
 * ptrdecl_probe.c -- C-Kermit link campaign item 1a: pointer-array and
 * double-pointer declarators in multi-declarator lists.
 *
 * minic's ext_decl (the declarator list after the first name) took ONE
 * leading `*` followed by a bare name, `name()`, `name(params)` or
 * `name = init` only, so every one of these was a hard parse error:
 *
 *     extern char *binpatterns[], *txtpatterns[];   // C-Kermit ckuus*.c
 *     extern char *cmarg, **cmlist, filnam[];       // C-Kermit ckcmai.c
 *     char *a[3], *b[4];   char **p, **q;           // any scope
 *
 * and `extern void *p;` died "invalid void extern pointer".  The same code
 * also silently MIS-TYPED declarators after the first:
 *
 *   - file scope ignored each item's own `*`: `int a, *b;` made b an int,
 *     `char *p, c;` made c a pointer;
 *   - file scope dropped a later item's initializer: `int a, b = 5;` -> b 0;
 *   - `char *a[3], b;` in a function made a a char[3] and b a char*;
 *   - `char *s, *f();` at file scope / `char *p, *g(int);` in a function
 *     made the prototype a VARIABLE ("invalid call").
 *
 * THE FIX (frontend minic.y): `ext_decl: '*' ext_decl` (any number of
 * stars on any declarator form), and one rule for every consumer: the
 * first declarator's `*`s were absorbed into `type` by the grammar, so
 * later declarators start from the declaration's specifier, recovered by
 * tracking `type '*'` reductions (decl_base0 / ed_elem).
 *
 * Bug-loud: the pre-fix compiler rejects this file (parse error), and each
 * mis-typing above prints a FAIL line.  Output is values and sizeof
 * RATIOS, so the golden is identical across all five models.
 */
#include <stdio.h>

/* ---- file scope: multi-declarator items with their own stars ---- */
int ga, *gb;                    /* gb is int*   (was: int)          */
char *gc, gd;                   /* gd is char   (was: char*)        */
char *gt[3], *gu[2];            /* arrays of pointers (was: error)  */
char **ge, **gf;                /* double pointers (was: error)     */
int gi, gj = 5;                 /* gj initialized (was: dropped)    */
char gk[4], *gl;                /* array-first, then pointer        */
char *gs, *gdup();              /* gdup a function (was: variable)  */
char *gret(int n), *gret2(), gm;/* function-first, then char        */

/* ---- file-scope externs of the C-Kermit shapes, defined below ---- */
extern char *xs[], *ys[];
extern char *cm, **cl, fn[];
extern void *vp, *vq;
extern int xn, *xp;

char *xs[] = { "alpha", "beta", 0 };
char *ys[] = { "gamma", 0 };
char *cm = "cmarg";
char **cl = xs;
char fn[] = "filnam";
int xn = 42;
int *xp = &xn;
void *vp = &xn;
void *vq = 0;

char *gdup() { return "dup"; }
char *gret(int n) { return n ? "one" : "zero"; }
char *gret2() { return "two"; }

static void ok(char *what, int cond)
{
	printf("%s %s\n", what, cond ? "ok" : "FAIL");
}

static int localdecls(void)
{
	char *la[3], lb;            /* la: 3 pointers, lb: a char        */
	char **lp, **lq;
	int n, *np;
	char *s, *lookup(int);      /* a prototype, not a variable        */
	extern char *ys[], **cl;    /* function-top multi extern          */
	static char *sa[2], *sb[3]; /* static: arrays of pointers         */

	ok("local ptr array", sizeof(la) == 3 * sizeof(char *) && sizeof(lb) == 1);
	la[0] = "x"; la[2] = "z"; lb = 'q';
	ok("local ptr array use", la[0][0] == 'x' && la[2][0] == 'z' && lb == 'q');
	lp = xs; lq = &la[2];
	ok("local double ptr", lp[1][1] == 'e' && **lq == 'z');
	n = 3; np = &n; *np = 9;
	ok("local int + int*", n == 9 && sizeof(n) == sizeof(int));
	s = lookup(1);
	ok("local prototype", s[0] == 'o');
	ok("local multi extern", ys[0][0] == 'g' && cl[0][0] == 'a');
	ok("static ptr arrays", sizeof(sa) == 2 * sizeof(char *) &&
	    sizeof(sb) == 3 * sizeof(char *));
	sb[2] = "s2";
	return sb[2][1] == '2';
}

char *lookup(int n) { return gret(n); }

int main(void)
{
	gb = &ga;
	*gb = 7;
	ok("int a, *b", ga == 7 && sizeof(gb) == sizeof(int *));
	ok("char *p, c", sizeof(gd) == 1 && sizeof(gc) == sizeof(char *));
	gt[1] = "t1"; gu[1] = "u1";
	ok("ptr arrays", sizeof(gt) == 3 * sizeof(char *) &&
	    sizeof(gu) == 2 * sizeof(char *) && gt[1][0] == 't' && gu[1][0] == 'u');
	ge = xs; gf = ys;
	ok("double ptrs", ge[1][0] == 'b' && gf[0][0] == 'g');
	ok("later initializer", gi == 0 && gj == 5);
	gl = gk; gk[0] = 'k';
	ok("array then ptr", sizeof(gk) == 4 && *gl == 'k');
	ok("K&R proto item", gdup()[0] == 'd');
	ok("fn-first list", gret2()[1] == 'w' && sizeof(gm) == 1);
	ok("extern ptr arrays", xs[1][0] == 'b' && ys[0][1] == 'a');
	ok("extern **", cl[0][1] == 'l' && cm[0] == 'c' && fn[3] == 'n');
	ok("extern void *", *(int *)vp == 42 && vq == 0);
	ok("extern int, *int", *xp == 42 && xn == 42);
	ok("static rest", localdecls());
	printf("ptrdecl_probe done\n");
	return 0;
}
