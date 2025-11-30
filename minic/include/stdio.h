/* stdio.h - Minimal stdio.h stub for MiniC
 * This is a stub - actual I/O functions must be provided by the runtime
 */

#ifndef _STDIO_H
#define _STDIO_H

typedef unsigned long size_t;

/* Function declarations - implementations in runtime library */
/* Note: MiniC doesn't support const or variadic functions (...) */
int printf(char *format);
int sprintf(char *str, char *format);
int putchar(int c);
int puts(char *s);

#endif /* _STDIO_H */
