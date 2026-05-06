/*
 * Definitions etc. for regexp(3) routines.
 *
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */
#define NSUBEXP  10
/* MiniC-compatible: arrays-in-struct converted to pointers (allocated by regcomp) */
typedef struct regexp {
	char **startp;		/* array of NSUBEXP char* */
	char **endp;		/* array of NSUBEXP char* */
	char regstart;		/* Internal use only. */
	char reganch;		/* Internal use only. */
	char *regmust;		/* Internal use only. */
	int regmlen;		/* Internal use only. */
	char *program;		/* dynamically allocated */
} regexp;

regexp *regcomp(char *exp);
int regexec(regexp *prog, char *string);
void regsub(regexp *prog, char *source, char *dest);
