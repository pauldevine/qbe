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
