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
 * §7n (snprintf_test + the full small FAT/VFS/ramfs/stdio/block test set):
 * the str/mem family — memcpy / memset / strlen / strcmp / strcpy / memcmp —
 * plus the std-stream FILE objects below.  §7o adds malloc / free (the
 * allocator newlibc itself lacks), backed by newlibc's _sbrk + the BSS heap
 * in minic/dos/heap.asm.  As more tests join the libstub-free path, grow this
 * file with the functions they call (more str/mem, ctype, atoi, ...).
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

/* malloc / free (§7o, Phase-6 libstub retirement).
 *
 * newlibc's portable subset has NO allocator of its own — phase-3 links
 * newlib's libc.a for malloc/free, the same gap dos_libc fills for str/mem.
 * This is the canonical K&R free-list allocator, backed by newlibc's _sbrk
 * (libgloss/syscalls.c), which carves from the BSS heap bracketed by
 * __heap_start / __heap_end (minic/dos/heap.asm).  All near pointers in one
 * DGROUP (small model), so Header* comparisons in free() are a plain 16-bit
 * compare.  --gc-sections drops the whole chain (malloc → _sbrk → heap) from
 * any --no-libstub build that never calls malloc. */

typedef long Align;		/* worst-case alignment for the payload */

union header {
	struct {
		union header *ptr;	/* next free block */
		size_t size;		/* size of this block, in Header units */
	} s;
	Align x;			/* force Header alignment */
};
typedef union header Header;

extern void *_sbrk(ptrdiff_t incr);

void *malloc(size_t nbytes);
void free(void *ap);

static Header base;		/* the empty list to get started */
static Header *freep = NULL;	/* start of the free list */

#define NALLOC 256		/* minimum #units to request from _sbrk */

static Header *morecore(size_t nu)
{
	char *cp;
	Header *up;

	if (nu < NALLOC)
		nu = NALLOC;
	cp = (char *)_sbrk((ptrdiff_t)(nu * sizeof(Header)));
	if (cp == (char *)-1)		/* heap exhausted */
		return NULL;
	up = (Header *)cp;
	up->s.size = nu;
	free((void *)(up + 1));		/* fold the new arena into the list */
	return freep;
}

void *malloc(size_t nbytes)
{
	Header *p, *prevp;
	size_t nunits;

	nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;
	if ((prevp = freep) == NULL) {	/* no free list yet */
		base.s.ptr = freep = prevp = &base;
		base.s.size = 0;
	}
	for (p = prevp->s.ptr; ; prevp = p, p = p->s.ptr) {
		if (p->s.size >= nunits) {		/* big enough */
			if (p->s.size == nunits)	/* exact fit */
				prevp->s.ptr = p->s.ptr;
			else {				/* carve the tail */
				p->s.size -= nunits;
				p += p->s.size;
				p->s.size = nunits;
			}
			freep = prevp;
			return (void *)(p + 1);
		}
		if (p == freep)			/* wrapped the free list */
			if ((p = morecore(nunits)) == NULL)
				return NULL;	/* none left */
	}
}

void free(void *ap)
{
	Header *bp, *p;

	if (ap == NULL)
		return;
	bp = (Header *)ap - 1;		/* the block header */
	for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
		if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
			break;		/* freed block at the arena's start/end */

	if (bp + bp->s.size == p->s.ptr) {	/* coalesce with the upper nbr */
		bp->s.size += p->s.ptr->s.size;
		bp->s.ptr = p->s.ptr->s.ptr;
	} else
		bp->s.ptr = p->s.ptr;

	if (p + p->s.size == bp) {		/* coalesce with the lower nbr */
		p->s.size += bp->s.size;
		p->s.ptr = bp->s.ptr;
	} else
		p->s.ptr = bp;

	freep = p;
}

/* ===========================================================================
 * §7r (Phase-6 libstub retirement, stevie): the wider libc surface a normal
 * minic program (stevie) calls that the §7n/§7o fill did not yet cover.
 *
 * STRATEGY = match libstub.asm EXACTLY (the equivalence anchor): the
 * libstub-free build must be behavior-identical to the interactively-verified
 * libstub stevie, and the gate diffs one golden across both runtimes.  So the
 * functions libstub implements for real (the str/ctype family, exit) are
 * implemented for real here, and the ones libstub STUBS (atoi -> 0, getc ->
 * -1, getenv -> NULL, system -> 0, signal -> NULL) are stubbed identically.
 * Real atoi/getc/getenv/system would be an IMPROVEMENT over the verified
 * baseline, hence a behavior change the libstub anchor can't validate — a
 * separate future step, NOT this one.  --gc-sections drops any of these a
 * given program never references (the newlibc tests pull none of them).
 * =========================================================================== */

/* ---- string (libstub.asm _strncmp/_strchr/_strrchr/_strcat/_strncpy) ---- */

