/*
 * shift_count_spill_probe.c — §4r regression guard for the i8086
 * variable-shift count-operand clobber ([[churn-gc-live-object-freed]] §4q).
 *
 * THE BUG: i8086 selshift gave rega no register constraint for a variable
 * shift count, and emit.c read the count from whatever register rega last
 * noted for it (`mov cx, rname[r1.val]`).  When the count was SPILLED and
 * its old register reused by an adjacent op (in gc_mark_subtree: the atb
 * byte's `extub` reused AX), the shift read the CLOBBERED register —
 * `atb >> atb` instead of `atb >> shift` — so ATB_GET_KIND mis-classified
 * a live HEAD block, the mark phase skipped it, and the "churn" qstr was
 * freed-while-live (the 13-session churn(120) NameError saga).
 *
 * THE FIX (i8086/isel.c selshift, mirroring amd64): a non-immediate count
 * is pinned to CX via a real `Ocopy CX <- count` before the shift (rega
 * lowers it correctly, reloading from the spill slot) plus a no-dest
 * `Ocopy <- CX` marker after it so rega keeps CX busy across the shift.
 *
 * The probe recreates the gc_mark_subtree shape — a 2-bit-packed table
 * scan where the shift count is `2*(block&3)` (an imul product) and the
 * shifted value is a zero-extended byte load (extub), under register
 * pressure from many live loop-carried values — and cross-checks every
 * kind classification against an arithmetic identity that uses no shifts.
 * Also covers: count >= 8, count 0, value==count (x>>x), Kl (32-bit)
 * variable shifts, and shifts whose value operand is a constant (1<<n).
 *
 * The original miscompile was register-allocation/layout-sensitive, so a
 * green run of THIS probe alone never proves the bug absent — it pins the
 * fixed semantics and fails loudly if the stale-count read ever returns.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/shift_count_spill_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/shift_count_spill_probe/shift_count_spill_probe.exe \
 *             | diff - minic/dos/tests/shift_count_spill_probe.golden.txt
 */

#include <stdio.h>

#define NBLOCKS 32

/* 2-bit kinds packed 4-per-byte, exactly like MicroPython's ATB. */
static unsigned char atb_tab[NBLOCKS / 4];
static int kind_expect[NBLOCKS];

/* Opaque identity: defeats QBE constant folding so counts/values stay
 * runtime variables. */
int opaque(int x) { return x; }
long opaque_l(long x) { return x; }

/*
 * gc_mark_subtree shape: classify each block via the packed table.  The
 * extra live values (acc/wsum/prod/lastblk + the bounds) replicate the
 * register pressure that spilled the count in gc.c.  Returns a checksum
 * of the blocks classified as HEAD (kind 1).
 */
unsigned
scan_heads(unsigned char *tab, unsigned start, unsigned end,
           unsigned *nheads, unsigned *wsum_out)
{
	unsigned block, kind, acc, wsum, prod, lastblk;

	acc = 0;
	wsum = 0;
	prod = 1;
	lastblk = 0;
	for (block = start; block < end; block++) {
		/* ATB_GET_KIND: byte load (extub) + variable shift whose
		 * count = 2*(block&3) (imul) — the §4q shape. */
		kind = (tab[block / 4] >> (2 * (block & 3))) & 3;
		wsum += kind;
		if (kind != 1)
			continue;
		acc += block * 3 + 1;
		prod = prod * 2 + block;
		lastblk = block;
		*nheads += 1;
	}
	*wsum_out = wsum;
	return acc + (prod & 7) + lastblk;
}

int
main(void)
{
	unsigned i, k, kind, nheads, wsum, chk;
	unsigned expect_heads, expect_wsum;
	int n, v, ok;
	long lv, lr;

	/* Fill the table with a deterministic kind pattern and record the
	 * per-block expectation WITHOUT shifts (built by byte composition). */
	expect_heads = 0;
	expect_wsum = 0;
	for (i = 0; i < NBLOCKS; i++) {
		kind = (i * 5 + 3) & 3;        /* 3,0,1,2, 3,0,1,2 ... */
		kind_expect[i] = kind;
		expect_wsum += kind;
		if (kind == 1)
			expect_heads++;
	}
	for (i = 0; i < NBLOCKS / 4; i++) {
		atb_tab[i] = (unsigned char)(kind_expect[i * 4]
		    + kind_expect[i * 4 + 1] * 4
		    + kind_expect[i * 4 + 2] * 16
		    + kind_expect[i * 4 + 3] * 64);
	}

	/* (1) The gc_mark_subtree scan, full range. */
	nheads = 0;
	wsum = 0;
	chk = scan_heads(atb_tab, opaque(0), opaque(NBLOCKS), &nheads, &wsum);
	printf("scan nheads=%u wsum=%u chk=%u\r\n", nheads, wsum, chk);
	if (nheads == expect_heads && wsum == expect_wsum)
		printf("scan_heads ok\r\n");
	else
		printf("scan_heads FAIL (expect nheads=%u wsum=%u)\r\n",
		    expect_heads, expect_wsum);

	/* (2) Per-block direct check: variable-shift extract vs recorded
	 * expectation for every block (catches any single wrong count). */
	ok = 1;
	for (i = 0; i < NBLOCKS; i++) {
		kind = (atb_tab[i / 4] >> (2 * (i & 3))) & 3;
		if ((int)kind != kind_expect[i])
			ok = 0;
	}
	printf("perblock %s\r\n", ok ? "ok" : "FAIL");

	/* (3) Constant value, variable count: 1 << n (the need_val_load
	 * path; emit.c materializes the value AFTER securing the count).
	 * The expectation is built by DOUBLING, never by a shift, so a
	 * shift miscompile can't corrupt both sides identically. */
	ok = 1;
	v = 1;
	for (n = 0; n < 15; n++) {
		if ((1 << opaque(n)) != v)
			ok = 0;
		v += v;
	}
	printf("one_shl_n %s\r\n", ok ? "ok" : "FAIL");

	/* (4) Count 0 and count >= 8 through a variable. */
	v = opaque(0x1234);
	n = opaque(0);
	printf("shr0=%x ", (unsigned)v >> n);
	n = opaque(12);
	printf("shr12=%x\r\n", (unsigned)v >> n);

	/* (5) value == count (x >> x) — the §4q wrong formula `atb >> atb`
	 * must NOT equal the right answer for these inputs. */
	v = opaque(9);
	printf("x_shr_x=%d\r\n", v >> v);          /* 9>>9 = 0 */
	v = opaque(2);
	printf("two_shl_two=%d\r\n", v << v);      /* 2<<2 = 8 */

	/* (6) Kl (32-bit) variable shifts — the separate emit handler also
	 * reads the count register at emit time.  Expectation lv (= 2^n)
	 * is built by doubling, shift-free: 1L << n must equal it, and
	 * 2^30 >> (30-n) must equal it too (variable descending count). */
	ok = 1;
	lv = 1;
	for (n = 0; n <= 30; n++) {
		lr = opaque_l(1L) << opaque(n);
		if (lr != lv)
			ok = 0;
		lr = opaque_l(0x40000000L) >> opaque(30 - n);
		if (lr != lv)
			ok = 0;
		lv += lv;
	}
	printf("kl_var_shift %s\r\n", ok ? "ok" : "FAIL");

	/* (7) Signed sar with a variable count. */
	v = opaque(-32768);
	n = opaque(3);
	printf("sar3=%d\r\n", v >> n);             /* -4096 */

	printf("DONE\r\n");
	return 0;
}
