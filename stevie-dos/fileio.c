/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

void filemess(char *s)
{
	char *fname;
	if (Filename == NULL)
		fname = "";
	else
		fname = Filename;
	smsg("\"%s\" %s", fname, s);
}

void renum(void)
{
	LPTR *p;
	int l;

	l = 0;
	p = Filemem;
	while (p != NULL)
	{
		p->linep->num = l;
		p = (LPTR *)nextline(p);
		l = l + LINEINC;
	}

	Fileend->linep->num = 65535;
}

bool_t readfile(char *fname, LPTR *fromp, bool_t nochangename)
{
	FILE *f;
	LINE *curr;
	LINE *lp;
	LINE *dummy;
	char *buff;
	char *p;
	int i;
	int c;
	int len;
	long nchars;
	int unprint;
	int linecnt;
	bool_t wasempty;

	buff = (char *)alloc(1024);
	unprint = 0;
	linecnt = 0;
	wasempty = bufempty();

	curr = fromp->linep;

	if (!nochangename)
		Filename = (char *)strsave(fname);

	f = (FILE *)fopen(fname, "r");
	if (f == NULL)
	{
		free(buff);
		return TRUE;
	}

	filemess("");

	i = 0;
	nchars = 0;
	c = getc(f);
	while (c != EOF)
	{
		nchars = nchars + 1;

		if (c >= 128)
		{
			c = c - 128;
			unprint = unprint + 1;
		}

		/*
		 * Nulls are special, so they can't show up in the file.
		 */
		if (c == NUL)
		{
			unprint = unprint + 1;
			c = getc(f);
			continue;
		}

		if (c == 10)	/* newline - process the completed line */
		{
			*(buff + i) = 0;
			len = strlen(buff) + 1;
			lp = (LINE *)newline(len);
			if (lp == NULL)
				exit(1);

			strcpy(lp->s, buff);

			curr->next->prev = lp;	/* new line to next one */
			lp->next = curr->next;

			curr->next = lp;	/* new line to prior one */
			lp->prev = curr;

			curr = lp;		/* new line becomes current */
			i = 0;
			linecnt = linecnt + 1;
		}
		else
		{
			*(buff + i) = (char)c;
			i = i + 1;
		}
		c = getc(f);
	}
	fclose(f);

	/*
	 * If the buffer was empty when we started, we have to go back
	 * and remove the "dummy" line at Filemem and patch up the ptrs.
	 */
	if (wasempty)
	{
		dummy = Filemem->linep;	/* dummy line ptr */

		free(dummy->s);			/* free string space */
		Filemem->linep = Filemem->linep->next;
		free(dummy);			/* free LINE struct */
		Filemem->linep->prev = (LINE *)0;

		Curschar->linep = Filemem->linep;
		Topchar->linep = Filemem->linep;
	}

	if (unprint > 0)
		p = "\"%s\" %d lines, %ld characters (%d un-printable))";
	else
		p = "\"%s\" %d lines, %ld characters";

	sprintf(buff, p, fname, linecnt, nchars, unprint);
	msg(buff);
	renum();
	free(buff);
	return FALSE;
}


/*
 * writeit - write to file 'fname' lines 'start' through 'end'
 *
 * If either 'start' or 'end' contain null line pointers, the default
 * is to use the start or end of the file respectively.
 */
bool_t writeit(char *fname, LPTR *start, LPTR *end)
{
	FILE *f;
	char *buff;
	char *backup;
	char *s;
	long nchars;
	int lines;
	LPTR *p;

	buff = (char *)alloc(80);
	backup = (char *)alloc(16);

	sprintf(buff, "\"%s\"", fname);
	msg(buff);

	/*
	 * Form the backup file name - change foo.* to foo.bak
	 */
	strcpy(backup, fname);
	s = backup;
	while (*s && *s != '.')
		s = s + 1;
	*s = NUL;
	strcat(backup, ".bak");

	/*
	 * Delete any existing backup and move the current version
	 * to the backup.
	 */
	rename(fname, backup);

	if (P(P_CR))
		f = (FILE *)fopen(fname, "w");
	else
		f = (FILE *)fopenb(fname, "w");

	if (f == NULL)
	{
		emsg("Can't open file for writing!");
		free(buff);
		free(backup);
		return FALSE;
	}

	/*
	 * If we were given a bound, start there. Otherwise just
	 * start at the beginning of the file.
	 */
	if (start == NULL || start->linep == NULL)
		p = Filemem;
	else
		p = start;

	lines = 0;
	nchars = 0;
	while (1)
	{
		fprintf(f, "%s\n", p->linep->s);
		nchars = nchars + strlen(p->linep->s) + 1;
		lines = lines + 1;

		/*
		 * If we were given an upper bound, and we just did that
		 * line, then bag it now.
		 */
		if (end != NULL && end->linep != NULL)
		{
			if (end->linep == p->linep)
				break;
		}

		p = (LPTR *)nextline(p);
		if (p == NULL)
			break;
	}

	fclose(f);
	sprintf(buff, "\"%s\" %d lines, %ld characters", fname, lines, nchars);
	msg(buff);
	UNCHANGED;

	/*
	 * Remove the backup unless they want it left around
	 */
	if (!P(P_BK))
		remove(backup);

	free(buff);
	free(backup);
	return TRUE;
}
