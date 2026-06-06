/*
 * fnptr_klret_probe.c — return value of an INDIRECT far call (through a far
 * struct member fn-ptr) that returns a 32-bit (Kl) type, where the callee
 * reads its argument as a far pointer and post-increments a far-pointer
 * member.
 *
 * Reproduces a far-data codegen gap found while bringing up MicroPython on the
 * Victor (§2e): py/lexer.c's next_char does
 *     mp_uint_t chr2 = lex->reader.readbyte(lex->reader.data);
 * where mp_uint_t is uintptr_t (32-bit under far-data), lex/reader live in the
 * GC heap (a far segment, not DS), and readbyte is mp_reader_mem_readbyte:
 *     return *reader->cur++;   // far byte deref + far-member post-increment
 * The callee read the right byte (0x70 'p') and advanced cur correctly, but the
 * caller received ~0x00000001 — the 32-bit return value was lost.  With chr2
 * wrong, the lexer mis-tokenised every byte and the parser hung.
 *
 * Gated compact + large (the far-data models).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/fnptr_klret_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/fnptr_klret_probe/fnptr_klret_probe.exe) \
 *              minic/dos/tests/fnptr_klret_probe.golden.txt
 */

#include <stdio.h>
#include <stdlib.h>

typedef unsigned long mp_uint_t;   /* uintptr_t == 32-bit under far-data */

/* Mirror mp_reader_mem_t. */
struct reader_mem {
	unsigned long free_len;
	const unsigned char *beg;
	const unsigned char *cur;
	const unsigned char *end;
};

/* readbyte: data is the reader_mem; far byte deref + far-member post-inc. */
static mp_uint_t mem_readbyte(void *data)
{
	struct reader_mem *r = (struct reader_mem *)data;
	if (r->cur < r->end) {
		return *r->cur++;
	}
	return (mp_uint_t)(-1);
}

/* Mirror mp_reader_t (the by-value-passed descriptor). */
struct reader {
	void *data;
	mp_uint_t (*readbyte)(void *);
	void (*close)(void *);
};

/* Mirror the lexer holding the reader by value plus the chr shift register. */
struct lexer {
	long source_name;
	struct reader reader;
	unsigned long chr0, chr1, chr2;
};

/* The exact next_char fetch: indirect far call through a heap struct member. */
static mp_uint_t fetch(struct lexer *lex)
{
	return lex->reader.readbyte(lex->reader.data);
}

int main(void)
{
	static unsigned char src[4] = { 0x70, 0x72, 0x69, 0x00 };
	struct reader_mem *rm;
	struct lexer *lex;
	mp_uint_t c;

	rm = (struct reader_mem *)malloc(sizeof(struct reader_mem));
	lex = (struct lexer *)malloc(sizeof(struct lexer));
	if (rm == NULL || lex == NULL) { printf("alloc FAIL\r\n"); return 1; }

	rm->free_len = 0;
	rm->beg = src;
	rm->cur = src;
	rm->end = src + 3;

	lex->reader.data = rm;
	lex->reader.readbyte = mem_readbyte;
	lex->reader.close = 0;

	c = fetch(lex);
	if (c == 0x70UL) printf("c0 ok\r\n"); else printf("c0 FAIL %lx\r\n", c);
	c = fetch(lex);
	if (c == 0x72UL) printf("c1 ok\r\n"); else printf("c1 FAIL %lx\r\n", c);
	c = fetch(lex);
	if (c == 0x69UL) printf("c2 ok\r\n"); else printf("c2 FAIL %lx\r\n", c);
	c = fetch(lex);
	if (c == (mp_uint_t)(-1)) printf("ceof ok\r\n"); else printf("ceof FAIL %lx\r\n", c);

	free(lex);
	free(rm);
	return 0;
}
