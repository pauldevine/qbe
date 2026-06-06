/*
 * sprintf_probe.c — conformance probe for libstub's sprintf.
 *
 * Built via tools/build-sprintf-probe.sh into a self-contained DOS .EXE.
 * Run in DOSBox; output to stdout via INT 21h AH=02h:
 *   dosbox -conf <(printf "[autoexec]\nmount c .\nc:\nSP.EXE > OUT.TXT\nexit\n") -exit
 *
 * All 23 cases below were verified pass on 2026-05-20.
 *
 * Minic parser quirks worked around in this file:
 *   - 0xdeadbeefL overflows minic's NUM accumulator (`int n`) so the full
 *     32-bit hex tests are omitted.  Exercise full 32-bit %ld via stevie
 *     (Ctrl-G shows status with %ld line counts on a file > 16 bits worth
 *     of characters).
 */

char buf[128];

int writes(s) char *s; {
	while (*s) {
		dos_putch(*s);
		s++;
	}
	return 0;
}

int line(label) char *label; {
	writes(label);
	writes(": '");
	writes(buf);
	writes("'\r\n");
	return 0;
}

long la;

int main() {
	sprintf(buf, "%d", 42);            line("d 42");
	sprintf(buf, "%d", -7);            line("d -7");
	sprintf(buf, "%d", 0);             line("d 0");
	sprintf(buf, "%5d", 42);           line("5d");
	sprintf(buf, "%-5d", 42);          line("-5d");
	sprintf(buf, "%05d", 42);          line("05d");
	sprintf(buf, "%05d", -7);          line("05d-7");
	sprintf(buf, "%x", 255);           line("x");
	sprintf(buf, "%02x", 10);          line("02x");
	sprintf(buf, "%04x", 0x1234);      line("04x");
	sprintf(buf, "%X", 0xabcd);        line("X");
	sprintf(buf, "%u", 65000);         line("u");
	sprintf(buf, "%o", 8);             line("o");
	sprintf(buf, "%s", "hello");       line("s");
	sprintf(buf, "%-10s", "hi");       line("-10s");
	sprintf(buf, "%10s", "hi");        line("10s");
	sprintf(buf, "%.3s", "hello");     line(".3s");
	sprintf(buf, "%c", 65);            line("c");
	sprintf(buf, "%%");                line("pct");
	sprintf(buf, "%.5d", 42);          line(".5d");
	sprintf(buf, "%-5.3d", 42);        line("-5.3d");
	sprintf(buf, "%.0d", 0);           line(".0d");

	la = 100000L;
	sprintf(buf, "%ld", la);           line("ld 100k");

	return 0;
}
