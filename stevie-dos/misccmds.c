/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * MiniC doesn't support local struct variables, use global pointers
 */
LPTR *misc_gotoline_l;
LPTR *misc_csave;

void openfwd(int can_ai)
{
	LINE *l;
	LPTR *next;
	char *s;		/* string to be moved to new line, if any */
	int lnum;		/* for line number calculation */

	/*
	 * If we're in insert mode, we need to move the remainder of the
	 * current line onto the new line. Otherwise the new line is left
	 * blank.
	 */
	if (State == INSERT)
		s = &Curschar->linep->s[Curschar->index];
	else
		s = "";

	next = (LPTR *)nextline(Curschar);
	if (next == NULL)	/* open on last line */
		next = Fileend;

	/*
	 * By asking for as much space as the prior line had we make sure
	 * that we'll have enough space for any auto-indenting.
	 */
	l = (LINE *)newline(strlen(Curschar->linep->s) + SLOP);
	if (l == NULL)
		return;

	if (*s != NUL)
		strcpy(l->s, s);		/* copy string to new line */

	if (State == INSERT)		/* truncate current line at cursor */
		*s = NUL;

	Curschar->linep->next = l;	/* link neighbors to new line */
	next->linep->prev = l;

	l->prev = Curschar->linep;	/* link new line to neighbors */
	l->next = next->linep;

	if (next == Fileend)			/* new line at end */
		l->num = Curschar->linep->num + LINEINC;

	else if ((l->prev->num) + 1 == l->next->num)	/* no gap, renumber */
		renum();

	else {					/* stick it in the middle */
		lnum = (l->prev->num + l->next->num) / 2;
		l->num = lnum;
	}

	next = (LPTR *)nextline(Curschar);
	*Curschar = *next;	/* cursor moves down */
	Curschar->index = 0;

	s_ins(Cursrow + 1, 1);	/* insert a physical line */

	updatescreen();		/* because Botchar is now invalid... */

	cursupdate();		/* update Cursrow before insert */
}

void openbwd(int can_ai)
{
	LINE *l;
	LPTR *prev;
	LPTR *tmp;

	prev = (LPTR *)prevline(Curschar);

	l = (LINE *)newline(strlen(Curschar->linep->s) + SLOP);
	if (l == NULL)
		return;

	Curschar->linep->prev = l;	/* link neighbors to new line */
	if (prev != NULL)
		prev->linep->next = l;

	l->next = Curschar->linep;	/* link new line to neighbors */
	if (prev != NULL)
		l->prev = prev->linep;

	tmp = (LPTR *)prevline(Curschar);
	*Curschar = *tmp;	/* cursor moves up */
	Curschar->index = 0;

	if (prev == NULL)			/* new start of file */
		Filemem->linep = l;

	renum();	/* keep it simple - we don't do this often */

	cursupdate();			/* update Cursrow before insert */
	if (Cursrow != 0)
		s_ins(Cursrow, 1);		/* insert a physical line */

	updatescreen();
}

/*
 * opencmd
 *
 * Add a blank line above or below the current line.
 */
void opencmd(int dir, int can_ai)
{
	if (dir == FORWARD)
		openfwd(can_ai);
	else
		openbwd(can_ai);
}

int cntllines(LPTR *pbegin, LPTR *pend)
{
	LINE *lp;
	int lnum;

	lnum = 1;
	lp = pbegin->linep;
	while (lp != pend->linep)
	{
		lnum = lnum + 1;
		lp = lp->next;
	}

	return lnum;
}

/*
 * plines(p) - return the number of physical screen lines taken by line 'p'
 */
int plines(LPTR *p)
{
	int col;
	char *s;

	s = p->linep->s;

	if (*s == NUL)		/* empty line */
		return 1;

	/*
	 * If list mode is on, then the '$' at the end of
	 * the line takes up one extra column.
	 */
	if (P(P_LS))
		col = 1;
	else
		col = 0;

	while (*s != NUL)
	{
		if (*s == TAB && !P(P_LS))
			col = col + P(P_TS) - (col % P(P_TS));
		else
			col = col + chars[(int)(*s & 255)].ch_size;
		s = s + 1;
	}
	return ((col + (Columns - 1)) / Columns);
}

void fileinfo(void)
{
	long l1;
	long l2;
	char *buf;

	buf = (char *)alloc(80);

	if (bufempty())
	{
		msg("Buffer Empty");
		free(buf);
		return;
	}

	l1 = cntllines(Filemem, Curschar);
	l2 = cntllines(Filemem, Fileend) - 1;
	if (Filename != NULL)
	{
		if (Changed)
			sprintf(buf, "\"%s\" [Modified] line %ld of %ld -- %ld %% --",
				Filename, l1, l2, (l1 * 100) / l2);
		else
			sprintf(buf, "\"%s\" line %ld of %ld -- %ld %% --",
				Filename, l1, l2, (l1 * 100) / l2);
	}
	else
	{
		sprintf(buf, "\"No File\" line %ld of %ld -- %ld %% --",
			l1, l2, (l1 * 100) / l2);
	}
	msg(buf);
	free(buf);
}

/*
 * gotoline(n) - return a pointer to line 'n'
 *
 * Returns a pointer to the last line of the file if n is zero, or
 * beyond the end of the file.
 */
