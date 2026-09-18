/*
 * paramdecl_probe.c -- C-Kermit link campaign item 2: parameter
 * declarators that were parse errors.
 *
 *     int apply(int (*)(char), char);       abstract fn-ptr param   (G5)
 *     int sum(int [], int);                 abstract array params   (G6)
 *     int cnt(char *[]);   struct K[]
 *     int run(jbuf_t (jb), int);            parenthesized name      (G20)
 *     int hk(int (__far *fn)(int));         __far fn-ptr param      (G21)
 *     struct R { void (__far *rtn)(void); } __far fn-ptr member    (G21)
 *     void (__far *hook)(void);             __far fn-ptr variable + cast (G21)
 *     typedef int binop_t(int, int);        function typedef        (G1)
 *     extern void (*sig(int, void (*)(int)))(int);
 *                                           function returning a
 *                                           function pointer (G2; the
 *                                           prototype only -- minic has
 *                                           no definition form for it)
 *
 * The prototypes are the only declarations in scope at the calls in
 * main (the definitions follow it), so they must carry the right types.
 * Bug-loud: the pre-fix compiler rejects this file.  Output is values
 * only: one golden for all five models.
 */
#include <stdio.h>

typedef int jbuf_t[4];
struct K { int k; char *name; };
struct R { unsigned char type; void (__far *rtn)(void); };
typedef int binop_t(int, int);

int apply(int (*)(char), char);
int sum(int [], int);
int cnt(char *[]);
int keys(struct K[], int);
int run(jbuf_t (jb), int);
int hk(int (__far *fn)(int), int);
int nest(int (*)(int (*)(char), char));
extern void (*sig(int, void (*)(int)))(int);
static void (*pick(int n))(void);
void (__far *hook)(void);

static int hits;
static void bump(void) { hits++; }
static int up(char c) { return c + 1; }
static int dbl(int n) { return 2 * n; }
static int sub2(int a, int b) { return a - b; }
static binop_t *bop = sub2;
static int twice_apply(int (*f)(char), char c) { return f(c) + f(c); }

int main(void)
{
	int a[3];
	char *words[4];
	struct K kt[2];
	jbuf_t jb;
	struct R r;

	a[0] = 1; a[1] = 2; a[2] = 3;
	words[0] = "a"; words[1] = "b"; words[2] = "c"; words[3] = 0;
	kt[0].k = 10; kt[0].name = "x";
	kt[1].k = 20; kt[1].name = "y";
	jb[0] = 5; jb[3] = 6;
	r.type = 1;
	r.rtn = bump;

	printf("apply %d\n", apply(up, 'A'));
	printf("sum %d\n", sum(a, 3));
	printf("cnt %d\n", cnt(words));
	printf("keys %d\n", keys(kt, 2));
	printf("run %d\n", run(jb, 1));
	printf("hk %d\n", hk(dbl, 21));
	printf("nest %d\n", nest(twice_apply));
	r.rtn();
	hook = (void (__far *)(void))bump;
	hook();
	printf("rtn %d %d\n", r.type, hits);
	printf("binop %d\n", bop(10, 3));
	printf("paramdecl_probe done\n");
	return 0;
}

int apply(int (*f)(char), char c) { return f(c); }
int sum(int v[], int n) { int s = 0, i; for (i = 0; i < n; i++) s += v[i]; return s; }
int cnt(char *w[]) { int n = 0; while (w[n]) n++; return n; }
int keys(struct K t[], int n) { return t[n - 1].k + t[0].name[0]; }
int run(jbuf_t (jb), int i) { return jb[0] * 10 + jb[3] + i; }
int hk(int (__far *fn)(int), int v) { return fn(v); }
int nest(int (*g)(int (*)(char), char)) { return g(up, 'a'); }
