/* stdio.h - Minimal stdio.h stub for MiniC
 * This is a stub - actual I/O functions must be provided by the runtime
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
/* Stevie includes <stdio.h> in K&R style and expects exit, getenv,
 * malloc, etc. to be available without an explicit <stdlib.h>. */
#include <stdlib.h>

typedef int FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)

/* MiniC doesn't model varargs.  Declare the K&R-extern way (empty
 * parens) so call sites can pass any printf-style arguments. */
extern int printf();
extern int fprintf();
extern int sprintf();
extern int putchar();
extern int puts();
extern int fputs();
extern int fputc();
extern int fgetc();
extern int getchar();
extern char *fgets();

extern FILE *fopen();
extern int fclose();
extern int fread();
extern int fwrite();
extern int fseek();
extern long ftell();

#endif /* _STDIO_H */
