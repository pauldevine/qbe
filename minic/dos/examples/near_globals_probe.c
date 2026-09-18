/* minic -G (near globals) probe.  Built with --near-globals in the far-data
 * models: small named globals live in DGROUP and are accessed DS-relative;
 * string literals and objects >= 128 B live in the module's far _FAR
 * segment.  Every check crosses the two access paths: a value written
 * DIRECTLY (near DS:[_g] for small objects) is read back through its FAR
 * address (&g = seg:off), and vice versa, so a placement/access mismatch
 * (object placed far but accessed near, or a group-frame disagreement
 * between DS:[_g] and seg _g:_g) prints a wrong value.  The second TU
 * (near_globals_probe2.c) checks the extern side of the type-based rule. */
#include <stdio.h>
#include <string.h>

struct small { int a; long b; char c; };
struct bigs { int v[70]; int tail; };          /* 142 B: far-placed */

int g;
long lg;
char *msg = "near-globals";
struct small sm;
struct bigs bs;
char table[300];                                /* array: far-placed */
static int sg = 7;

/* defined in near_globals_probe2.c */
extern int ext_counter;
extern struct small ext_sm;
extern struct bigs ext_big;
extern char ext_buf[];
int bump_ext(void);
int ext_sum(void);

static int
counter(void)
{
	static int n;
	n++;
	return n;
}

static int
via_ptr(int *p, int v)
{
	int old;
	old = *p;
	*p = v;
	return old;
}

int
main(void)
{
	int *pg;
	long *pl;
	struct small *ps;
	struct bigs *pb;
	char *pc;
	int i, ok;

	/* scalar: direct write, far read; far write, direct read */
	g = 1234;
	pg = &g;
	ok = (*pg == 1234);
	via_ptr(&g, 4321);
	printf("scalar %s %d\n", ok && g == 4321 ? "ok" : "FAIL", g);

	lg = 100000L;
	pl = &lg;
	*pl += 5;
	printf("long %s %ld\n", lg == 100005L ? "ok" : "FAIL", lg);

	/* small struct: member writes direct, reads via pointer */
	sm.a = 11; sm.b = 70000L; sm.c = 'x';
	ps = &sm;
	ok = ps->a == 11 && ps->b == 70000L && ps->c == 'x';
	ps->a = 12;
	printf("small_struct %s %d\n", ok && sm.a == 12 ? "ok" : "FAIL", sm.a);

	/* big struct: far-placed, direct + pointer access */
	bs.v[3] = 33; bs.tail = 99;
	pb = &bs;
	ok = pb->v[3] == 33 && pb->tail == 99;
	pb->tail = 100;
	printf("big_struct %s %d\n", ok && bs.tail == 100 ? "ok" : "FAIL", bs.tail);

	/* far-placed array */
	for (i = 0; i < 300; i++)
		table[i] = (char)(i & 0x7f);
	pc = table;
	ok = 1;
	for (i = 0; i < 300; i++)
		if (pc[i] != (char)(i & 0x7f))
			ok = 0;
	printf("array %s %d\n", ok ? "ok" : "FAIL", table[257]);

	/* string literal (far-placed) through a near pointer variable */
	printf("literal %s %s\n", strcmp(msg, "near-globals") == 0 ? "ok" : "FAIL", msg);

	/* static file-scope and function-local statics */
	sg += 3;
	counter(); counter();
	printf("statics %s %d %d\n", sg == 10 && counter() == 3 ? "ok" : "FAIL", sg, counter());

	/* extern side (other TU, same rule) */
	ext_counter = 5;
	bump_ext();
	ext_sm.b = 123456L;
	ext_big.tail = 77;
	ext_buf[1] = 'Q';
	printf("extern %s %d %d\n",
	    ext_counter == 6 && ext_sum() == 123456L % 1000 + 77 + 'Q' ? "ok" : "FAIL",
	    ext_counter, ext_sum());
	return 0;
}
