/*
 * subword_bitfield_probe.c — bug-loud gate for bitfields whose container
 * type is narrower than int (char / unsigned char / _Bool / short).
 *
 * THE BUG: the bitfield read path (shr/and) and read-modify-write store
 * path (and/and/shl/or) in minic emitted their arithmetic with the
 * MEMORY class of the container (`=b` for char/_Bool, `=h` for short),
 * but QBE arithmetic exists only in w/l/s/d — so every program with a
 * sub-int bitfield died at the QBE stage with "invalid class specifier".
 * int/long containers (the only ones the MicroPython corpus uses) were
 * fine, which is why it went unnoticed until the gcc.dg sweep
 * (tools/gccdg) hit c99-bool-3, volatile-bitfields-2, store_merging_*.
 *
 * THE FIX: arithmetic on a container value uses opkls() (b/h widen to w);
 * the load/store suffix keeps irtyp().  On the UNFIXED compiler this probe
 * does not build at all; on the fixed one the golden is plain C semantics
 * (verified against a native cc run).  Only unsigned containers and
 * non-negative values are used: plain-int/char bitfield signedness is
 * implementation-defined, and _Bool-conversion of a bitfield store is a
 * separate (open) gap, so stores to the _Bool field are 0/1 only.
 */
#include <stdio.h>

struct byte_pack {            /* three fields share ONE byte */
	unsigned char a : 3;
	unsigned char b : 2;
	unsigned char c : 3;
};
struct bool_pack {            /* _Bool flags + a char field in one byte */
	_Bool f0 : 1;
	_Bool f1 : 1;
	unsigned char v : 4;
};
struct short_pack {           /* two fields share ONE halfword */
	unsigned short lo : 5;
	unsigned short hi : 11;
};
struct mixed {                /* sub-int bitfields beside ordinary members */
	unsigned char tag : 4;
	int count;
	unsigned short flags : 3;
	unsigned short width : 9;
};

static struct byte_pack g_bp;
static struct short_pack g_sp;

static void fill_via_ptr(struct bool_pack *p, int v, int f0, int f1)
{
	p->v = v;
	p->f0 = f0;
	p->f1 = f1;
}

int main(void)
{
	struct byte_pack bp;      /* local (stack) container */
	struct bool_pack lp;
	struct short_pack sp;
	struct mixed m;
	int i, acc;

	/* byte container, three fields, local */
	bp.a = 5; bp.b = 2; bp.c = 6;
	printf("local byte a=%d b=%d c=%d size=%d\n", bp.a, bp.b, bp.c, (int)sizeof(bp));
	bp.b = 1;                 /* rewrite the middle field only */
	printf("local byte after b=1: a=%d b=%d c=%d\n", bp.a, bp.b, bp.c);

	/* byte container, global */
	g_bp.a = 7; g_bp.b = 3; g_bp.c = 1;
	g_bp.a = 2;
	printf("global byte a=%d b=%d c=%d\n", g_bp.a, g_bp.b, g_bp.c);

	/* _Bool fields + char field through a pointer */
	fill_via_ptr(&lp, 9, 1, 0);
	printf("bool f0=%d f1=%d v=%d\n", lp.f0, lp.f1, lp.v);
	fill_via_ptr(&lp, 15, 0, 1);
	printf("bool f0=%d f1=%d v=%d\n", lp.f0, lp.f1, lp.v);

	/* halfword container, local + global */
	sp.lo = 31; sp.hi = 1234;
	printf("local short lo=%d hi=%d size=%d\n", sp.lo, sp.hi, (int)sizeof(sp));
	g_sp.hi = 2047; g_sp.lo = 4;
	g_sp.hi = 1000;
	printf("global short lo=%d hi=%d\n", g_sp.lo, g_sp.hi);

	/* mixed struct: bitfield updates must not disturb the int member */
	m.tag = 12; m.count = -321; m.flags = 5; m.width = 300;
	m.tag = 3; m.width = 511;
	printf("mixed tag=%d count=%d flags=%d width=%d\n", m.tag, m.count, m.flags, m.width);

	/* bitfield values in expressions / loops */
	acc = 0;
	for (i = 0; i < 8; i++) {
		bp.a = i; bp.c = 7 - i;
		acc += bp.a * 10 + bp.c;
	}
	printf("loop acc=%d last a=%d c=%d\n", acc, bp.a, bp.c);
	printf("subword_bitfield_probe done\n");
	return 0;
}
