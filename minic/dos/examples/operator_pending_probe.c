/*
 * operator_pending_probe.c -- model Stevie's operator-pending LPTR flow.
 */

#include <stdio.h>

struct LINE {
	struct LINE *next;
	struct LINE *prev;
	char *s;
	unsigned long num;
};

struct LPTR {
	struct LINE *linep;
	int index;
};

static struct LINE filetop;
static struct LINE fileend;
static struct LINE line1;
static struct LINE line2;
static struct LPTR cur;
static struct LPTR startop;
static int pending;
static int mtype;
static int mincl;

#define MCHAR 0

static int
cls(int c)
{
	if (c == ' ' || c == '\t' || c == '\0')
		return 0;
	return 1;
}

static int
inc_lptr(struct LPTR *p)
{
	if (p->linep->s[p->index] != '\0') {
		p->index++;
		return p->linep->s[p->index] != '\0' ? 0 : 1;
	}
	if (p->linep->next != &fileend) {
		p->linep = p->linep->next;
		p->index = 0;
		return 1;
	}
	return -1;
}

static struct LPTR *
fwd_word(void)
{
	static struct LPTR pos;
	int sclass;

	sclass = cls(cur.linep->s[cur.index]);
	pos = cur;

	if (inc_lptr(&pos) == -1)
		return 0;

	if (sclass != 0) {
		while (cls(pos.linep->s[pos.index]) == sclass) {
			if (inc_lptr(&pos) == -1)
				return 0;
		}
		if (cls(pos.linep->s[pos.index]) != 0)
			return &pos;
	}

	while (cls(pos.linep->s[pos.index]) == 0) {
		if (pos.index == 0 && pos.linep->s[0] == '\0')
			break;
		if (inc_lptr(&pos) == -1)
			return 0;
	}
	return &pos;
}

static int
lt(struct LPTR *a, struct LPTR *b)
{
	int an;
	int bn;

	an = a->linep->num;
	bn = b->linep->num;
	if (an != bn)
		return an < bn;
	return a->index < b->index;
}

static void
pswap(struct LPTR *a, struct LPTR *b)
{
	struct LPTR tmp;
	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int
cntllines(struct LPTR *a, struct LPTR *b)
{
	int n;
	struct LINE *lp;

	n = 0;
	for (lp = a->linep; lp != 0; lp = lp->next) {
		n++;
		if (lp == b->linep)
			break;
	}
	return n;
}

static int
equal(struct LPTR *a, struct LPTR *b)
{
	return a->linep == b->linep && a->index == b->index;
}

static int
ltoreq(struct LPTR *a, struct LPTR *b)
{
	return lt(a, b) || equal(a, b);
}

static void
doyank_like(void)
{
	struct LPTR top;
	struct LPTR bot;
	struct LINE *lp;
	char *s;
	int i;
	int nlines;

	top = startop;
	bot = cur;

	if (lt(&bot, &top))
		pswap(&top, &bot);

	nlines = cntllines(&top, &bot);
	printf("range same=%d nlines=%d top=%d:%d bot=%d:%d text=",
	    top.linep == bot.linep, nlines,
	    (int)top.linep->num, top.index, (int)bot.linep->num, bot.index);

	if (mtype == MCHAR && !mincl) {
		if (bot.index)
			bot.index--;
		else {
			bot.linep = bot.linep->prev;
			bot.index = 0;
			while (bot.linep->s[bot.index] != '\0')
				bot.index++;
		}
	}

	if (top.linep == bot.linep) {
		s = top.linep->s;
		for (i = top.index; i <= bot.index; i++)
			putchar(s[i] != '\0' ? s[i] : '\n');
	} else {
		s = top.linep->s;
		for (i = top.index; s[i] != '\0'; i++)
			putchar(s[i]);
		putchar('\n');
		for (lp = top.linep->next; lp != bot.linep; lp = lp->next) {
			for (i = 0; lp->s[i] != '\0'; i++)
				putchar(lp->s[i]);
			putchar('\n');
		}
		for (i = 0; i <= bot.index; i++)
			putchar(bot.linep->s[i]);
	}
	printf("\r\n");
}

static void
doyank_iter_like(void)
{
	struct LPTR top;
	struct LPTR bot;

	top = startop;
	bot = cur;

	if (lt(&bot, &top))
		pswap(&top, &bot);

	if (mtype == MCHAR && !mincl) {
		if (bot.index)
			bot.index--;
		else {
			bot.linep = bot.linep->prev;
			bot.index = 0;
			while (bot.linep->s[bot.index] != '\0')
				bot.index++;
		}
	}

	printf("iter=");
	for (; ltoreq(&top, &bot); inc_lptr(&top))
		putchar(top.linep->s[top.index] != '\0' ? top.linep->s[top.index] : '\n');
	printf("\r\n");
}

static void
normal(int c)
{
	struct LPTR *moved;

	if (c == 'd') {
		startop = cur;
		pending = c;
		return;
	}

	if (c == 'w') {
		moved = fwd_word();
		cur = *moved;
	}
}

int
main(void)
{
	filetop.next = &line1;
	filetop.prev = 0;
	filetop.s = "";
	filetop.num = 0;

	line1.next = &line2;
	line1.prev = &filetop;
	line1.s = "alpha beta gamma";
	line1.num = 1;

	line2.next = &fileend;
	line2.prev = &line1;
	line2.s = "delta epsilon";
	line2.num = 2;

	fileend.next = 0;
	fileend.prev = &line2;
	fileend.s = "";
	fileend.num = 3;

	cur.linep = &line1;
	cur.index = 0;
	pending = 0;
	mtype = MCHAR;
	mincl = 0;

	normal('d');
	normal('w');

	printf("pending=%d same=%d start=%d end=%d\r\n",
	    pending == 'd', startop.linep == cur.linep, startop.index, cur.index);
	doyank_like();
	doyank_iter_like();

	return 0;
}
