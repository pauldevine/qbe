/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 *
 * Simplified screen handling routines for DOS text mode.
 */

#include "stevie.h"

/*
 * Global variables for screen handling
 */
int Cline_size;		/* size (in rows) of the cursor line */
int Cline_row;		/* starting row of the cursor line */

/*
 * plines(p) - return the number of physical screen lines taken by line 'p'
 */
int plines(LPTR *p)
{
	int col;
	char *s;
	int c;
	int n;

	col = 0;
	s = p->linep->s;

	while (*s != NUL)
	{
		c = *s;
		s = s + 1;
		if (c == TAB && !P(P_LS))
			col = col + (P(P_TS) - (col % P(P_TS)));
		else
			col = col + chars[c & 0xff].ch_size;
	}

	/*
	 * If list mode is on, then the '$' at the end of the line
	 * takes up one extra column.
	 */
	if (P(P_LS))
		col = col + 1;

	/*
	 * If 'col' is 0, then the line must be empty.
	 * Just return 1.
	 */
	if (col == 0)
		return 1;

	return ((col + (Columns - 1)) / Columns);
}

/*
 * filetonext()
 *
 * Transfer a screenful of stuff from Filemem to Nextscreen,
 * and update Botchar.
 */
void filetonext(void)
{
	int row;
	int col;
	char *screenp;
	char *endscreen;
	char *nextrow;
	LPTR memp;
	LPTR save;
	char c;
	int n;
	int done;
	int srow;
	char *p;
	int nextra;
	int extra_idx;

	memp = *Topchar;
	save = memp;

	screenp = Nextscreen;
	endscreen = Nextscreen + ((Rows - 1) * Columns);

	srow = 0;
	done = 0;
	row = 0;
	col = 0;
	nextra = 0;

	while ((int)screenp < (int)endscreen && done == 0)
	{
		/* Get the next character to put on the screen */

		if (nextra > 0)
		{
			nextra = nextra - 1;
			c = ' ';  /* Simplified - just use spaces for tabs */
		}
		else
		{
			c = gchar(&memp);
			if (inc(&memp) == -1)
				done = 1;

			/* Handle special characters */
			if (c == TAB && !P(P_LS))
			{
				/* Tab - expand to spaces */
				nextra = ((P(P_TS) - 1) - col % P(P_TS));
				c = ' ';
			}
			else if (c == NUL && P(P_LS))
			{
				nextra = 1;
				c = '$';
			}
			else if (c != NUL && chars[c & 0xff].ch_size > 1)
			{
				/* Non-printable character - show first char of representation */
				p = chars[c & 0xff].ch_str;
				c = *p;
			}
		}

		if (c == NUL)
		{
			srow = row + 1;
			row = row + 1;
			/* Blank out rest of this row */
			nextrow = Nextscreen + (row * Columns);
			while ((int)screenp < (int)nextrow)
			{
				*screenp = ' ';
				screenp = screenp + 1;
			}
			col = 0;
			save = memp;
		}
		else
		{
			if (col >= Columns)
			{
				row = row + 1;
				col = 0;
			}
			*screenp = c;
			screenp = screenp + 1;
			col = col + 1;
		}
	}

	/* If we didn't finish the last line, mark screen overflow */
	if (done == 0 && c != NUL)
	{
		/* Clear rest of screen and mark unused lines with '@' */
		while ((int)screenp < (int)endscreen)
		{
			*screenp = ' ';
			screenp = screenp + 1;
		}
		screenp = Nextscreen + (srow * Columns);
		while (srow < (Rows - 1))
		{
			*screenp = '@';
			screenp = screenp + Columns;
			srow = srow + 1;
		}
		*Botchar = save;
		return;
	}

	/* Blank rest of screen */
	while ((int)screenp < (int)endscreen)
	{
		*screenp = ' ';
		screenp = screenp + 1;
	}

	/* Put '~'s on rows that aren't part of the file */
	if (col != 0)
		row = row + 1;
	screenp = Nextscreen + (row * Columns);
	while (row < Rows)
	{
		*screenp = '~';
		screenp = screenp + Columns;
		row = row + 1;
	}

	if (done)
		*Botchar = *Fileend;
	else
		*Botchar = memp;
}

/*
 * nexttoscreen()
 *
 * Transfer the contents of Nextscreen to the screen,
 * using Realscreen to avoid unnecessary output.
 */
