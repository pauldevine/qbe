/* stddef.h - Minimal stddef.h for MiniC */

#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long size_t;
typedef long ptrdiff_t;

/* MiniC's grammar doesn't yet accept `((void *)0)` at file scope as a
 * pointer initializer; both forms are valid null pointer constants in
 * standard C, so use the simpler one. */
#define NULL 0

#endif /* _STDDEF_H */
