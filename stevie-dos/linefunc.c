/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * Savaged to compile under modern gcc and improved (haha) by: George Nakos  ggn@atari.org
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * nextline(curr)
 *
 * Return a pointer to the beginning of the next line after the one
 * referenced by 'curr'. Return NULL if there is no next line (at EOF).
 */

LPTR *nextline(LPTR *curr)
{
    static LPTR next;

    if (curr->linep->next != Fileend->linep)
    {
        next.index = 0;
        next.linep = curr->linep->next;
        return &next;
    }
    return (LPTR *) NULL;
}

/*
 * prevline(curr)
 *
 * Return a pointer to the beginning of the line before the one
 * referenced by 'curr'. Return NULL if there is no prior line.
 */

LPTR *prevline(LPTR *curr)
{
    static LPTR prev;

    if (curr->linep->prev != (LINE *)NULL)
    {
        prev.index = 0;
        prev.linep = curr->linep->prev;
        return &prev;
    }
    return (LPTR *) NULL;
}

/*
 * coladvance(p,col)
 *
 * Try to advance to the specified column, starting at p.
 */

LPTR *coladvance(LPTR *p, int col)
{
    static LPTR lp;
    int c;
    int in;

    lp.linep = p->linep;
    lp.index = p->index;

    /* If we're on a blank ('\n' only) line, we can't do anything */
    if (lp.linep->s[lp.index] == '\0')
        return &lp;
    /* try to advance to the specified column */
    c = 0;
    while (col > 0) {
        col = col - 1;
        /* Count a tab for what it's worth (if list mode not on) */
        if (gchar(&lp) == TAB && !P(P_LS))
        {
            in = ((P(P_TS) - 1) - c % P(P_TS));
            col = col - in;
            c = c + in;
        }
        /* Don't go past the end of */
        /* the file or the line. */
        if (inc(&lp))
        {
            dec(&lp);
            break;
        }
        c = c + 1;
    }
    return &lp;
}
