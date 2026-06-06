/* inc_gchar_probe.c — replicate stevie's filetonext char-by-char walk
 * via inc/gchar.  Print each char gchar returns and what inc says.
 * Output should be the file content one byte per line, terminated by
 * NUL between lines, then EOF after last line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUL 0

struct line {
	struct line *prev, *next;
	char *s;
	int size;
	unsigned long num;
};

struct lptr {
	struct line *linep;
	int index;
};

typedef struct line LINE;
typedef struct lptr LPTR;

LPTR *Filemem;
LPTR *Filetop;
LPTR *Fileend;
LPTR *Topchar;

LINE *
newline(nchars)
int nchars;
{
	LINE *l;
	l = (LINE *) malloc(sizeof(LINE));
	if (l == NULL) return NULL;
	l->s = malloc(nchars + 1);
	if (l->s == NULL) return NULL;
	l->s[0] = 0;
	l->size = nchars + 1;
	l->prev = NULL;
	l->next = NULL;
	l->num = 0;
	return l;
}

int
inc(lp)
register LPTR *lp;
{
	register char *p;

	if (lp && lp->linep)
		p = &(lp->linep->s[lp->index]);
	else
		return -1;
	if (*p != NUL) {
		lp->index++;
		return ((p[1] != NUL) ? 0 : 1);
	}
	if (lp->linep->next != Fileend->linep) {
		lp->index = 0;
		lp->linep = lp->linep->next;
		return 1;
	}
	return -1;
}

int
gchar(lp)
register LPTR *lp;
{
	if (lp && lp->linep)
		return lp->linep->s[lp->index];
	return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buff[256];
	int i, c, linecnt;
	LINE *curr, *lp;
	LPTR memp;
	int chcount = 0;

	if (argc < 2) { printf("usage\n"); return 1; }

	Filemem = (LPTR *) malloc(sizeof(LPTR));
	Filetop = (LPTR *) malloc(sizeof(LPTR));
	Fileend = (LPTR *) malloc(sizeof(LPTR));
	Topchar = (LPTR *) malloc(sizeof(LPTR));

	Filemem->linep = newline(0);
	Filetop->linep = newline(0);
	Fileend->linep = newline(0);
	Filemem->index = Filetop->index = Fileend->index = 0;
	Filetop->linep->next = Filemem->linep;
	Filemem->linep->prev = Filetop->linep;
	Filemem->linep->next = Fileend->linep;
	Fileend->linep->prev = Filemem->linep;
	*Topchar = *Filemem;

	f = fopen(argv[1], "r");
	if (f == NULL) { printf("fopen fail\n"); return 1; }

	curr = Filemem->linep;
	linecnt = 0;
	i = 0;
	while ((c = getc(f)) != EOF) {
		if (c == '\n' || i == 255) {
			buff[i] = 0;
			lp = newline(strlen(buff));
			strcpy(lp->s, buff);
			curr->next->prev = lp;
			lp->next = curr->next;
			curr->next = lp;
			lp->prev = curr;
			curr = lp;
			i = 0;
			linecnt++;
		} else {
			buff[i++] = c;
		}
	}
	fclose(f);
	if (linecnt > 0) {
		LINE *dummy = Filemem->linep;
		free(dummy->s);
		Filemem->linep = Filemem->linep->next;
		free((char *)dummy);
		Filemem->linep->prev = Filetop->linep;
		Filetop->linep->next = Filemem->linep;
		Topchar->linep = Filemem->linep;
	}

	/* Walk like filetonext does — gchar + inc */
	memp = *Topchar;
	printf("memp.linep=%p index=%d\n", (void *)memp.linep, memp.index);
	while (chcount < 100) {
		c = gchar(&memp) & 0xff;
		i = inc(&memp);
		if (c == 0)
			printf("[NUL] inc=%d\n", i);
		else
			printf("'%c' (0x%02x) inc=%d\n", c, c, i);
		chcount++;
		if (i == -1) break;
	}
	printf("done chcount=%d\n", chcount);
	return 0;
}
