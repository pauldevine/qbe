/*
 * lptr_range_probe.c -- LPTR-like start/end range copy should stay bounded.
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

static struct LINE line1;
static struct LPTR cur;
static struct LPTR startop;

static void
move_word(struct LPTR *p)
{
	while (p->linep->s[p->index] != ' ' && p->linep->s[p->index] != '\0')
		p->index++;
}

static void
print_range(struct LPTR a, struct LPTR b)
{
	int i;
	printf("same=%d start=%d end=%d text=", a.linep == b.linep, a.index, b.index);
	for (i = a.index; a.linep == b.linep && i < b.index; i++)
		putchar(a.linep->s[i]);
	printf("\r\n");
}

int
main(void)
{
	line1.next = 0;
	line1.prev = 0;
	line1.s = "alpha beta gamma";
	line1.num = 1;

	cur.linep = &line1;
	cur.index = 0;

	startop = cur;
	move_word(&cur);
	print_range(startop, cur);

	return 0;
}
