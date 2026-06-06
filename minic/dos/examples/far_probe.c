/*
 * far_probe.c — far-pointer probe (medium memory model).
 *
 * Validates:
 *   - minic lexer accepts 0xB8000000L (32-bit hex with L suffix)
 *   - minic grammar accepts the __far type-prefix qualifier
 *   - i8086 backend lowers storefb / loadfb through a far pointer
 *   - i8086 backend save-restores ES around far accesses so the libstub
 *     ABI (sprintf stosb against ES:DI etc.) survives intact
 *
 * Writes to a sentinel area of conventional memory at 0xA0000000L (the
 * graphics VGA frame buffer, inactive in text mode) so the probe doesn't
 * leave visible residue when stdout is redirected.
 *
 * Expected output: each step printed in order; readback shows the value
 * that storefb wrote.
 *
 * Build:  tools/build-example.sh minic/dos/examples/far_probe.c
 * Run:    dosbox build/examples/far_probe/far_probe.exe
 */

#include <stdio.h>

int main()
{
	__far char *p;
	char c;

	printf("step1: open\r\n");

	p = (__far char *)0xA0000000L;
	*p = 'X';
	printf("step2: stored\r\n");

	c = *p;
	printf("step3: readback=%c (want X)\r\n", c);
	return 0;
}
