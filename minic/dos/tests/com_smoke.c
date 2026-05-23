/*
 * com_smoke.c -- regression smoke test for the i8086 tiny-model (.COM)
 *                build pipeline.
 *
 * Goals:
 *   1. Builds clean with `minic -m small -> qbe -t i8086 -m small ->
 *      nasm -f bin` (no OMF, no linker).
 *   2. Resulting flat .COM image stays comfortably under the 64 KB
 *      single-segment ceiling -- a regression in libstub size, codegen
 *      bloat, or memref-base rega hinting will push this over budget
 *      and fail the test.
 *   3. Exercises the bits most likely to regress in the small-model
 *      pipeline: near-pointer arithmetic, libstub printf/sprintf,
 *      INT 21h exit, simple loops.
 *
 * Expected stdout (when run under DOSBox):
 *
 *   COM-SMOKE
 *   sum=55
 *   ptr=hello,world
 *   OK
 *
 * Exit code: 0 on success.
 *
 * Build: tools/build-com-test.sh minic/dos/tests/com_smoke.c
 */

#include <stdio.h>
#include <string.h>

static char buf[40];

int main(void)
{
	int i;
	int sum;
	char *p;

	printf("COM-SMOKE\r\n");

	/* Loop + integer math + sprintf-with-%d. */
	sum = 0;
	for (i = 1; i <= 10; i++)
		sum += i;
	sprintf(buf, "sum=%d\r\n", sum);
	printf("%s", buf);

	/* Walk a near pointer end-to-end -- this is the path that the
	 * Move 3a hint (i8086 isel, commit bc1af04) optimises.  If the
	 * hint regresses, this still works but bloats the binary. */
	strcpy(buf, "ptr=hello");
	p = buf;
	while (*p)
		p++;
	*p++ = ',';
	strcpy(p, "world\r\n");
	printf("%s", buf);

	printf("OK\r\n");
	return 0;
}
