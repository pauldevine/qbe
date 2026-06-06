/* stdlib.h - Minimal stdlib.h stub for MiniC
 * This is a stub - actual functions must be provided by the runtime
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

extern void *malloc();
extern void *calloc();
extern void *realloc();
extern void free();

extern void exit();
extern void abort();
extern int system();
extern char *getenv();

extern int abs();
extern int atoi();
extern long atol();

#endif /* _STDLIB_H */
