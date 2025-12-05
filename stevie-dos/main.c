/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

int Rows;
int Columns;

char *Realscreen;
char *Nextscreen;

char *Filename;

LPTR *Filemem;
LPTR *Fileend;
LPTR *Topchar;
LPTR *Botchar;
LPTR *Curschar;

int Cursrow;
int Curscol;
int Cursvcol;
int Curswant;

bool_t set_want_col;

int State;
int Prenum;

LPTR *Insstart;

bool_t Changed;
bool_t Debug;

char *Redobuff;
char *Undobuff;
char *Insbuff;

LPTR *Uncurschar;

int Ninsert;
int Undelchars;
char *Insptr;

char **files;
int  numfiles;
int  curfile;

#define RBSIZE 1280
char *getcbuff;
char *getcnext;

char *arg_tag;
char *arg_pat;
int arg_line;

void usage(void)
{
	fprintf(stderr, "usage: stevie [file ...]\n");
	fprintf(stderr, "       stevie -t tag\n");
	fprintf(stderr, "       stevie +[num] file\n");
	fprintf(stderr, "       stevie +/pat  file\n");
	exit(1);
}

void init_main(void)
{
	Realscreen = (char *)0;
	Nextscreen = (char *)0;
	Filename = (char *)0;
	Curswant = 0;
	State = NORMAL;
	Prenum = 0;
	Changed = 0;
	Debug = 0;
	Ninsert = 0;
	Undelchars = 0;
	Insptr = (char *)0;
	Redobuff = (char *)alloc(1024);
	Undobuff = (char *)alloc(1024);
	Insbuff = (char *)alloc(1024);
	getcbuff = (char *)alloc(RBSIZE);
	getcnext = (char *)0;
	arg_tag = (char *)0;
	arg_pat = (char *)0;
	arg_line = -1;
}

int parse_dash_arg(int argc, char **argv)
{
	char c1;
	c1 = argv[1][1];
	if (c1 != 116) return 0;
	if (argv[2] == (char *)0) usage();
	Filename = (char *)0;
	arg_tag = argv[2];
	numfiles = 1;
	return 1;
}

int parse_plus_slash(int argc, char **argv)
{
	char *p;
	int tmp;
	if (argv[2] == (char *)0) usage();
	tmp = strsave(argv[2]);
	Filename = (char *)tmp;
	p = argv[1];
	p = p + 1;
	arg_pat = p;
	numfiles = 1;
	return 1;
}

int parse_plus_num(int argc, char **argv, int c1)
{
	int isdig;
	char *p;
	int tmp;
	isdig = 0;
	if (c1 >= 48 && c1 <= 57) isdig = 1;
	if (isdig == 0 && c1 != NUL) return 0;
	if (argv[2] == (char *)0) usage();
	tmp = strsave(argv[2]);
	Filename = (char *)tmp;
	numfiles = 1;
	arg_line = 0;
	p = argv[1];
	p = p + 1;
	if (isdig) arg_line = atoi(p);
	return 1;
}

int parse_plus_arg(int argc, char **argv)
{
	return 1;
}

void parse_args(int argc, char **argv)
{
	Filename = (char *)0;
	numfiles = 1;
}

void alloc_ptrs(void)
{
}

void handle_exinit(void)
{
}

void handle_initial_cmd(void)
{
}

int main(int argc, char **argv)
{
	return 0;
}

void stuffin(char *s)
{
}

void stuffnum(int n)
{
}

void addtobuff(char *s, char c1, char c2, char c3, char c4, char c5, char c6)
{
}

int vgetc(void)
{
	return 0;
}

int vpeekc(void)
{
	return -1;
}

bool_t anyinput(void)
{
	return 0;
}