void nexttoscreen(void)
{
	char *np;
	char *rp;
	char *endscreen;
	int row;
	int col;
	int gorow;
	int gocol;

	np = Nextscreen;
	rp = Realscreen;
	endscreen = np + ((Rows - 1) * Columns);

	gorow = -1;
	gocol = -1;
	row = 0;
	col = 0;

	outstr(T_CI);		/* disable cursor */

	while ((int)np < (int)endscreen)
	{
		if (*np != *rp)
		{
			/* Need to update this position */
			if (gocol != col || gorow != row)
			{
				/* If just off by one, output previous char */
				if (gorow == row && gocol + 1 == col)
				{
					outchar(*(np - 1));
					gocol = gocol + 1;
				}
				else
				{
					windgoto(row, col);
					gorow = row;
					gocol = col;
				}
			}
			*rp = *np;
			outchar(*np);
			gocol = gocol + 1;
		}
		np = np + 1;
		rp = rp + 1;
		col = col + 1;
		if (col >= Columns)
		{
			col = 0;
			row = row + 1;
		}
	}

	outstr(T_CV);		/* enable cursor again */
}

/*
 * updatescreen() - update the entire screen
 */
void updatescreen(void)
{
	filetonext();
	nexttoscreen();
}

/*
 * updateline() - update the line the cursor is on
 *
 * For simplicity, just call updatescreen().
 * A real optimization would only update the cursor line.
 */
void updateline(void)
{
	updatescreen();
}

/*
 * s_ins(row, nlines) - insert 'nlines' lines at 'row'
 *
 * Scrolls the screen down by inserting blank lines.
 */
void s_ins(int row, int nlines)
{
	char *src;
	char *dst;
	int count;
	int i;

	if (nlines <= 0)
		return;

	/* Move lines down */
	src = Realscreen + ((Rows - 2 - nlines) * Columns);
	dst = Realscreen + ((Rows - 2) * Columns);
	count = (Rows - 2 - nlines - row) * Columns;

	while (count > 0)
	{
		dst = dst - 1;
		src = src - 1;
		*dst = *src;
		count = count - 1;
	}

	/* Clear inserted lines */
	dst = Realscreen + (row * Columns);
	count = nlines * Columns;
	while (count > 0)
	{
		*dst = ' ';
		dst = dst + 1;
		count = count - 1;
	}

	/* Use terminal insert line if available, otherwise redraw */
	if (T_IL != (char *)0 && *T_IL != NUL)
	{
		windgoto(row, 0);
		i = 0;
		while (i < nlines)
		{
			outstr(T_IL);
			i = i + 1;
		}
	}
}

/*
 * s_del(row, nlines) - delete 'nlines' lines at 'row'
 *
 * Scrolls the screen up by deleting lines.
 */
void s_del(int row, int nlines)
{
	char *src;
	char *dst;
	int count;
	int i;

	if (nlines <= 0)
		return;

	/* Move lines up */
	src = Realscreen + ((row + nlines) * Columns);
	dst = Realscreen + (row * Columns);
	count = (Rows - 2 - row - nlines) * Columns;

	while (count > 0)
	{
		*dst = *src;
		dst = dst + 1;
		src = src + 1;
		count = count - 1;
	}

	/* Clear vacated lines at bottom */
	count = nlines * Columns;
	while (count > 0)
	{
		*dst = ' ';
		dst = dst + 1;
		count = count - 1;
	}

	/* Use terminal delete line if available, otherwise redraw */
	if (T_DL != (char *)0 && *T_DL != NUL)
	{
		windgoto(row, 0);
		i = 0;
		while (i < nlines)
		{
			outstr(T_DL);
			i = i + 1;
		}
	}
}

/*
 * scrollup(n) - scroll the screen up 'n' lines
 */
void scrollup(int n)
{
	LPTR *p;
	int i;
	int done;

	/* Move Topchar down by 'n' lines */
	done = 0;
	i = 0;
	while (i < n && done == 0)
	{
		p = (LPTR *)nextline(Topchar);
		if (p == NULL)
			done = 1;
		else
			*Topchar = *p;
		i = i + 1;
	}
	s_del(0, i);
}

/*
 * scrolldown(n) - scroll the screen down 'n' lines
 */
void scrolldown(int n)
{
	LPTR *p;
	int i;
	int done;

	/* Move Topchar up by 'n' lines */
	done = 0;
	i = 0;
	while (i < n && done == 0)
	{
		p = (LPTR *)prevline(Topchar);
		if (p == NULL)
			done = 1;
		else
			*Topchar = *p;
		i = i + 1;
	}
	s_ins(0, i);
}

/*
 * cursupdate() - update the cursor position
 *
 * Make sure the cursor is visible and update Cursrow, Curscol, Cursvcol.
 */
