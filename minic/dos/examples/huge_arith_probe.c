/*
 * huge_arith_probe.c -- tiny-model (.COM) gate for the huge-pointer
 * arithmetic helpers in libstub.asm (_qbe_huge_norm/add/sub/cmp).
 *
 * Phase A of [[huge-mode-plan]]: codegen does NOT yet route Mhuge Kl
 * arith through these helpers, so this probe calls them as ordinary
 * cdecl functions and checks normalisation on hand-constructed seg:off
 * values.  Once Phase B lands, ordinary `huge *p + N` arithmetic will
 * lower to the same helpers and exercise the same arithmetic paths.
 *
 * Output is the INT 21h AH=40h dance from tinyprobe.c: the .COM libstub
 * has STUB _printf / _file_io (real versions live in libstub_to_exe.py
 * and are .EXE-only), so we sprintf + dos_write each line.
 *
 * Two codegen workarounds:
 *
 *   1. Inline-literal `L` suffix is dropped by minic ([[minic-long-literal-int-vararg]]),
 *      so every long-valued arg is staged through a named `long` local
 *      before being passed to a helper.
 *
 *   2. The i8086 Kl shift handler clobbers AX/DX without telling rega.
 *      A still-live Kw temp held in AX (e.g. an unsigned int func arg
 *      to be sign-extended to Kl later) gets silently zeroed.  We avoid
 *      that here by passing both halves of every packed pointer as Kl
 *      (`unsigned long`) — `pack` no longer needs an `extsw` on either
 *      input, so AX never holds a value across the shift.
 *
 * Build:  tools/build-com-test.sh --model=tiny \
 *             minic/dos/examples/huge_arith_probe.c
 * Verify: tools/run-dos-exe.sh build/com-test/huge_arith_probe/huge_arith_probe.com \
 *             | diff - minic/dos/tests/huge_arith_probe.golden.txt
 */

#include <stdio.h>
#include <string.h>

extern unsigned long qbe_huge_norm(unsigned long ptr);
extern unsigned long qbe_huge_add(unsigned long ptr, long offset);
extern unsigned long qbe_huge_sub(unsigned long ptr, long offset);
extern long          qbe_huge_cmp(unsigned long p1, unsigned long p2);

/* See tinyprobe.c for the rationale: minic's inline-asm operand
 * substitution is broken, so we read cdecl args by fixed offsets and
 * push/pop everything we touch since clobber lists aren't surfaced to
 * rega. */
static int
dos_write(char *s, int len)
{
	__asm__ volatile (
		"push bx\n\tpush cx\n\tpush dx\n\tmov dx, [bp+4]\n\tmov cx, [bp+6]\n\tmov bx, 1\n\tmov ah, 0x40\n\tint 0x21\n\tpop dx\n\tpop cx\n\tpop bx"
	);
	return len;
}

static char buf[80];

static void
emit(char *s)
{
	int n;
	n = 0;
	while (s[n]) n++;
	dos_write(s, n);
	dos_write("\r\n", 2);
}

/* Pack (seg, off) into the 32-bit representation the helpers expect:
 * low word = off, high word = seg.  Both halves are taken as
 * `unsigned long` to avoid the Kl-shift / Kw-extsw register-clobber
 * issue described in the file header. */
static unsigned long
pack(unsigned long seg, unsigned long off)
{
	return (seg << 16) | off;
}

int
main(void)
{
	unsigned long sl;
	unsigned long ol;
	unsigned long p1;
	unsigned long p2;
	unsigned long r;
	long delta;
	long d;

	/* --- norm: already normalised (idempotent) ---------------------- */
	sl = 0x1234; ol = 0x0005;
	p1 = pack(sl, ol);
	r = qbe_huge_norm(p1);
	sprintf(buf, "norm[1234:0005]=%08lx", r);
	emit(buf);

	/* --- norm: low nibble + carry into seg -------------------------- */
	sl = 0x1000; ol = 0x0010;
	p1 = pack(sl, ol);
	r = qbe_huge_norm(p1);
	sprintf(buf, "norm[1000:0010]=%08lx", r);
	emit(buf);

	/* --- norm: full 12-bit carry from off into seg ------------------ */
	sl = 0xB800; ol = 0xFFF0;
	p1 = pack(sl, ol);
	r = qbe_huge_norm(p1);
	sprintf(buf, "norm[B800:FFF0]=%08lx", r);
	emit(buf);

	/* --- add: no carry needed --------------------------------------- */
	sl = 0x1000; ol = 0x0000;
	p1 = pack(sl, ol);
	delta = 0x10;
	r = qbe_huge_add(p1, delta);
	sprintf(buf, "add[1000:0000+10]=%08lx", r);
	emit(buf);

	/* --- add: 64K boundary cross ------------------------------------ */
	sl = 0x1000; ol = 0xFFF0;
	p1 = pack(sl, ol);
	delta = 0x20;
	r = qbe_huge_add(p1, delta);
	sprintf(buf, "add[1000:FFF0+20]=%08lx", r);
	emit(buf);

	sl = 0xB800; ol = 0xFFF0;
	p1 = pack(sl, ol);
	delta = 0x20;
	r = qbe_huge_add(p1, delta);
	sprintf(buf, "add[B800:FFF0+20]=%08lx", r);
	emit(buf);

	/* --- add: full segment hop -------------------------------------- */
	sl = 0x1000; ol = 0x0000;
	p1 = pack(sl, ol);
	delta = 0x10000;
	r = qbe_huge_add(p1, delta);
	sprintf(buf, "add[1000:0000+10000]=%08lx", r);
	emit(buf);

	/* --- add: negative offset (borrow into segment) ----------------- */
	sl = 0x1000; ol = 0x0010;
	p1 = pack(sl, ol);
	delta = -0x20;
	r = qbe_huge_add(p1, delta);
	sprintf(buf, "add[1000:0010-20]=%08lx", r);
	emit(buf);

	/* --- sub: mirror of the add path -------------------------------- */
	sl = 0x2000; ol = 0x0010;
	p1 = pack(sl, ol);
	delta = 0x20;
	r = qbe_huge_sub(p1, delta);
	sprintf(buf, "sub[2000:0010-20]=%08lx", r);
	emit(buf);

	/* --- cmp: ordered less (same seg, smaller off) ------------------ */
	sl = 0x1000; ol = 0x0000;
	p1 = pack(sl, ol);
	sl = 0x1000; ol = 0x0010;
	p2 = pack(sl, ol);
	d = qbe_huge_cmp(p1, p2);
	sprintf(buf, "cmp[<]=%ld", d);
	emit(buf);

	/* --- cmp: equal linear, different seg:off ----------------------- */
	sl = 0x1001; ol = 0x0000;
	p1 = pack(sl, ol);
	sl = 0x1000; ol = 0x0010;
	p2 = pack(sl, ol);
	d = qbe_huge_cmp(p1, p2);
	sprintf(buf, "cmp[=]=%ld", d);
	emit(buf);

	/* --- cmp: ordered greater (one full segment apart) -------------- */
	sl = 0x2000; ol = 0x0000;
	p1 = pack(sl, ol);
	sl = 0x1000; ol = 0x0000;
	p2 = pack(sl, ol);
	d = qbe_huge_cmp(p1, p2);
	sprintf(buf, "cmp[>]=%ld", d);
	emit(buf);

	emit("OK");
	return 0;
}
