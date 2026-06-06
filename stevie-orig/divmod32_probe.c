/*
 * divmod32_probe.c — conformance probe for qbe i8086 32-bit divide / remainder.
 *
 * Verifies the libstub soft helpers (_qbe_div32u/s, _qbe_rem32u/s) and the
 * emit.c lowering of Odiv/Oudiv/Orem/Ourem on Kl operands.
 *
 * Build:    tools/build-divmod32-probe.sh
 * Run:      dosbox build/divmod32_probe/divmod32_probe.exe
 *
 * Output prints "case <n>: PASS" / "FAIL got=<got> want=<want>" per case.
 * Test cases cover:
 *   - small/large unsigned values (forces 32-bit path)
 *   - negative numerator / denominator
 *   - both negative (trunc-toward-zero quotient sign)
 *   - C99 remainder sign (matches dividend)
 *   - value at full-32-bit-range boundary
 *
 * Avoids 0xdeadbeefL-style literals — see [[minic-sprintf-probe-quirks]].
 */

char buf[128];

int writes(s) char *s; {
	while (*s) { dos_putch(*s); s++; }
	return 0;
}

int writed(n) int n; {
	sprintf(buf, "%d", n);
	writes(buf);
	return 0;
}

int writeu(n) unsigned long n; {
	sprintf(buf, "%lu", n);
	writes(buf);
	return 0;
}

int writeS(n) long n; {
	sprintf(buf, "%ld", n);
	writes(buf);
	return 0;
}

int ufail; int sfail;

int uassert(label, got, want) char *label; unsigned long got; unsigned long want;
{
	if (got == want) {
		writes("PASS u "); writes(label); writes("\r\n");
	} else {
		writes("FAIL u "); writes(label);
		writes(" got="); writeu(got);
		writes(" want="); writeu(want);
		writes("\r\n");
		ufail++;
	}
	return 0;
}

int sassert(label, got, want) char *label; long got; long want; {
	if (got == want) {
		writes("PASS s "); writes(label); writes("\r\n");
	} else {
		writes("FAIL s "); writes(label);
		writes(" got="); writeS(got);
		writes(" want="); writeS(want);
		writes("\r\n");
		sfail++;
	}
	return 0;
}

unsigned long ua; unsigned long ub; unsigned long uq; unsigned long ur;
long sa; long sb; long sq; long sr;
unsigned long uw;      /* scratch want-value; needed because minic doesn't
                        * promote bare integer literals at vararg/non-prototyped
                        * call sites — see [[minic-sprintf-probe-quirks]]. */
long sw;

int main() {
	/* --- unsigned 32-bit divide/remainder --- */

	/* 1: small / small */
	ua = 100; ub = 7; uq = ua / ub; ur = ua % ub;
	uw = 14;     uassert("100/7 q",  uq, uw);
	uw = 2;      uassert("100/7 r",  ur, uw);

	/* 2: large numerator > 16-bit */
	ua = 1000000; ub = 13; uq = ua / ub; ur = ua % ub;
	uw = 76923;  uassert("1M/13 q",  uq, uw);
	uw = 1;      uassert("1M/13 r",  ur, uw);

	/* 3: both > 16-bit */
	ua = 1000000; ub = 100000; uq = ua / ub; ur = ua % ub;
	uw = 10;     uassert("1M/100k q", uq, uw);
	uw = 0;      uassert("1M/100k r", ur, uw);

	/* 4: numerator = 2^31 + delta */
	ua = 2147483647; ub = 1000; uq = ua / ub; ur = ua % ub;
	uw = 2147483; uassert("2G-1/1000 q", uq, uw);
	uw = 647;     uassert("2G-1/1000 r", ur, uw);

	/* 5: exact division at 32-bit-ish range */
	ua = 16777216; ub = 4096; uq = ua / ub;
	uw = 4096;   uassert("16M/4096 q", uq, uw);

	/* --- signed 32-bit --- */

	/* 6: positive / positive */
	sa = 100; sb = 7; sq = sa / sb; sr = sa % sb;
	sw = 14;     sassert("100/7 q", sq, sw);
	sw = 2;      sassert("100/7 r", sr, sw);

	/* 7: negative / positive */
	sa = -100; sb = 7; sq = sa / sb; sr = sa % sb;
	sw = -14;    sassert("-100/7 q", sq, sw);    /* trunc-toward-zero */
	sw = -2;     sassert("-100/7 r", sr, sw);    /* sign of dividend */

	/* 8: positive / negative */
	sa = 100; sb = -7; sq = sa / sb; sr = sa % sb;
	sw = -14;    sassert("100/-7 q", sq, sw);
	sw = 2;      sassert("100/-7 r", sr, sw);

	/* 9: negative / negative */
	sa = -100; sb = -7; sq = sa / sb; sr = sa % sb;
	sw = 14;     sassert("-100/-7 q", sq, sw);
	sw = -2;     sassert("-100/-7 r", sr, sw);

	/* 10: large signed magnitudes */
	sa = 1000000L; sb = -13; sq = sa / sb; sr = sa % sb;
	sw = -76923; sassert("1M/-13 q", sq, sw);
	sw = 1;      sassert("1M/-13 r", sr, sw);

	/* 11: most-negative-ish dividend */
	sa = -2000000000L; sb = 13; sq = sa / sb;
	sw = -153846153; sassert("-2G/13 q", sq, sw);

	/* --- summary --- */
	writes("\r\n--- divmod32 probe ---\r\n");
	writes("unsigned failures: "); writed(ufail); writes("\r\n");
	writes("signed   failures: "); writed(sfail); writes("\r\n");
	return 0;
}
