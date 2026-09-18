/*
 * initdecl_probe.c -- C-Kermit link campaign item 2 (G8, G15, const dims):
 * multi-declarator lists whose FIRST declarator is initialized.
 *
 * These were all parse errors (the triage split or rewrote them):
 *
 *     int spsiz = 90, spmax = 90, rpsiz;        file scope        (G8)
 *     CHAR *bigsbuf = NULL, *bigrbuf = NULL;    file scope        (G8)
 *     { char *p = 0, *q = s; ... }              block scope       (G15)
 *     { register char c = 0, *p; ... }          block scope       (G15)
 *     char *p = 0, name[128+1];                 constant-expr dim in a
 *                                               later declarator
 *
 * and a block `char *p = 0, c = 1;` gave c the FIRST declarator's type
 * (char *) instead of the specifier's (char).
 *
 * Bug-loud: the pre-fix compiler rejects this file.  Output is values and
 * sizes only: one golden for all five models.
 */
#include <stdio.h>
#include <string.h>

int spsiz = 90, spmax = 91, rpsiz, lastm = -3;
char *bigsbuf = 0, *bigrbuf = "rbuf", *third;
static int sa = 7, sb, *sp = &sa, sarr[4];
long lg = 70000L, lh = 5;
char fa[64+1], fb[2*8];

static void ok(char *what, int cond)
{
	printf("%s %s\n", what, cond ? "ok" : "FAIL");
}

static int top(char *s)
{
	char *p = 0, *q = s, c = 'k', name[16+1];
	int i = 0, n, *ip = &i;

	n = 3;
	*ip = 4;
	strcpy(name, q);
	ok("top ptrs", p == 0 && q == s && strcmp(name, "hello") == 0);
	ok("top char", sizeof(c) == 1 && c == 'k');
	ok("top dim", sizeof(name) == 17);
	return i + n;
}

static int loop(void)
{
	int k, t = 0;

	for (k = 0; k < 3; k++) {
		register char c = 'a', *p;
		char *a = "xyz", *b = a + 1, d = 0;
		c = (char)(c + k);
		p = b;
		d = p[k < 2 ? k : 1];
		t = t * 100 + (c - 'a') * 10 + (d - 'x');
	}
	return t;
}

int main(void)
{
	ok("g8 ints", spsiz == 90 && spmax == 91 && rpsiz == 0 && lastm == -3);
	ok("g8 ptrs", bigsbuf == 0 && strcmp(bigrbuf, "rbuf") == 0 && third == 0);
	ok("g8 static", sa == 7 && sb == 0 && *sp == 7 && sarr[3] == 0 && sizeof(sarr) / sizeof(sarr[0]) == 4);
	ok("g8 long", lg == 70000L && lh == 5);
	ok("dims", sizeof(fa) == 65 && sizeof(fb) == 16);
	printf("top %d\n", top("hello"));
	printf("loop %d\n", loop());
	printf("initdecl_probe done\n");
	return 0;
}
