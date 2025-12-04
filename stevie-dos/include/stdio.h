/* stdio.h - Minimal stdio for MiniC/DOS */
#ifndef _STDIO_H
#define _STDIO_H

typedef int FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* MiniC allows undeclared function calls - minimal declarations only */
#define EOF (-1)
#define NULL 0

#endif
