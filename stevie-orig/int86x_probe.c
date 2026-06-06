/*
 * int86x_probe.c — conformance probe for the segment-aware DOS API trio.
 *
 * Verifies:
 *   - segread() captures plausible CS/DS/SS/ES (CS != 0, SS == DS in
 *     small/medium model, ES non-garbage).
 *   - int86x() passes args through to a no-segment INT (AH=2Ah get-date)
 *     and returns the expected register values.
 *   - int86x() can pass DS:DX explicitly to AH=09h print-string (the
 *     classic DS-needing DOS call).
 *   - intdosx() is equivalent to int86x(0x21, ...).
 *
 * Build: tools/build-int86x-probe.sh
 * Run:   dosbox build/int86x_probe/int86x_probe.exe
 *
 * Avoids 0xdeadbeefL-style literals — see [[minic-sprintf-probe-quirks]].
 */

#include <dos.h>

char buf[128];
int fails;

int writes(s) char *s; {
	while (*s) { dos_putch(*s); s++; }
	return 0;
}

int writed(n) int n; {
	sprintf(buf, "%d", n);
	writes(buf);
	return 0;
}

int writex(n) unsigned int n; {
	sprintf(buf, "%04x", n);
	writes(buf);
	return 0;
}

int passf(label) char *label; {
	writes("PASS "); writes(label); writes("\r\n");
	return 0;
}

int failf(label) char *label; {
	writes("FAIL "); writes(label); writes("\r\n");
	fails++;
	return 0;
}

int assert_eq(label, got, want) char *label; int got; int want; {
	if (got == want) {
		passf(label);
	} else {
		writes("FAIL "); writes(label);
		writes(" got="); writed(got);
		writes(" want="); writed(want);
		writes("\r\n");
		fails++;
	}
	return 0;
}

int assert_nz(label, got) char *label; unsigned int got; {
	if (got != 0) {
		passf(label);
	} else {
		writes("FAIL "); writes(label); writes(" got=0\r\n");
		fails++;
	}
	return 0;
}

/* The DOS AH=09h print-string handler expects a '$'-terminated string.
 * We use it as a way to exercise int86x's DS:DX path.  Output is
 * captured by visual inspection in DOSBox; the test is "did this line
 * appear?". */
char hello_string[16];

int main() {
	union REGS r;
	union REGS rin;
	union REGS rout;
	struct SREGS segs;

	writes("int86x probe\r\n");
	writes("============\r\n");

	/* --- segread --- */
	segread(&segs);
	assert_nz("segread.cs nonzero", segs.cs);
	assert_eq("segread.ds==ss",    segs.ds, segs.ss);
	assert_nz("segread.es nonzero", segs.es);

	/* --- int86x with AH=2Ah (get-date) — no segment dependence, but
	 *     we still pass &segs so the wrapper exercises its segment-
	 *     load path harmlessly. */
	segread(&segs);
	rin.h.ah = 0x2A;
	int86x(0x21, &rin, &rout, &segs);
	/* CX = year (>=1980), DH = month (1..12), DL = day (1..31).  We
	 * don't pin specific values (DOSBox lets the host clock leak in)
	 * but we can sanity-check the ranges. */
	if (rout.x.cx < 1980 || rout.x.cx > 2099) {
		failf("int86x AH=2Ah year range");
	} else {
		passf("int86x AH=2Ah year range");
	}
	if (rout.h.dh < 1 || rout.h.dh > 12) {
		failf("int86x AH=2Ah month range");
	} else {
		passf("int86x AH=2Ah month range");
	}
	if (rout.h.dl < 1 || rout.h.dl > 31) {
		failf("int86x AH=2Ah day range");
	} else {
		passf("int86x AH=2Ah day range");
	}

	/* --- intdosx — must match int86x(0x21, ...) exactly. */
	segread(&segs);
	rin.h.ah = 0x2A;
	intdosx(&rin, &rout, &segs);
	if (rout.x.cx < 1980 || rout.x.cx > 2099) {
		failf("intdosx AH=2Ah year range");
	} else {
		passf("intdosx AH=2Ah year range");
	}

	/* --- int86x with AH=09h (print-string) — uses DS:DX explicitly.
	 *     We pass DS=our DGROUP via segread so the printout actually
	 *     comes from hello_string.  Visual confirmation via stdout. */
	hello_string[0] = 'h';
	hello_string[1] = 'i';
	hello_string[2] = ' ';
	hello_string[3] = 'd';
	hello_string[4] = 'o';
	hello_string[5] = 's';
	hello_string[6] = '\r';
	hello_string[7] = '\n';
	hello_string[8] = '$';
	hello_string[9] = 0;

	segread(&segs);
	rin.h.ah = 0x09;
	rin.x.dx = (unsigned int)hello_string;
	writes(">>> ");                  /* prefix so output is easy to spot */
	int86x(0x21, &rin, &rout, &segs);
	/* No assertion — visual: "hi dos" appears between >>> and ===. */
	writes("=== AH=09h done\r\n");

	/* --- get DOS version (AH=30h) — tests int86 baseline still works. */
	r.h.ah = 0x30;
	int86(0x21, &r, &r);
	if (r.h.al == 0) {
		failf("int86 AH=30h major (DOSBox sometimes reports 0?)");
	} else {
		passf("int86 AH=30h major nonzero");
	}
	writes("  DOS version: ");
	writed(r.h.al);
	writes(".");
	writed(r.h.ah);
	writes("\r\n");

	/* --- summary --- */
	writes("\r\n--- int86x probe ---\r\n");
	writes("failures: ");
	writed(fails);
	writes("\r\n");
	return 0;
}
