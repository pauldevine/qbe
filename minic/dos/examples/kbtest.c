/*
 * kbtest.c — kbhit/getche keyboard polling demo.
 *
 * Polls kbhit() in a busy-loop and echoes each keystroke with its
 * decimal code.  Press 'q' (or 'Q') to quit.  Function/arrow keys
 * yield getche() == 0 (the scancode is in the second byte that
 * getche() does not return — see [[int86x-trio]] note about INT 16h).
 *
 * Build: tools/build-example.sh minic/dos/examples/kbtest.c
 * Run:   dosbox build/examples/kbtest/kbtest.exe
 */

#include <stdio.h>
#include <dos.h>

int main()
{
	int c;

	printf("kbtest — press keys (q to quit).\r\n");
	while (1) {
		if (kbhit() != 0) {
			c = getche();
			printf(" [code=%d]\r\n", c);
			if (c == 'q' || c == 'Q') break;
		}
	}
	printf("done.\r\n");
	return 0;
}
