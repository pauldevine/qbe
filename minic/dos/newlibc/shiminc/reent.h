/* reent.h - triage shim for the §6a newlibc sweep.
 * Just enough of newlib's reentrancy surface for libgloss/*.c:
 * board_init.c wires _impure_ptr->_stdin/_stdout/_stderr to the
 * __sf_fake_* placeholder objects. */
#ifndef _REENT_H
#define _REENT_H

#include <stdio.h>

struct _reent {
    int _errno;
    FILE *_stdin;
    FILE *_stdout;
    FILE *_stderr;
};

struct __sFILE_fake {
    int _file;
};

extern struct _reent *_impure_ptr;
#define _REENT _impure_ptr

#endif /* _REENT_H */
