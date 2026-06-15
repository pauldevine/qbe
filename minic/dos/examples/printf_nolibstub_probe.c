/*
 * printf_nolibstub_probe.c -- a NON-newlibc plain minic program proving the
 * libstub-free path for the ORDINARY build-example regime (§7q, Phase-6
 * libstub retirement).
 *
 * §7n/§7o/§7p retired libstub for the newlibc test tree (built by
 * build-newlibc-test.sh, newlibc's own shiminc headers).  This probe is the
 * first NORMAL minic program — its own main(), compiled in the build-example
 * regime against minic/include/ headers (the path stevie and plain examples
 * take) — to run libstub-free: printf / puts / malloc resolve to newlibc's
 * portable stdio (printf -> _write -> VFS -> dos_shim INT 21h) + the
 * minic-compiled dos_libc.c fill + the qbe_rt/dos_syscall/heap runtime, with
 * NO libstub linked and in particular NOT libstub_to_exe.py's python printf
 * engine.  Built `tools/build-example.sh --no-libstub` and gated small +
 * medium in test-dos.sh against this golden.
 *
 * Deterministic fixed-text output (no addresses): bug-loud against a broken
 * runtime — a wrong _qbe_* decimal/hex conversion, a missing libc symbol
 * (fails the link), or a heap that doesn't carve cleanly all diff the golden.
 *
 * main() is renamed newlibc_test_main by the build's -Dmain=, so dos_shim's
 * main() runs vfs_init() before calling it (printf needs the VFS console up).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	char *s;
	int i;
	long sum;

	/* printf only (no puts): libstub.asm — the equivalence-anchor runtime —
	 * provides printf but not puts, so sticking to printf lets the SAME
	 * source link both with libstub (python printf) and libstub-free
	 * (newlibc printf) and diff one shared golden. */
	printf("printf_nolibstub_probe start\n");

	/* Integer / unsigned / hex / char formatting through newlibc's printf
	 * and the _qbe_* division helpers it leans on for the decimal digits. */
	printf("dec=%d neg=%d uns=%u\n", 12345, -678, 60000);
	printf("hex=%x char=%c pct=%%\n", 0xBEEF, 'V');

	/* A running sum exercises the 32-bit add/div path under %ld. */
	sum = 0;
	for (i = 1; i <= 100; i++)
		sum += i;
	printf("sum1..100=%ld\n", sum);

	/* malloc/free + a string round trip: the whole heap chain
	 * (dos_libc malloc -> _sbrk -> heap.asm) from a normally-built program. */
	s = malloc(32);
	if (s == NULL) {
		printf("FAIL: malloc returned NULL\n");
		return 1;
	}
	strcpy(s, "victor 9000");
	printf("str=%s len=%d\n", s, (int)strlen(s));
	free(s);

	printf("printf_nolibstub_probe done\n");
	return 0;
}