void cursupdate(void)
{
	LPTR *p;
	int inc;
	int c;
	int nlines;
	int i;
	int didinc;
	int twothirds;
	int handled;

	handled = 0;

	if (bufempty())		/* special case - file is empty */
	{
		*Topchar = *Filemem;
		*Curschar = *Filemem;
		handled = 1;
	}
	if (handled == 0 && LINEOF(Curschar) < LINEOF(Topchar))
	{
		nlines = cntllines(Curschar, Topchar);
		/* Cursor is above top of screen - put it at top */
		*Topchar = *Curschar;
		Topchar->index = 0;
		/* Scroll so cursor line is close to middle */
		if (nlines > Rows / 3)
		{
			i = 0;
			p = Topchar;
			while (i < Rows / 3)
			{
				p = (LPTR *)prevline(p);
				if (p == NULL)
					break;
				*Topchar = *p;
				i = i + 1;
			}
		}
		else
		{
			s_ins(0, nlines - 1);
		}
		updatescreen();
		handled = 1;
	}
	if (handled == 0 && LINEOF(Curschar) >= LINEOF(Botchar))
	{
		nlines = cntllines(Botchar, Curschar);
		/* Cursor is below bottom of screen */
		if (nlines > Rows / 3)
		{
			p = Curschar;
			i = 0;
			twothirds = Rows * 2;
			twothirds = twothirds / 3;
			while (i < twothirds)
			{
				p = (LPTR *)prevline(p);
				if (p == NULL)
					break;
				i = i + 1;
			}
			if (p != NULL)
				*Topchar = *p;
		}
		else
		{
			scrollup(nlines);
		}
		updatescreen();
	}

	/* Calculate cursor row and column */
	Cursrow = 0;
	Curscol = 0;
	Cursvcol = 0;

	p = Topchar;
	while (p->linep != Curschar->linep)
	{
		Cursrow = Cursrow + plines(p);
		p = (LPTR *)nextline(p);
		if (p == NULL)
			break;
	}

	Cline_row = Cursrow;
	Cline_size = plines(p);

	i = 0;
	didinc = FALSE;
	while (i <= Curschar->index)
	{
		c = Curschar->linep->s[i];
		/* A tab gets expanded depending on current column */
		if (c == TAB && !P(P_LS))
			inc = P(P_TS) - (Curscol % P(P_TS));
		else
			inc = chars[(c & 0xff)].ch_size;
		Curscol = Curscol + inc;
		Cursvcol = Cursvcol + inc;
		if (Curscol >= Columns)
		{
			Curscol = Curscol - Columns;
			Cursrow = Cursrow + 1;
			didinc = TRUE;
		}
		else
		{
			didinc = FALSE;
		}
		i = i + 1;
	}

	if (didinc)
		Cursrow = Cursrow - 1;

	if (c == TAB && State == NORMAL && !P(P_LS))
	{
		Curscol = Curscol - 1;
		Cursvcol = Cursvcol - 1;
	}

	if (set_want_col)
	{
		Curswant = Cursvcol;
		set_want_col = FALSE;
	}
}

/*
 * oneright() - move cursor one position to the right
 *
 * Returns TRUE if successful, FALSE if at end of line.
 */
int oneright(void)
{
	char *p;

	p = Curschar->linep->s + Curschar->index;
	if (*p == NUL || *(p + 1) == NUL)
		return FALSE;

	Curschar->index = Curschar->index + 1;
	set_want_col = TRUE;
	return TRUE;
}

/*
 * oneleft() - move cursor one position to the left
 *
 * Returns TRUE if successful, FALSE if at beginning of line.
 */
int oneleft(void)
{
	if (Curschar->index == 0)
		return FALSE;

	Curschar->index = Curschar->index - 1;
	set_want_col = TRUE;
	return TRUE;
}

/*
 * oneup(n) - move cursor 'n' lines up
 *
 * Returns TRUE if successful, FALSE if at top of file.
 */
int oneup(int n)
{
	LPTR *p;
	int k;

	p = Curschar;
	k = 0;
	while (k < n)
	{
		p = (LPTR *)prevline(p);
		if (p == NULL)
			break;
		k = k + 1;
	}

	if (k > 0)
	{
		*Curschar = *p;
		/* Try to preserve column position */
		Curschar->index = 0;
		Curschar = (LPTR *)coladvance(Curschar, Curswant);
		cursupdate();
		return TRUE;
	}
	return FALSE;
}

/*
 * onedown(n) - move cursor 'n' lines down
 *
 * Returns TRUE if successful, FALSE if at bottom of file.
 */
int onedown(int n)
{
	LPTR *p;
	int k;

	p = Curschar;
	k = 0;
	while (k < n)
	{
		p = (LPTR *)nextline(p);
		if (p == NULL)
			break;
		k = k + 1;
	}

	if (k > 0)
	{
		*Curschar = *p;
		/* Try to preserve column position */
		Curschar->index = 0;
		Curschar = (LPTR *)coladvance(Curschar, Curswant);
		cursupdate();
		return TRUE;
	}
	return FALSE;
}
