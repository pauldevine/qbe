/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * This flag is used to make auto-indent work right on lines where only
 * a <RETURN> or <ESC> is typed. It is set when an auto-indent is done,
 * and reset when any other editting is done on the line. If an <ESC>
 * or <RETURN> is received, and did_ai is TRUE, the line is truncated.
 */
bool_t did_ai;

/*
 * MiniC doesn't support local struct variables, use globals
 */
LPTR *edit_p_tmp;
LPTR *edit_np;

/*
 * Special characters in this context are those that need processing other
 * than the simple insertion that can be performed here. This includes ESC
 * which terminates the insert, and CR/NL which need special processing to
 * open up a new line. This routine tries to optimize insertions performed
 * by the "redo" command, so it needs to know when it should stop and defer
 * processing to the "normal" mechanism.
 */
int isspecial(int c)
{
	return c == NL || c == CR || c == ESC;
}

void insertchar(int c)
{
	char *p;

	if (!anyinput())
	{
		inschar(c);
		*Insptr = c;
		Insptr = Insptr + 1;
		Ninsert = Ninsert + 1;
		/*
		 * The following kludge avoids overflowing the statically
		 * allocated insert buffer. Just dump the user back into
		 * command mode, and print a message.
		 */
		if (Insptr + 10 >= &Insbuff[1024])
		{
			stuffin((char *)mkstr(ESC));
			emsg("No buffer space - returning to command mode");
			sleep(2);
		}
	}
	else
	{
		/* If there's any pending input, grab it all at once. */
		p = Insptr;
		*Insptr = c;
		Insptr = Insptr + 1;
		Ninsert = Ninsert + 1;
		c = vpeekc();
		while (!isspecial(c))
		{
			c = vgetc();
			*Insptr = c;
			Insptr = Insptr + 1;
			Ninsert = Ninsert + 1;
			c = vpeekc();
		}
		*Insptr = 0;
		insstr(p);
	}
	updateline();
}

void edit(void)
{
	int c;
	char *p;
	char *q;

	did_ai = FALSE;
	Prenum = 0;

	/* position the display and the cursor at the top of the file. */
	*Topchar = *Filemem;
	*Curschar = *Filemem;
	Cursrow = 0;
	Curscol = 0;

	while (1) {

	/* Figure out where the cursor is based on Curschar. */
	cursupdate();

	windgoto(Cursrow, Curscol);

	c = vgetc();

	if (State == NORMAL) {

		/* We're in the normal (non-insert) mode. */

		/* Pick up any leading digits and compute 'Prenum' */
		if ((Prenum > 0 && isdigit(c)) || (isdigit(c) && c != '0')) {
			Prenum = Prenum * 10 + (c - '0');
			continue;
		}
		/* execute the command */
		normal(c);
		Prenum = 0;

	} else {

		switch (c) {	/* We're in insert mode */

		case ESC:	/* an escape ends input mode */

			set_want_col = TRUE;

			/* Don't end up on a '\n' if you can help it. */
			if (gchar(Curschar) == NUL && Curschar->index != 0)
				dec(Curschar);

			/*
			 * The cursor should end up on the last inserted
			 * character. This is an attempt to match the real
			 * 'vi', but it may not be quite right yet.
			 */
			if (Curschar->index != 0 && !endofline(Curschar))
				dec(Curschar);

			State = NORMAL;
			msg("");
			*Uncurschar = *Insstart;
			Undelchars = Ninsert;
			/* Undobuff[0] = '\0'; */
			/* construct the Redo buffer */
			p = Redobuff;
			q = Insbuff;
			while ((int)q < (int)Insptr) {
				*p = *q;
				p = p + 1;
				q = q + 1;
			}
			*p = ESC;
			p = p + 1;
			*p = NUL;
			updatescreen();
			break;

		case 4:	/* CTRL('D') */
			/*
			 * Control-D is treated as a backspace in insert
			 * mode to make auto-indent easier. This isn't
			 * completely compatible with vi, but it's a lot
			 * easier than doing it exactly right, and the
			 * difference isn't very noticeable.
			 */
		case BS:
			/* can't backup past starting point */
			if (Curschar->linep == Insstart->linep &&
			    Curschar->index <= Insstart->index) {
				beep();
				break;
			}

			/* can't backup to a previous line */
			if (Curschar->linep != Insstart->linep &&
			    Curschar->index <= 0) {
				beep();
				break;
			}

			did_ai = FALSE;
			dec(Curschar);
			delchar(TRUE);
			Insptr = Insptr - 1;
			Ninsert = Ninsert - 1;
			cursupdate();
			updateline();
			break;

		case CR:
		case NL:
			*Insptr = NL;
			Insptr = Insptr + 1;
			Ninsert = Ninsert + 1;
			opencmd(FORWARD, TRUE);		/* open a new line */
			cursupdate();
			updatescreen();
			break;

		default:
			did_ai = FALSE;
			insertchar(c);
			break;
		}
	}
	}
}

