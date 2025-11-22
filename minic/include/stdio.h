/* stdio.h - Minimal stdio.h stub for MiniC
 * This is a stub - actual I/O functions must be provided by the runtime
 */

#ifndef _STDIO_H
#define _STDIO_H

typedef unsigned long size_t;

/* Function declarations - implementations in runtime library */
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int putchar(int c);
int puts(const char *s);

#endif /* _STDIO_H */