int strncmp(const char *a, const char *b, size_t n)
{
	while (n) {
		unsigned char ca = (unsigned char)*a;
		unsigned char cb = (unsigned char)*b;
		if (ca != cb)
			return (int)ca - (int)cb;
		if (ca == 0)
			return 0;
		a++;
		b++;
		n--;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	char ch = (char)c;

	for (;; s++) {
		if (*s == ch)
			return (char *)s;
		if (*s == 0)
			return NULL;
	}
}

char *strrchr(const char *s, int c)
{
	char ch = (char)c;
	const char *last = NULL;

	for (;; s++) {
		if (*s == ch)
			last = s;
		if (*s == 0)
			return (char *)last;
	}
}

char *strcat(char *dest, const char *src)
{
	char *d = dest;

	while (*d)
		d++;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	char *d = dest;

	while (n && *src) {
		*d++ = *src++;
		n--;
	}
	while (n) {		/* pad the remainder with NUL */
		*d++ = 0;
		n--;
	}
	return dest;
}

/* ---- ctype (libstub.asm _isalpha/_isdigit/_isspace/_islower/_isupper/
 * _toupper/_tolower); ranges copied byte-for-byte from libstub. ---- */

int isalpha(int c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? 1 : 0;
}

int isdigit(int c)
{
	return (c >= '0' && c <= '9') ? 1 : 0;
}

int isspace(int c)
{
	/* libstub: space, or 9..13 (TAB, LF, VT, FF, CR). */
	return (c == ' ' || (c >= 9 && c <= 13)) ? 1 : 0;
}

int islower(int c)
{
	return (c >= 'a' && c <= 'z') ? 1 : 0;
}

int isupper(int c)
{
	return (c >= 'A' && c <= 'Z') ? 1 : 0;
}

int toupper(int c)
{
	return (c >= 'a' && c <= 'z') ? c - 0x20 : c;
}

int tolower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
}

/* ---- termination (libstub.asm _exit: INT 21h AH=4Ch, AL=status) ----
 * Route through newlibc's _exit (libgloss/syscalls.c); the build's
 * NL_HALT2DOS rewrite turns its `hlt` into `INT 21h` AX=0x4C00, so the
 * process exits cleanly to DOS.  (newlibc _exit ignores the status, so the
 * DOS errorlevel is always 0 rather than libstub's `status`; invisible on
 * stdout, so the golden is unaffected, and stevie never inspects its own
 * errorlevel.) */

extern void _exit(int status);

void exit(int status)
{
	_exit(status);
	for (;;)		/* _exit does not return; satisfy the compiler */
		;
}

/* ---- stubs matching libstub EXACTLY (see strategy note above) ---- */

int atoi(const char *s)
{
	(void)s;
	return 0;		/* libstub _atoi: mov ax,0 / ret */
}

/* getc: the EXE libstub (libstub_to_exe.py) overrides libstub.asm's .COM-path
 * `mov ax,-1` stub with a REAL buffered read, so the .EXE anchor — and stevie's
 * fileio.c getc(f) file-read loop — expect a working getc.  Delegate to
 * newlibc's fgetc (read via fp->_file), which the FAT/VFS tests already gate. */
extern int fgetc(FILE *fp);

int getc(FILE *fp)
{
	return fgetc(fp);
}

char *getenv(const char *name)
{
	(void)name;
	return NULL;		/* libstub _getenv: xor ax,ax / xor dx,dx */
}

int system(const char *cmd)
{
	(void)cmd;
	return 0;		/* libstub _system: mov ax,0 / ret */
}

/* signal(int signum, void (*handler)(int)) returns the previous handler;
 * libstub returns NULL.  The in-tree <signal.h> declares `int signal()`. */
int signal(int signum, int handler)
{
	(void)signum;
	(void)handler;
	return 0;		/* libstub _signal: xor ax,ax / xor dx,dx */
}

/* ---- stevie-only surface (referenced by the editor, not the gate probe's
 * core, beyond strcspn).  Real strcspn; the rest match libstub's stubs. ---- */

size_t strcspn(const char *s, const char *reject)
{
	size_t n = 0;
	const char *r;

	for (; *s; s++, n++) {
		for (r = reject; *r; r++)
			if (*s == *r)
				return n;
	}
	return n;
}

int chmod(const char *path, int mode)
{
	(void)path;
	(void)mode;
	return 0;		/* libstub _chmod: mov ax,0 / ret */
}

/* remove: the EXE libstub (libstub_to_exe.py) overrides libstub.asm's .COM
 * `mov ax,0` stub with a REAL unlink (returns -1 on a missing file), so match
 * by delegating to newlibc's unlink (libgloss/unlink.c, in the support set). */
extern int unlink(const char *path);

int remove(const char *path)
{
	return unlink(path);
}

/* rename is NOT defined here: newlibc's libgloss/rename.c already provides it
 * (and the medium fat_write tests link rename.c — a dos_libc.c copy would be a
 * duplicate public symbol).  Consumers that need it (stevie) link rename.c. */

char *mktemp(char *template)
{
	return template;	/* libstub _mktemp: returns the template unchanged */
}

/* delay/sleep: libstub stubs them to a bare `ret` (no-op).  stevie calls them
 * for screen pacing / shell-out waits; a no-op is fine on DOSBox/Victor. */
void delay(int ms)
{
	(void)ms;
}

int sleep(int secs)
{
	(void)secs;
	return 0;
}
