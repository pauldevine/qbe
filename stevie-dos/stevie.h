/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#define DOS		1	/* For DOS/8086 target */

#define HELP

#define FILELENG 64000
#define NORMAL 0
#define CMDLINE 1
#define INSERT 2
#define APPEND 3
#define FORWARD 4
#define BACKWARD 5
#define WORDSEP " \t\n()[]{},;:'\"-="
#define SLOP 512

#define TRUE 1
#define FALSE 0
#define LINEINC 1

#define CHANGED Changed=1
#define UNCHANGED Changed=0

#define LINEOF(x) x->linep->num

#ifndef NULL
#define NULL 0
#endif

#include "param.h"
#include "ascii.h"
#include "term.h"
#include "keymap.h"

/*
 * Minimal C runtime declarations for DOS/MiniC target
 * These replace <stdio.h>, <stdlib.h>, <string.h>
 * Actual implementations linked from runtime library
 */
typedef int FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* stdio functions - these are runtime library calls */
/* fprintf, sprintf, fopen, fclose, fread, fwrite are implicitly declared */

/* Note: MiniC uses implicit declaration for library functions */

typedef int bool_t;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned int ULONG;
typedef short LONG;
typedef unsigned char UBYTE;
typedef char BYTE;

struct charinfo {
	char ch_size;
	char *ch_str;
};

struct line {
	struct line *prev;
	struct line *next;
	char *s;
	int size;
	unsigned int num;
};

struct lptr {
	struct line *linep;
	int index;
};

typedef struct line LINE;
typedef struct lptr LPTR;

/*
 * MiniC parser limitation: Too many extern declarations cause parse errors
 * when followed by multi-line function definitions. Keeping only essential
 * declarations. MiniC allows use of undeclared globals.
 */
extern struct charinfo chars[];
extern int State;
extern int Rows;
extern int Columns;
extern char *Realscreen;
extern char *Nextscreen;
extern struct lptr *Filemem;
extern struct lptr *Fileend;
extern struct lptr *Topchar;
extern struct lptr *Botchar;
extern struct lptr *Curschar;
extern int Cursrow;
extern int Curscol;
extern int Cursvcol;
extern int Curswant;
extern int set_want_col;
extern int Prenum;
extern struct lptr *Insstart;
extern int Changed;
extern char *Redobuff;
extern char *Undobuff;
extern char *Insbuff;
extern struct lptr *Uncurschar;
extern int Ninsert;
extern int Undelchars;
extern char *Insptr;
extern char *Filename;

/*
 * Note: MiniC does not support function prototypes with pointer return types
 * or ANSI-style parameters. Functions are called without prior declaration.
 * Function definitions are in their respective source files:
 *   alloc.c, linefunc.c, mark.c, misccmds.c, search.c, screen.c, dos.c
 */
