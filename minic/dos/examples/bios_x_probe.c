/* bios_x_probe.c — simplest possible BIOS test.  Print "A" via BIOS
 * teletype, then "B" via printf, then pause for keypress.  Tells us
 * which path is alive.
 */

#include <stdio.h>
#include <dos.h>

int main()
{
	union REGS r;

	printf("via printf: BEFORE\r\n");

	/* BIOS teletype 'A' */
	r.h.ah = 0x0E;
	r.h.al = 'A';
	r.h.bh = 0;
	r.h.bl = 7;
	int86(0x10, &r, &r);

	printf("\r\nvia printf: AFTER (between BIOS calls)\r\n");

	/* BIOS teletype 'B' */
	r.h.ah = 0x0E;
	r.h.al = 'B';
	r.h.bh = 0;
	r.h.bl = 7;
	int86(0x10, &r, &r);

	printf("\r\nvia printf: END.  Press a key.\r\n");

	/* Wait for keypress via INT 16h AH=0 */
	r.h.ah = 0;
	int86(0x16, &r, &r);

	return 0;
}
