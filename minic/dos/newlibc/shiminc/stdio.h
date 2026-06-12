/* stdio.h - newlib-shaped triage shim for the §6a newlibc sweep.
 * Shadows minic/include/stdio.h: same call surface (libstub provides the
 * engines for the test TUs), but FILE is a struct with the _file member
 * that newlibc's own printf_wrappers.c / board_init.c poke. */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>

#define __restrict

struct __sFILE {
    int _file;
};
typedef struct __sFILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

extern int printf();
extern int fprintf();
extern int sprintf();
extern int snprintf();
extern int vsnprintf();
extern int vfprintf();
extern int vprintf();
extern int sscanf();
extern int scanf();
extern int fscanf();
extern int putchar();
extern int puts();
extern int fputs();
extern int fputc();
extern int fgetc();
extern int getchar();
extern char *fgets();
extern int fflush();

extern FILE *fopen();
extern int fclose();
extern int fread();
extern int fwrite();
extern int fseek();
extern long ftell();

#endif /* _STDIO_H */
