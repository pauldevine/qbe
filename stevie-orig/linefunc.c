/* $Header: /nw/tony/src/stevie/src/RCS/linefunc.c,v 1.2 89/03/11 22:42:32 tony Exp $
 *
 * Basic line-oriented motions.
 */

#include "stevie.h"
#include "ops.h"

/* MiniC has a known bug: `static` for function-local variables is treated
 * as auto stack allocation, so returning the address of a function-local
 * static dangles and gets clobbered by subsequent function calls.  Promote
 * these to file-scope statics so the addresses returned by nextline /
 * prevline / coladvance persist across calls.
 *
 * Symptom that drove this: cursupdate's `for (p=Topchar; p->linep !=
 * Curschar->linep; p = nextline(p))` walked the line list ELEVEN times
 * before our break, because each iteration's returned &next pointed to a
 * stack slot that plines() overwrote — comparison cycled forever instead
 * of terminating at Curschar's line. */
static	LPTR	_nl_next;
static	LPTR	_pl_prev;
static	LPTR	_ca_lp;

/*
 * nextline(curr)
 *
 * Return a pointer to the beginning of the next line after the one
 * referenced by 'curr'. Return NULL if there is no next line (at EOF).
 */

LPTR *
nextline(curr)
LPTR	*curr;
{
	if (curr->linep->next != Fileend->linep) {
		_nl_next.index = 0;
		_nl_next.linep = curr->linep->next;
		return &_nl_next;
	}
	return (LPTR *) NULL;
}

/*
 * prevline(curr)
 *
 * Return a pointer to the beginning of the line before the one
 * referenced by 'curr'. Return NULL if there is no prior line.
 */

LPTR *
prevline(curr)
LPTR	*curr;
{
	if (curr->linep->prev != Filetop->linep) {
		_pl_prev.index = 0;
		_pl_prev.linep = curr->linep->prev;
		return &_pl_prev;
	}
	return (LPTR *) NULL;
}

/*
 * coladvance(p,col)
 *
 * Try to advance to the specified column, starting at p.
 */

LPTR *
coladvance(p, col)
LPTR	*p;
register int	col;
{
	register int	c, in;

	_ca_lp.linep = p->linep;
	_ca_lp.index = p->index;

	/* If we're on a blank ('\n' only) line, we can't do anything */
	if (_ca_lp.linep->s[_ca_lp.index] == '\0')
		return &_ca_lp;
	/* try to advance to the specified column */
	for ( c=0; col-- > 0; c++ ) {
		/* Count a tab for what it's worth (if list mode not on) */
		if ( gchar(&_ca_lp) == TAB && !P(P_LS) ) {
			in = ((P(P_TS)-1) - c%P(P_TS));
			col -= in;
			c += in;
		}
		/* Don't go past the end of */
		/* the file or the line. */
		if (inc(&_ca_lp)) {
			dec(&_ca_lp);
			break;
		}
	}
	return &_ca_lp;
}


/*
 * nextchar(curr)
 *
 * Return a line pointer to the next character after the
 * one referenced by 'curr'. Return NULL if there is no next one (at EOF).
 * NOTE: this COULD point to a \n or \0 character.
 */

LPTR *
nextchar(curr)
LPTR	*curr;
{
	static	LPTR	*next;
	char	c;

	next = curr;
	c = CHAR( next );
	if (c=='\n' || c=='\0')		/* end of line */
		next = nextline (next);
	else
		next->index++;

	return (next);
}


/*
 * prevchar(curr)
 *
 * Return a line pointer to the previous character before the
 * one referenced by 'curr'. Return NULL if there is no previous one.
 * Note: this COULD point to a \n or \0 character.
 */

LPTR *
prevchar(curr)
LPTR	*curr;
{
	static	LPTR	*prev;
	char	c;

	prev = curr;
	if (prev->index == 0) {		/* beginning of line */
		prev = prevline (prev);		/* jump back */
		c = CHAR( prev );
		while (c!='\n' && c!= '\0') {	/* go to end of line */
			prev->index++;
			c = CHAR( prev );
		}
	}
	else
		prev->index--;

	return (prev);
}


