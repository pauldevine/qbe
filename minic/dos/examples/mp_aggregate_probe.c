/*
 * mp_aggregate_probe.c -- static aggregate features added for the
 * MicroPython port (2026-05-29, §1g; see NEXT_SESSION.md /
 * MICROPYTHON_PORT.md).  Each exercises codegen + runtime, not just
 * parse:
 *
 *   1. Bitfield static initializers -- agg_emit_struct now packs a run
 *      of bitfield members sharing one storage unit into a single data
 *      item (sequential AND `.field =` designated).  Was
 *      die("bitfield initializer unsupported").  MicroPython's
 *      mp_obj_exception_t / mp_map_t static instances need this.
 *
 *   2. Static address-of in initializers -- `&global`, `&agg.member`
 *      (member-offset chains via cival_lval), and `&global` items inside
 *      a struct-array initializer (`{ key, &glob }`, the
 *      mp_rom_map_elem_t idiom).  The sai machinery grew an 'A' (address
 *      symbol) item kind.
 *
 *   3. Widening assignment `long <- char` (uint32_t = uint8_t): the
 *      assignment converter now sign/zero-extends a char RHS to long
 *      (was die("invalid assignment")).
 *
 *   4. sizeof(T[expr]) with a constant dimension folds to SIZE(T)*dim.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/mp_aggregate_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/mp_aggregate_probe/mp_aggregate_probe.exe \
 *             | diff - minic/dos/tests/mp_aggregate_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (medium only).  NOTE: the static
 * address-of items (feature 2) emit the canonical QBE form
 * `data $P = { l $target, l $pt+2, ... }`.  Under near-data (medium)
 * the linker relocates `$sym` correctly.  Under FAR-data (large/huge) a
 * 4-byte `$sym` data item must become a far seg:off pointer, and the
 * OMF toolchain does not yet emit that segment-relocation for static
 * data symbols -- so the address-of values are wrong under large/huge.
 * That is a pre-existing backend/linker gap (nothing in-tree emitted a
 * static far pointer-to-global before); the minic frontend output is
 * correct.  Features 1/3/4 do pass under large; this probe stays
 * medium-only until the far data-symbol relocation lands.
 */

#include <stdio.h>

typedef unsigned char uint8_t;

/* (1) Bitfields packed into one 16-bit storage unit, plus a following
 * non-bitfield member.  a:3=5, b:5=9, c:8=200 -> packed = 51277. */
struct bf {
	unsigned a : 3;
	unsigned b : 5;
	unsigned c : 8;
	int after;
};
static const struct bf b_seq = { 5, 9, 200, 12345 };

/* Designated bitfield init (out-of-source-order is still in unit order). */
static const struct bf b_des = { .b = 9, .a = 5, .c = 200, .after = 999 };

/* (2) address-of globals / members, collected in an aggregate (which is
 * the initializer path the address-of work targets; a bare file-scope
 * scalar pointer initializer is a separate, still-unsupported grammar
 * form). */
static int target = 77;

struct point { int x; int y; };
static struct point pt = { 3, 4 };

/* nested member-offset chain: &outer.mid.leaf */
struct mid { int pad; int leaf; };
struct outer { int head; struct mid mid; };
static struct outer ov = { 1, { 2, 42 } };

/* &global, &member, &member-chain, all in one struct of pointers. */
struct ptrs { int *a; int *b; int *c; };
static const struct ptrs P = { &target, &pt.y, &ov.mid.leaf };

/* struct-array initializer with address values (mp_rom_map_elem_t idiom). */
struct elem { int key; void *val; };
static int g1 = 11;
static int g2 = 22;
static const struct elem table[] = {
	{ 1, &g1 },
	{ 2, &g2 },
};

int
main(void)
{
	unsigned char uc;
	unsigned long ul;

	/* (1) read packed bitfields back. */
	printf("bseq a=%u b=%u c=%u after=%d\r\n",
	       b_seq.a, b_seq.b, b_seq.c, b_seq.after);
	printf("bdes a=%u b=%u c=%u after=%d\r\n",
	       b_des.a, b_des.b, b_des.c, b_des.after);

	/* (2) address-of values dereference to the right storage. */
	printf("p_target=%d (want 77)\r\n", *P.a);
	printf("p_y=%d (want 4)\r\n", *P.b);
	printf("p_leaf=%d (want 42)\r\n", *P.c);
	printf("table0=%d table1=%d (want 11 22)\r\n",
	       *(int *)table[0].val, *(int *)table[1].val);

	/* (3) long <- char widening assignment. */
	uc = 200;
	ul = uc;
	printf("widen ul=%lu (want 200)\r\n", ul);

	/* (4) sizeof(T[expr]). */
	printf("szc=%d szi=%d (want 5 8)\r\n",
	       (int)sizeof(char[5]), (int)sizeof(int[4]));

	return 0;
}
