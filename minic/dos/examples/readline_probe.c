/* readline_probe.c — exercise the read-buffer → strcpy-to-far-malloc →
 * print pattern that stevie's readfile uses.  If this works under
 * compact/large/huge, the bug is in stevie's screen-rendering or
 * LINE-struct walking; if it fails, the bug is in this core pattern.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct line {
	struct line *next;
	char *s;
	int size;
};

int main(argc, argv)
int argc;
char **argv;
{
	FILE *f;
	char buf[80];
	int i, n, c;
	struct line *head, *cur, *lp;

	if (argc < 2) { printf("usage\n"); return 1; }
	f = fopen(argv[1], "r");
	if (f == NULL) { printf("fopen failed\n"); return 1; }

	head = NULL;
	cur = NULL;
	i = 0;
	while ((c = getc(f)) != EOF) {
		if (c == '\n' || i == 79) {
			buf[i] = 0;
			lp = (struct line *)malloc(sizeof(struct line));
			if (lp == NULL) { printf("malloc fail\n"); return 1; }
			lp->s = malloc(strlen(buf) + 1);
			if (lp->s == NULL) { printf("malloc s fail\n"); return 1; }
			strcpy(lp->s, buf);
			lp->size = i + 1;
			lp->next = NULL;
			if (head == NULL) head = lp;
			else cur->next = lp;
			cur = lp;
			i = 0;
		} else {
			buf[i++] = c;
		}
	}
	fclose(f);

	/* Walk the list and print every line's text. */
	n = 0;
	for (lp = head; lp != NULL; lp = lp->next) {
		printf("line %d: [%s] (size=%d)\n", n++, lp->s, lp->size);
	}
	printf("done %d lines\n", n);
	return 0;
}
