/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * The routines in this file attempt to imitate many of the operations
 * that used to be performed on simple character pointers and are now
 * performed on LPTR's. This makes it easier to modify other sections
 * of the code. Think of an LPTR as representing a position in the file.
 * Positions can be incremented, decremented, compared, etc. through
 * the functions implemented here.
 */

/*
 * dec(lp)
 *
 * Decrement the line pointer 'lp' crossing line boundaries as necessary.
 * Return 1 when crossing a line, -1 when at start of file, 0 otherwise.
 */
int dec(LPTR *lp)
{
	if (lp->index > 0)			/* still within line */
	{
		lp->index = lp->index - 1;
		return 0;
	}

	if (lp->linep->prev != NULL)		/* there is a prior line */
	{
		lp->linep = lp->linep->prev;
		lp->index = strlen(lp->linep->s);
		return 1;
	}

	return -1;				/* at start of file */
}


/*
 * pchar(lp, c) - put character 'c' at position 'lp'
 */
void pchar(LPTR *lp, char c)
{
	lp->linep->s[lp->index] = c;
}

/*
 * pswap(a, b) - swap two position pointers
 *
 * MiniC doesn't support local struct variables, so we use
 * a static global for the temporary.
 */
LPTR *pswap_tmp;

void pswap(LPTR *a, LPTR *b)
{
	/* Allocate temp on first use */
	if (pswap_tmp == NULL)
		pswap_tmp = (LPTR *)alloc(16);

	*pswap_tmp = *a;
	*a = *b;
	*b = *pswap_tmp;
}

/*
 * Position comparisons
 */

bool_t lt(LPTR *a, LPTR *b)
{
	int an;
	int bn;

	an = LINEOF(a);
	bn = LINEOF(b);

	if (an != bn)
		return an < bn;
	else
		return a->index < b->index;
}

bool_t gt(LPTR *a, LPTR *b)
{
	int an;
	int bn;

	an = LINEOF(a);
	bn = LINEOF(b);

	if (an != bn)
		return an > bn;
	else
		return a->index > b->index;
}

bool_t equal(LPTR *a, LPTR *b)
{
	return a->linep == b->linep && a->index == b->index;
}

bool_t ltoreq(LPTR *a, LPTR *b)
{
	return lt(a, b) || equal(a, b);
}

bool_t gtoreq(LPTR *a, LPTR *b)
{
	return gt(a, b) || equal(a, b);
}
