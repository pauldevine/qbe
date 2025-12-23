/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * This file contains routines to maintain and manipulate marks.
 */

#define	NMARKS	10		/* max. # of marks that can be saved */

/*
 * Mark structure: a named position in the file
 * Size: char (1) + padding (1-3) + LPTR.linep (4) + LPTR.index (4) = ~12 bytes
 */
struct mark
{
    char name;
    LPTR pos;
};

/*
 * MiniC doesn't support global struct variables/arrays, so we use
 * pointers and allocate dynamically. These are initialized in init_marks().
 */
struct mark *mlist;		/* array of NMARKS marks */
struct mark *pcmark;		/* previous context mark */
bool_t pcvalid;			/* true if pcmark is valid */

/*
 * init_marks() - initialize mark storage
 *
 * Must be called before any other mark functions.
 */
void init_marks(void)
{
    int i;

    /* Allocate mark array - each mark is ~16 bytes to be safe */
    mlist = (struct mark *)alloc(NMARKS * 16);
    pcmark = (struct mark *)alloc(16);
    pcvalid = FALSE;

    /* Clear all marks */
    i = 0;
    while (i < NMARKS) {
        mlist[i].name = NUL;
        i = i + 1;
    }
}

/*
 * setmark(c) - set mark 'c' at current cursor position
 *
 * Returns TRUE on success, FALSE if no room for mark or bad name given.
 */
bool_t setmark(char c)
{
    int i;

    if (!isalpha(c))
        return FALSE;

    /*
     * If there is already a mark of this name, then just use the
     * existing mark entry.
     */
    i = 0;
    while (i < NMARKS) {
        if (mlist[i].name == c)
        {
            mlist[i].pos = *Curschar;
            return TRUE;
        }
        i = i + 1;
    }

    /*
     * There wasn't a mark of the given name, so find a free slot
     */
    i = 0;
    while (i < NMARKS) {
        if (mlist[i].name == NUL)  	/* got a free one */
        {
            mlist[i].name = c;
            mlist[i].pos = *Curschar;
            return TRUE;
        }
        i = i + 1;
    }
    return FALSE;
}

/*
 * setpcmark() - set the previous context mark to the current position
 */
void setpcmark(void)
{
    pcmark->pos = *Curschar;
    pcvalid = TRUE;
}

/*
 * getmark(c) - find mark for char 'c'
 *
 * Return pointer to LPTR or NULL if no such mark.
 */
LPTR *getmark(char c)
{
    int i;

    if (c == 39 || c == 96)	/* previous context mark: ' or ` */
        return pcvalid ? &(pcmark->pos) : (LPTR *) NULL;

    i = 0;
    while (i < NMARKS) {
        if (mlist[i].name == c)
            return &(mlist[i].pos);
        i = i + 1;
    }
    return (LPTR *) NULL;
}

/*
 * clrall() - clear all marks
 *
 * Used mainly when trashing the entire buffer during ":e" type commands
 */
void clrall(void)
{
    int i;

    i = 0;
    while (i < NMARKS) {
        mlist[i].name = NUL;
        i = i + 1;
    }
    pcvalid = FALSE;
}

/*
 * clrmark(line) - clear any marks for 'line'
 *
 * Used any time a line is deleted so we don't have marks pointing to
 * non-existent lines.
 */
void clrmark(LINE *line)
{
    int i;

    i = 0;
    while (i < NMARKS) {
        if (mlist[i].pos.linep == line)
            mlist[i].name = NUL;
        i = i + 1;
    }
    if (pcvalid && (pcmark->pos.linep == line))
        pcvalid = FALSE;
}
