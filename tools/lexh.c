/*% c99 -O3 -Wall -o # %
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

/* This list must exactly mirror the runtime kwmap in parse.c: every
 * PUBLIC op name from ops.h (everything before the INTERNAL OPERATIONS
 * marker) plus parse.c's explicit kwmap alias entries.  A stale list
 * here finds a K that lexinit()'s perfect-hash assert then rejects. */
char *tok[] = {

	/* public ops (ops.h order) */
	"add", "sub", "neg", "div", "rem", "udiv", "urem", "mul",
	"and", "or", "xor", "sar", "shr", "shl",
	"ceqw", "cnew", "csgew", "csgtw", "cslew", "csltw",
	"cugew", "cugtw", "culew", "cultw",
	"ceql", "cnel", "csgel", "csgtl", "cslel", "csltl",
	"cugel", "cugtl", "culel", "cultl",
	"ceqs", "cges", "cgts", "cles", "clts", "cnes", "cos", "cuos",
	"ceqd", "cged", "cgtd", "cled", "cltd", "cned", "cod", "cuod",
	"storeb", "storeh", "storew", "storel", "stores", "stored",
	"loadsb", "loadub", "loadsh", "loaduh", "loadsw", "loaduw",
	"load",
	"extsb", "extub", "extsh", "extuh", "extsw", "extuw",
	"exts", "truncd",
	"stosi", "stoui", "dtosi", "dtoui",
	"swtof", "uwtof", "sltof", "ultof",
	"cast",
	"alloc4", "alloc8", "alloc16",
	"vaarg", "vastart",
	"copy", "dbgloc", "asm",
	"loadfb", "loadfh", "loadfw", "loadfl", "loadfs",
	"storefb", "storefh", "storefw", "storefl", "storefs",
	"mkfar", "farseg", "faroff", "addfo", "subfo", "vargp",

	/* parse.c kwmap aliases */
	"loadw", "loadl", "loads", "loadd", "alloc1", "alloc2",
	"blit", "call", "env", "phi", "jmp", "jnz", "ret", "hlt",
	"export", "thread", "extern", "common", "interrupt",
	"function", "type", "data", "section", "align", "dbgfile",
	"sb", "ub", "sh", "uh", "b", "h", "w", "l", "s", "d", "z",
	"volatile", "...",

};
enum {
	Ntok = sizeof tok / sizeof tok[0]
};

uint32_t th[Ntok];

uint32_t
hash(char *s)
{
	uint32_t h;

	h = 0;
	for (; *s; ++s)
		h = *s + 17*h;
	return h;
}

int
main()
{
	char *bmap;
	uint32_t h, M, K;
	int i, j;

	bmap = malloc(1u << 31);

	for (i=0; i<Ntok; ++i) {
		h = hash(tok[i]);
		for (j=0; j<i; ++j)
			if (th[j] == h) {
				printf("error: hash()\n");
				printf("\t%s\n", tok[i]);
				printf("\t%s\n", tok[j]);
				exit(1);
			}
		th[i] = h;
	}

	for (i=9; 1<<i < Ntok; ++i);
	M = 32 - i;

	for (;; --M) {
		printf("trying M=%d...\n", M);
		K = 1;
		do {
			memset(bmap, 0, 1 << (32 - M));
			for (i=0; i<Ntok; ++i) {
				h = (th[i]*K) >> M;
				if (bmap[h])
					break;
				bmap[h] = 1;
			}
			if (i==Ntok) {
				printf("found K=%d for M=%d\n", K, M);
				exit(0);
			}
			K += 2;
		} while (K != 1);
	}
}
