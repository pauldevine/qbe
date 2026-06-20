/*
 * bitfield_multidecl_probe.c -- a no-consumer compiler track (hunted this
 * session): the MULTI-DECLARATOR bitfield list.
 *
 * minic parsed and correctly packed a SINGLE bitfield member
 * (`unsigned a:3;`) and a run of SEPARATE bitfield declarations
 * (`unsigned a:3; unsigned b:5;`), but a comma-separated declarator list
 * sharing one type with ANY bitfield in it --
 *
 *     unsigned a:3, b:5, c:4;   // all bitfields (the hardware-register form)
 *     unsigned x:4, y;          // bitfield then plain
 *     unsigned p, q:5;          // plain then bitfield
 *     unsigned m:3, n, o:4, r;  // mixed
 *
 * -- was a hard parse error.  Only the single-bitfield and the plain
 * multi-name (`struct L *prev, *next;`) productions existed in `smembers`;
 * there was no path for a struct-declarator list whose items can each
 * optionally carry a `: width`.  This is the C11 struct-declarator-list
 * (6.7.2.1) and the overwhelmingly common way real code lays out a packed
 * hardware register -- directly relevant to the Victor 9000 driver structs.
 *
 * THE FIX (frontend minic.y, additive): a multi-declarator list node now
 * carries an optional bitfield width-expr in n->l (NIL = a plain member),
 * so the existing `sm_more_names` tail can hold bitfields too; a new
 * `type IDENT ':' expr ',' sm_more_names ';'` production handles a list
 * whose FIRST declarator is a bitfield.  The plain-only path is byte-
 * identical (every node keeps n->l == NIL -> structaddmember, exactly as
 * before), and the all-bitfield list emits SSA byte-identical to the
 * separate-declaration form.  Conflicts stay at the 117 s/r, 0 r/r baseline.
 *
 * Bug-loud: on the pre-fix compiler the first multi-declarator bitfield
 * below is a parse error, so the program does not even build (confirmed by
 * git-stashing the minic.y change).  All values are field contents and
 * sizeofs -- model-independent -- so the golden is byte-identical across
 * small / medium / compact / large / huge.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/bitfield_multidecl_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/bitfield_multidecl_probe/bitfield_multidecl_probe.exe \
 *             | diff - minic/dos/tests/bitfield_multidecl_probe.golden.txt
 */

#include <stdio.h>

/* (A) all bitfields sharing one type -- the headline form. */
struct A { unsigned a:3, b:5, c:4; };

/* (B) a bitfield followed by a plain member (the plain member starts a new
 * storage unit, breaking the bitfield run). */
struct B { unsigned x:4, y; };

/* (C) a plain member followed by a bitfield. */
struct C { unsigned p, q:5; };

/* (D) a mixed list: bitfield, plain, bitfield, plain. */
struct D { unsigned m:3, n, o:4, r; };

int main(void)
{
	struct A sa;
	struct B sb;
	struct C sc;
	struct D sd;

	/* (A) distinct values, each within its width. */
	sa.a = 5;
	sa.b = 20;
	sa.c = 9;
	printf("A a=%u b=%u c=%u\n", sa.a, sa.b, sa.c);

	/* (A) field independence: overflow a (3 bits) must wrap to its width
	 * AND leave b/c untouched (proves the read-modify-write masking that
	 * a multi-declarator list must emit per field, same as separate
	 * declarations). 13 = 0b1101 -> low 3 bits = 0b101 = 5. */
	sa.a = 13;
	printf("A wrap a=%u b=%u c=%u\n", sa.a, sa.b, sa.c);

	/* (B) bitfield + plain. */
	sb.x = 10;
	sb.y = 1000;
	printf("B x=%u y=%u\n", sb.x, sb.y);

	/* (C) plain + bitfield. */
	sc.p = 500;
	sc.q = 20;
	printf("C p=%u q=%u\n", sc.p, sc.q);

	/* (D) mixed. */
	sd.m = 5;
	sd.n = 300;
	sd.o = 9;
	sd.r = 400;
	printf("D m=%u n=%u o=%u r=%u\n", sd.m, sd.n, sd.o, sd.r);

	/* sizeofs prove the packing layout (model-independent: unsigned is
	 * 16-bit in every i8086 model, so a bitfield storage unit is 2 bytes). */
	printf("sizeof A=%u B=%u C=%u D=%u\n",
	       (unsigned)sizeof(struct A), (unsigned)sizeof(struct B),
	       (unsigned)sizeof(struct C), (unsigned)sizeof(struct D));

	printf("bitfield_multidecl_probe done\n");
	return 0;
}
