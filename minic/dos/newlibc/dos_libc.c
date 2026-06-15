/*
 * dos_libc.c -- the minimal libc fill for the libstub-free DOS-hosted newlibc
 * link (§7n, Phase-6 libstub retirement).
 *
 * Phase 6's end-state retires libstub: a DOS-hosted newlibc program links
 * newlibc's own stdio (printf -> _write -> VFS -> dos_shim INT 21h) plus the
 * irreducible runtime (qbe_rt.asm compiler helpers, dos_syscall.asm INT 21h
 * primitives) -- and NO libstub.  That leaves only the handful of plain libc
 * functions newlibc's portable subset calls but does not define itself
 * (phase-3 normally links newlib's libc.a for these).  THIS toolchain compiles
 * them here -- the whole point of Phase 6 is to build the libc with our own
 * compiler.
 *
 * First increment (snprintf_test): the only libc symbols the linked set
 * actually calls are memcpy / memset / strlen / strcmp (from vfs/fat/block,
 * pulled in by dos_shim's vfs_init(), plus strcmp in the test).  No malloc is
 * reached -- deferred to a later increment with a real heap (see the §7o
 * handoff).  As more tests join the libstub-free path, grow this file with the
 * functions they call (str/mem family, ctype, atoi, ...).
 *
 * Compiled by tools/build-newlibc-test.sh --no-libstub via the normal
 * compile_unit path (small model, shiminc + newlibc headers).
 */

#include <stddef.h>
#include <stdio.h>

/* The std streams.  libstub used to provide _stdin/_stdout/_stderr; without it
 * we define them here.  newlibc's printf family (stream_fd in
 * printf_wrappers.c) only uses pointer identity (fp == stdout) + ->_file, so
 * three distinct one-word FILE objects carrying their fd suffice. */
static FILE shim_stdin_obj  = { 0 };
static FILE shim_stdout_obj = { 1 };
static FILE shim_stderr_obj = { 2 };
FILE *stdin  = &shim_stdin_obj;
FILE *stdout = &shim_stdout_obj;
FILE *stderr = &shim_stderr_obj;

void *memcpy(void *dest, const void *src, size_t n)
{
	char *d = (char *)dest;
	const char *s = (const char *)src;

	while (n--)
		*d++ = *s++;
	return dest;
}

void *memset(void *s, int c, size_t n)
{
	char *p = (char *)s;

	while (n--)
		*p++ = (char)c;
	return s;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *dest, const char *src)
{
	char *d = dest;

	while ((*d++ = *src++))
		;
	return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;

	while (n--) {
		if (*p != *q)
			return (int)*p - (int)*q;
		p++;
		q++;
	}
	return 0;
}
