/* objalign_probe.c — i8086 fast-alloc 4-byte address alignment.
 *
 * MicroPython's REPR_A object model tags pointers in the low 2 bits, so any
 * stack object whose address is used as an mp_obj_t MUST be 4-byte aligned
 * (low 2 bits clear).  On 8086 BP is only 2-byte aligned, so the bare
 * `lea`-ed offset of a stack local can land at &3==2.  The i8086 backend
 * rounds the materialised address of a >=4-byte fast-alloc up to a 4-byte
 * boundary (isel reserves headroom; emit rounds in the Oaddr handlers).
 *
 * This mirrors the mp_builtin_min_max -> mp_getiter -> list-iterator
 * stack-iter_buf round-trip that raised "object not an iterator" before the
 * fix: a function writes through &stack_buf and returns it; the caller both
 * derefs the returned (tagged) pointer AND reads the local directly — both
 * must see the same rounded base.  Bug-loud: pre-fix this prints
 * "is_obj BUG" / round-trip FAILs for any buf landing at &3==2.
 */
#include <stdio.h>

typedef struct { const void *type; } base_t;
typedef struct { base_t base; void *buf[3]; } iter_buf_t;   /* 16 bytes */
static const base_t iter_type;

static void *new_iter(void *o, iter_buf_t *b) {
	b->base.type = &iter_type;
	b->buf[0] = o;
	return b;
}
static void *getiter(void *o, iter_buf_t *b) { return new_iter(o, b); }

static int aligned(void *p) { return ((unsigned long)p & 3) == 0; }

static void one(const char *tag, void *seed)
{
	iter_buf_t iter_buf;
	void *it;

	iter_buf.buf[1] = seed;                 /* direct field write */
	it = getiter((void *)0x2222, &iter_buf); /* escape + write-through */
	printf("%s is_obj %s\r\n", tag, aligned(it) ? "OK" : "BUG");
	printf("%s type %s\r\n", tag,
	       (((iter_buf_t *)it)->base.type == &iter_type) ? "OK" : "FAIL");
	printf("%s buf0 %s\r\n", tag,
	       (((iter_buf_t *)it)->buf[0] == (void *)0x2222) ? "OK" : "FAIL");
	printf("%s buf1 %s\r\n", tag,
	       (iter_buf.buf[1] == seed) ? "OK" : "FAIL");
}

int
main(void)
{
	int n = 1;
	/* nest in a non-entry block + perturb the frame, as min_max does */
	if (n == 1) {
		char pad = 0;                  /* odd-size local to shift offsets */
		one("a", (void *)0x3333);
		one("b", (void *)0x4444);
		(void)pad;
	}
	return 0;
}