void getout(void)
{
	windgoto(Rows - 1, 0);
	putchar(13);
	putchar(10);
	windexit(0);
}

void scrolldown(int nlines)
{
	LPTR *p;
	LPTR *tmp;
	int done;	/* total # of physical lines done */

	done = 0;

	/* Scroll up 'nlines' lines. */
	while (nlines > 0)
	{
		nlines = nlines - 1;
		p = (LPTR *)prevline(Topchar);
		if (p == NULL)
			break;
		done = done + plines(p);
		*Topchar = *p;
		if (Curschar->linep == Botchar->linep->prev)
		{
			tmp = (LPTR *)prevline(Curschar);
			*Curschar = *tmp;
		}
	}
	s_ins(0, done);
}

void scrollup(int nlines)
{
	LPTR *p;
	int done;	/* total # of physical lines done */
	int pl;		/* # of plines for the current line */

	done = 0;

	/* Scroll down 'nlines' lines. */
	while (nlines > 0)
	{
		nlines = nlines - 1;
		pl = plines(Topchar);
		p = (LPTR *)nextline(Topchar);
		if (p == NULL)
			break;
		done = done + pl;
		if (Curschar->linep == Topchar->linep)
			*Curschar = *p;
		*Topchar = *p;
	}
	s_del(0, done);
}

/*
 * oneright
 * oneleft
 * onedown
 * oneup
 *
 * Move one char {right,left,down,up}.  Return TRUE when
 * sucessful, FALSE when we hit a boundary (of a line, or the file).
 */

bool_t oneright(void)
{
	int result;

	set_want_col = TRUE;

	result = inc(Curschar);
	if (result == 0)
		return TRUE;

	if (result == 1)
		dec(Curschar);		/* crossed a line, so back up */
	return FALSE;
}

bool_t oneleft(void)
{
	int result;

	set_want_col = TRUE;

	result = dec(Curschar);
	if (result == 0)
		return TRUE;

	if (result == 1)
		inc(Curschar);		/* crossed a line, so back up */
	return FALSE;
}

void beginline(bool_t flag)
{
	while (oneleft())
		;
	if (flag)
	{
		while (isspace(gchar(Curschar)) && oneright())
			;
	}
	set_want_col = TRUE;
}

bool_t oneup(int n)
{
	LPTR *np;
	int k;

	/* Allocate temp LPTR on first use */
	if (edit_p_tmp == NULL)
		edit_p_tmp = (LPTR *)alloc(16);

	*edit_p_tmp = *Curschar;
	k = 0;
	while (k < n)
	{
		/* Look for the previous line */
		np = (LPTR *)prevline(edit_p_tmp);
		if (np == NULL)
		{
			/* If we've at least backed up a little .. */
			if (k > 0)
				break;	/* to update the cursor, etc. */
			else
				return FALSE;
		}
		*edit_p_tmp = *np;
		k = k + 1;
	}
	*Curschar = *edit_p_tmp;
	/* This makes sure Topchar gets updated so the complete line */
	/* is one the screen. */
	cursupdate();
	/* try to advance to the column we want to be at */
	np = (LPTR *)coladvance(edit_p_tmp, Curswant);
	*Curschar = *np;
	return TRUE;
}

bool_t onedown(int n)
{
	LPTR *np;
	int k;

	/* Allocate temp LPTR on first use */
	if (edit_p_tmp == NULL)
		edit_p_tmp = (LPTR *)alloc(16);

	*edit_p_tmp = *Curschar;
	k = 0;
	while (k < n)
	{
		/* Look for the next line */
		np = (LPTR *)nextline(edit_p_tmp);
		if (np == NULL)
		{
			if (k > 0)
				break;
			else
				return FALSE;
		}
		*edit_p_tmp = *np;
		k = k + 1;
	}
	/* try to advance to the column we want to be at */
	np = (LPTR *)coladvance(edit_p_tmp, Curswant);
	*Curschar = *np;
	return TRUE;
}
