/*
 * bitfield_far_probe.c — bitfield read-MODIFY-write through a far address.
 *
 * Pins a far-data codegen bug (§2e): minic's bitfield ASSIGNMENT path
 * (case '=' on a `.` member) computed a far storage-unit address
 * (ptyp = IDIR_FAR, base_far true under any far-data model) but then
 * emitted a NEAR `load`/`store%c` for the read-modify-write.  A near
 * store of a far Kl address uses only the offset against DS, so the
 * write landed in the wrong segment and the bitfield value never reached
 * its real (far) home — the subsequent read returned 0 / garbage.  The
 * bitfield READ path already used loadfar; only the write was wrong.
 *
 * Canonical victim: MicroPython's parser.  py/parse.c's rule_stack_t is
 *   { size_t src_line : 8; size_t rule_id : 8; size_t arg_i; }
 * pushed/popped from a GC-heap (far) rule stack.  push_rule writes
 * rs->rule_id; pop_rule reads it back.  With the broken write, the
 * top-level RULE_single_input was stored to the wrong segment, pop_rule
 * always read rule_id==0, and mp_parse spun forever (the parse FSM never
 * emptied its rule stack) — a hard HANG after the lexer on the Victor.
 *
 * Gated compact + large (the far-data models).  Medium (near data) takes
 * the byte-identical near path and is covered by the existing bitfield
 * coverage; this probe targets the far path specifically.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/bitfield_far_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/bitfield_far_probe/bitfield_far_probe.exe) \
 *              minic/dos/tests/bitfield_far_probe.golden.txt
 */

#include <stdio.h>
#include <stdlib.h>

/* Same shape as py/parse.c's rule_stack_t (size_t is 16-bit on i8086, so
 * src_line is an 8-bit field and rule_id the high 8 bits of word 0). */
struct rs {
	unsigned src_line : 8;
	unsigned rule_id  : 8;
	unsigned arg_i;
};

/* Mixed widths spanning a 16-bit storage unit, plus a neighbour word. */
struct bf {
	unsigned a : 3;
	unsigned b : 5;
	unsigned c : 8;
	unsigned tail;
};

int main(void)
{
	struct rs local;
	struct rs *heap;
	struct bf bits;

	/* 1. Local struct (far under far-data via base_far=!NEAR_DATA()).
	 *    Write rule_id, read it back. */
	local.src_line = 0;
	local.rule_id = 0;
	local.arg_i = 0;
	local.rule_id = 137;            /* RULE_single_input-ish nonzero */
	if (local.rule_id == 137) printf("local_rule_id ok\r\n");
	else printf("local_rule_id FAIL %u\r\n", local.rule_id);

	/* 2. Read-modify-write: setting one field must not disturb the other
	 *    packed into the same 16-bit unit (the RMW must read the REAL
	 *    current value, which needs a correct far load). */
	local.src_line = 42;
	if (local.src_line == 42 && local.rule_id == 137)
		printf("local_rmw ok\r\n");
	else
		printf("local_rmw FAIL sl=%u rid=%u\r\n", local.src_line, local.rule_id);

	/* 3. Heap struct through an explicit far pointer (the real MicroPython
	 *    case — rule_stack lives in the GC heap, a far segment). */
	heap = (struct rs *)malloc(sizeof(struct rs));
	if (heap == NULL) { printf("heap_alloc FAIL\r\n"); return 1; }
	heap->src_line = 7;
	heap->rule_id = 200;
	heap->arg_i = 0xBEEF;
	if (heap->rule_id == 200 && heap->src_line == 7 && heap->arg_i == 0xBEEF)
		printf("heap_bf ok\r\n");
	else
		printf("heap_bf FAIL sl=%u rid=%u ai=%x\r\n",
		       heap->src_line, heap->rule_id, heap->arg_i);

	/* 4. Three-field pack across one unit + neighbour word survives. */
	bits.tail = 0;
	bits.a = 5;     /* 101 */
	bits.b = 21;    /* 10101 */
	bits.c = 0xA5;
	bits.tail = 12345;
	if (bits.a == 5 && bits.b == 21 && bits.c == 0xA5 && bits.tail == 12345)
		printf("pack ok\r\n");
	else
		printf("pack FAIL a=%u b=%u c=%u t=%u\r\n",
		       bits.a, bits.b, bits.c, bits.tail);

	free(heap);
	return 0;
}
