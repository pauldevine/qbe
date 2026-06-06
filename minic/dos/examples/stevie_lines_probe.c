/* stevie_lines_probe.c — mirror stevie's LINE/LPTR struct layout and
 * the readfile-style insertion pattern.  Pins whether the bug is in
 * stevie or in a pattern this probe doesn't yet exercise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buff[256];
	int i, c, linecnt;
	LINE *curr, *lp;
	LPTR *p;

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

	/* struct copy — same shape as stevie's filealloc */
	*Topchar = *Filemem;

	f = fopen(argv[1], "r");
	if (f == NULL) { printf("fopen failed\n"); return 1; }

	curr = Filemem->linep;
	linecnt = 0;
	i = 0;
	while ((c = getc(f)) != EOF) {
		if (c == '\n' || i == 255) {
			buff[i] = 0;
			lp = newline(strlen(buff));
			if (lp == NULL) { printf("malloc fail\n"); return 1; }
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

	/* If we have lines, free Filemem's dummy and point to first real */
	if (linecnt > 0) {
		LINE *dummy = Filemem->linep;
		free(dummy->s);
		Filemem->linep = Filemem->linep->next;
		free((char *)dummy);
		Filemem->linep->prev = Filetop->linep;
		Filetop->linep->next = Filemem->linep;
		Topchar->linep = Filemem->linep;
	}

	/* Walk from Topchar->linep and print each line */
	printf("linecnt=%d\n", linecnt);
	lp = Topchar->linep;
	while (lp != Fileend->linep && lp != NULL) {
		printf("[%s] (size=%d, num=%lu)\n", lp->s, lp->size, lp->num);
		lp = lp->next;
	}
	printf("done\n");
	return 0;
}
