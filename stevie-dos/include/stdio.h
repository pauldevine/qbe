/* stdio.h - Minimal stdio for MiniC/DOS */
#ifndef _STDIO_H
#define _STDIO_H

/* FILE is an opaque type - use int for simplicity */
typedef int FILE;

/* Standard streams - declared as extern */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* All functions declared without parameters for MiniC compatibility */
/* MiniC will still allow calls with any arguments */
/* Pointer returns must be cast at call site */
int printf();
int fprintf();
int sprintf();
int fopen();
int fclose();
int fgetc();
int fputc();
int fgets();
int fputs();
int fread();
int fwrite();
int fseek();
long ftell();
int feof();
int ferror();
int getchar();
int putchar();
int puts();
int getc();
int putc();
int ungetc();

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define NULL 0

#endif
