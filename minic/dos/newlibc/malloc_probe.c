/*
 * malloc_probe.c -- libstub-free heap probe (§7o, Phase-6 libstub retirement).
 *
 * Exercises malloc / free (dos_libc.c) routed through newlibc's _sbrk
 * (libgloss/syscalls.c) + the BSS heap (minic/dos/heap.asm) -- the whole heap
 * chain compiled/assembled by THIS toolchain with NO libstub linked.  Built
 * --no-libstub by tools/build-newlibc-test.sh and gated in test-dos.sh.
 *
 * Deterministic, fixed-text output (no addresses printed): the assertions are
 * functional (no-clobber overlap, free-list reuse keeps live blocks intact,
 * heap-exhaustion returns NULL cleanly through _sbrk's __heap_end bound,
 * strings survive a round trip), so a broken _sbrk / heap symbol / free-list
 * diffs the golden loudly.  main() is renamed newlibc_test_main by the build;
 * dos_shim's main runs vfs_init() first so printf reaches INT 21h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBLK 8
#define BLKSZ 64

int main(void)
{
	char *p[NBLK];
	int i, j, ok;

	/* Allocate NBLK distinct blocks, stamp each with a unique byte. */
	for (i = 0; i < NBLK; i++) {
		p[i] = malloc(BLKSZ);
		if (p[i] == NULL) {
			printf("FAIL: malloc #%d returned NULL\n", i);
			return 1;
		}
		memset(p[i], 'A' + i, BLKSZ);
	}

	/* No block may overlap another (a later alloc clobbering an earlier). */
	ok = 1;
	for (i = 0; i < NBLK; i++)
		for (j = 0; j < BLKSZ; j++)
			if (p[i][j] != (char)('A' + i))
				ok = 0;
	printf("noclobber %s\n", ok ? "ok" : "FAIL");

	/* Free the even blocks, reallocate + restamp them; the odd (still-live)
	 * blocks must be untouched by the free-list churn. */
	for (i = 0; i < NBLK; i += 2)
		free(p[i]);
	for (i = 0; i < NBLK; i += 2) {
		p[i] = malloc(BLKSZ);
		if (p[i] == NULL) {
			printf("FAIL: realloc #%d returned NULL\n", i);
			return 1;
		}
		memset(p[i], 'a' + i, BLKSZ);
	}
	ok = 1;
	for (i = 1; i < NBLK; i += 2)
		for (j = 0; j < BLKSZ; j++)
			if (p[i][j] != (char)('A' + i))
				ok = 0;
	printf("liveintact %s\n", ok ? "ok" : "FAIL");

	for (i = 0; i < NBLK; i++)
		free(p[i]);

	/* A request larger than the whole heap must fail cleanly (the _sbrk
	 * __heap_end bound), not wrap or corrupt. */
	printf("exhaust %s\n", (malloc(60000) == NULL) ? "ok" : "FAIL");

	/* malloc must recover after the failed over-large request. */
	{
		char *s = malloc(32);
		if (s == NULL) {
			printf("FAIL: malloc(32) after exhaust returned NULL\n");
			return 1;
		}
		strcpy(s, "victor 9000");
		printf("string %s\n", s);
		free(s);
	}

	printf("malloc_probe done\n");
	return 0;
}
