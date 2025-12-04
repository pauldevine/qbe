/* stdlib.h - Minimal stdlib for MiniC/DOS */
#ifndef _STDLIB_H
#define _STDLIB_H

#ifndef NULL
#define NULL 0
#endif

/* Functions returning pointers declared as int - cast at call site */
int malloc();
int calloc();
int realloc();
int free();
int exit();
int atoi();
long atol();
int getenv();
int abs();
long labs();

#endif
