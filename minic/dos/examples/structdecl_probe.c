/*
 * structdecl_probe.c -- C-Kermit link campaign item 2: declaration syntax
 * around struct/union types.  All of these were parse errors:
 *
 *     struct S { ... } s, *p, arr[3];     tag definition + declarators
 *     typedef struct S { ... } S_t;       (the typedef form still works)
 *     extern struct T *f(int);            extern prototype returning a
 *                                         struct pointer (C-Kermit G3)
 *     struct { unsigned :16, :16; ... }   unnamed bitfields (G19,
 *                                         Watcom dos.h INTPACKB)
 *     long double d;                      (G7; == double here)
 *     void const *p;                      east const (G23)
 *     *t = c ? a : b;                     aggregate ?: as an assignment
 *                                         / return source (G24; "invalid
 *                                         lvalue"); the condition must be
 *                                         evaluated once
 *
 * and a struct member list ignored its later declarators' `*`:
 * `int a, *b;` made b an int, `char *c, d;` made d a char *.
 *
 * Bug-loud: the pre-fix compiler rejects this file.  Output is ok/FAIL and
 * model-independent values: one golden for all five models.
 */
#include <stdio.h>

struct S { int a; char c; } gs, *gp = &gs, garr[3];
static struct Cnt { int n; } cnt;
typedef struct Pt { int x, y; } Pt_t;
union U { int i; char b[2]; } gu;

struct T;
extern struct T *mk(int);
struct T { int v; };
static struct T tpool[2];
struct T *mk(int v) { tpool[v & 1].v = v; return &tpool[v & 1]; }

struct Pad { unsigned :16; unsigned a:4; };
struct Intb { unsigned :16, :16; unsigned char al, ah; };
struct Zero { unsigned a:3; unsigned :0; unsigned b:3; };
struct Skip { unsigned a:4; unsigned :4; unsigned b:4; };

struct M { int a, *b; char *c, d; };

long double ld;
void const *vcp;
char const *ccp = "east";

static void ok(char *what, int cond)
{
	printf("%s %s\n", what, cond ? "ok" : "FAIL");
}

static struct Pt pa, pb;

static int pick_into(int c, struct Pt *t)
{
	*t = c++ ? pa : pb;
	return c;
}

static struct Pt pick_ret(int c)
{
	struct Pt *pp = &pb;
	return c ? pa : *pp;
}

static int local_tag(int k)
{
	struct L { int a; int b; } v, *vp;
	vp = &v;
	vp->a = k;
	vp->b = k * 2;
	return v.a + v.b;
}

int main(void)
{
	struct M m;
	struct Intb ib;
	struct Pad pd;
	struct Zero z;
	struct Skip sk;
	Pt_t pt;
	struct Pt pt2;

	gs.a = 7; gp->c = 'q';
	garr[2].a = 9;
	cnt.n = 3;
	ok("tag+vars", gs.a == 7 && gs.c == 'q' && garr[2].a == 9 && cnt.n == 3);
	ok("tag+array", sizeof(garr) == 3 * sizeof(struct S));
	pt.x = 1; pt2.y = 2;
	ok("typedef tag", pt.x + pt2.y == 3 && sizeof(pt) == sizeof(struct Pt));
	gu.i = 0; gu.b[0] = 1;
	ok("union tag+var", gu.i != 0 && sizeof(gu) == sizeof(int));
	ok("extern struct fn", mk(4)->v == 4 && mk(5)->v == 5);
	printf("local tag %d\n", local_tag(5));

	ok("pad size", sizeof(pd) == 4);
	pd.a = 15;
	ok("pad field", pd.a == 15);
	ok("intpackb", (char *)&ib.al - (char *)&ib == 4
	    && (char *)&ib.ah - (char *)&ib.al == 1);
	ok("zero width", sizeof(z) == 2 * sizeof(unsigned));
	z.a = 5; z.b = 6;
	ok("zero fields", z.a == 5 && z.b == 6);
	sk.a = 1; sk.b = 2;
	ok("skip fields", sk.a == 1 && sk.b == 2);

	m.a = 4;
	m.b = &m.a;
	*m.b = 11;
	m.c = "cc";
	m.d = 'd';
	ok("member ptrs", m.a == 11 && m.c[1] == 'c' && m.d == 'd');
	ok("member sizes", sizeof(m.d) == 1 && sizeof(m.b) == sizeof(int *)
	    && sizeof(m.a) == sizeof(int));

	pa.x = 1; pa.y = 2; pb.x = 3; pb.y = 4;
	pt.x = pick_into(5, &pt2);
	ok("cond assign", pt2.x == 1 && pt2.y == 2 && pt.x == 6);
	pick_into(0, &pt2);
	pt = pick_ret(0);
	ok("cond ret", pt2.x == 3 && pt.y == 4 && pick_ret(1).y == 2);
	ok("long double", sizeof(ld) >= sizeof(float));
	vcp = ccp;
	ok("east const", ((char const *)vcp)[0] == 'e');
	printf("structdecl_probe done\n");
	return 0;
}
