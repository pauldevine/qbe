/*
 * vga_pixels.c — VGA mode 13h pixel-plot demo.
 *
 * Switches to 320x200x256 mode, paints a diagonal gradient using
 * putpixel(), waits for a keystroke, and returns to 80x25 text mode.
 *
 * Build: tools/build-example.sh minic/dos/examples/vga_pixels.c
 * Run:   dosbox build/examples/vga_pixels/vga_pixels.exe
 */

#include <stdio.h>
#include <dos.h>

int main()
{
	int x, y;

	set_video_mode(0x13);  /* 320x200x256 linear at 0xA000 */

	for (y = 0; y < 200; y = y + 1) {
		for (x = 0; x < 320; x = x + 1) {
			putpixel(x, y, (x + y) & 0xFF);
		}
	}

	getche();              /* wait for any key */
	set_video_mode(0x03);  /* 80x25 colour text */
	printf("done.\r\n");
	return 0;
}