LPTR *gotoline(int n)
{
	LPTR *p;

	/* Allocate static LPTR on first use */
	if (misc_gotoline_l == NULL)
		misc_gotoline_l = (LPTR *)alloc(16);

	misc_gotoline_l->index = 0;

	if (n == 0)
	{
		p = (LPTR *)prevline(Fileend);
		*misc_gotoline_l = *p;
	}
	else
	{
		*misc_gotoline_l = *Filemem;
		n = n - 1;
		while (n > 0)
		{
			p = (LPTR *)nextline(misc_gotoline_l);
			if (p == NULL)
				break;
			*misc_gotoline_l = *p;
			n = n - 1;
		}
	}
	return misc_gotoline_l;
}

void inschar(int c)
{
	char *p;
	char *pend;
	LPTR *lpos;

	/* make room for the new char. */
	if (!canincrease(1))
		return;

	p = &Curschar->linep->s[strlen(Curschar->linep->s) + 1];
	pend = &Curschar->linep->s[Curschar->index];

	while (p > pend)
	{
		*p = *(p - 1);
		p = p - 1;
	}

	*p = c;

	/*
	 * If we're in insert mode and showmatch mode is set, then
	 * check for right parens and braces. If there isn't a match,
	 * then beep. If there is a match AND it's on the screen, then
	 * flash to it briefly. If it isn't on the screen, don't do anything.
	 */
	if (P(P_SM) && State == INSERT && (c == ')' || c == '}' || c == ']'))
	{
		lpos = (LPTR *)showmatch();
		if (lpos == NULL)	/* no match, so beep */
			beep();
		else if (LINEOF(lpos) >= LINEOF(Topchar))
		{
			/* Allocate csave on first use */
			if (misc_csave == NULL)
				misc_csave = (LPTR *)alloc(16);

			updatescreen();		/* show the new char first */
			*misc_csave = *Curschar;
			*Curschar = *lpos;	/* move to matching char */
			cursupdate();
			windgoto(Cursrow, Curscol);
			delay();		/* brief pause */
			*Curschar = *misc_csave;	/* restore cursor position */
			cursupdate();
		}
	}

	inc(Curschar);
	CHANGED;
}

void insstr(char *s)
{
	char *p;
	char *endp;
	int k;
	int n;

	n = strlen(s);

	/* Move everything in the file over to make */
	/* room for the new string. */
	if (!canincrease(n))
		return;

	endp = &Curschar->linep->s[Curschar->index];
	p = Curschar->linep->s + strlen(Curschar->linep->s) + 1 + n;

	while (p > endp)
	{
		*p = *(p - n);
		p = p - 1;
	}

	p = &Curschar->linep->s[Curschar->index];
	k = 0;
	while (k < n)
	{
		*p = *s;
		p = p + 1;
		s = s + 1;
		inc(Curschar);
		k = k + 1;
	}
	CHANGED;
}

bool_t delchar(bool_t fixpos)
{
	int i;

	/* Check for degenerate case; there's nothing in the file. */
	if (bufempty())
		return FALSE;

	if (lineempty())	/* can't do anything */
		return FALSE;

	/* Delete the char. at Curschar by shifting everything */
	/* in the line down. */
	i = Curschar->index + 1;
	while (i < Curschar->linep->size)
	{
		Curschar->linep->s[i - 1] = Curschar->linep->s[i];
		i = i + 1;
	}

	/* If we just took off the last character of a non-blank line, */
	/* we don't want to end up positioned at the newline. */
	if (fixpos)
	{
		if (gchar(Curschar) == NUL && Curschar->index > 0 && State != INSERT)
			Curschar->index = Curschar->index - 1;
	}
	CHANGED;

	return TRUE;
}


void delline(int nlines)
{
	LINE *p;
	LINE *q;
	int doscreen;	/* if true, update the screen */

	doscreen = TRUE;

	/*
	 * There's no point in keeping the screen updated if we're
	 * deleting more than a screen's worth of lines.
	 */
	if (nlines > (Rows - 1))
	{
		doscreen = FALSE;
		s_del(Cursrow, Rows - 1);	/* flaky way to clear rest of screen */
	}

	while (nlines > 0)
	{
		nlines = nlines - 1;

		if (bufempty())			/* nothing to delete */
			break;

		if (buf1line())		/* just clear the line */
		{
			Curschar->linep->s[0] = NUL;
			Curschar->index = 0;
			break;
		}

		p = Curschar->linep->prev;
		q = Curschar->linep->next;

		if (p == NULL)		/* first line of file so... */
		{
			Filemem->linep = q;	/* adjust start of file */
			Topchar->linep = q;	/* and screen */
		}
		else
			p->next = q;
		q->prev = p;

		clrmark(Curschar->linep);	/* clear marks for the line */

		/*
		 * Delete the correct number of physical lines on the screen
		 */
		if (doscreen)
			s_del(Cursrow, plines(Curschar));

		/*
		 * If deleting the top line on the screen, adjust Topchar
		 */
		if (Topchar->linep == Curschar->linep)
			Topchar->linep = q;

		free(Curschar->linep->s);
		free(Curschar->linep);

		Curschar->linep = q;
		Curschar->index = 0;		/* is this right? */
		CHANGED;

		/* If we delete the last line in the file, back up */
		if (Curschar->linep == Fileend->linep)
		{
			Curschar->linep = Curschar->linep->prev;
			/* and don't try to delete any more lines */
			break;
		}
	}
}
