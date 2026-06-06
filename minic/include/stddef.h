/* stddef.h - Minimal stddef.h for MiniC */

#ifndef _STDDEF_H
#define _STDDEF_H

/* On i8086 small/medium model, size_t and ptrdiff_t are 16-bit
 * (unsigned int / int).  Using long here would force 32-bit pair ops
 * for every string-length / sizeof-driven expression. */
typedef unsigned int size_t;
typedef int ptrdiff_t;

/* MiniC's grammar doesn't yet accept `((void *)0)` at file scope as a
 * pointer initializer; both forms are valid null pointer constants in
 * standard C, so use the simpler one. */
#define NULL 0

/* offsetof: byte offset of a member within a struct/union type.  The
 * classic null-pointer form folds to the member's offset; MiniC computes
 * member offsets, so `&((type *)0)->member` yields the offset directly. */
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif /* _STDDEF_H */
