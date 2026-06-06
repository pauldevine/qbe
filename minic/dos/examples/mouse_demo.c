/*
 * mouse_demo.c — minimal INT 33h mouse driver demo.
 *
 * Resets the mouse, shows the cursor, polls position in a loop, and
 * prints x/y/button changes.  Exit by pressing any key.
 *
 * Build: tools/build-example.sh minic/dos/examples/mouse_demo.c
 * Run:   dosbox build/examples/mouse_demo/mouse_demo.exe
 *        (DOSBox needs `mouse=true` in its config; default is on.)
 */

#include <stdio.h>
#include <dos.h>

int main()
{
	int x, y, buttons;
	int last_x, last_y, last_b;

	printf("INT 33h mouse demo\r\n");
	printf("==================\r\n");

	if (mouse_reset() == 0) {
		printf("no mouse driver loaded\r\n");
		return 1;
	}

	mouse_show();
	printf("move the mouse; press any key to quit.\r\n");

	last_x = -1;
	last_y = -1;
	last_b = -1;
	while (kbhit() == 0) {
		mouse_get_pos(&x, &y, &buttons);
		if (x != last_x || y != last_y || buttons != last_b) {
			printf("x=%d y=%d b=%d\r\n", x, y, buttons);
			last_x = x;
			last_y = y;
			last_b = buttons;
		}
	}

	mouse_hide();
	getche();  /* consume the keystroke that broke the loop */
	return 0;
}
