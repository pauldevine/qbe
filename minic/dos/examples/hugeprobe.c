/*
 * hugeprobe.c -- Phase C gate for huge-model > 64K arrays.
 *
 * Phase A (libstub _qbe_huge_add/sub/norm helpers) and Phase B (minic
 * routes Mhuge Kl pointer add/sub through those helpers) together gave
 * us correct pointer arithmetic across a 64K boundary.  Phase C adds
 * per-symbol segments so an array CAN legally span more than 64K:
 *  - minic emits `section "_HUGE_<sym>" data $<sym> = align N { z S }`
 *    for any global array bigger than 64K under --model=huge
 *  - asm_to_omf.py splits the resulting .section block across
 *    paragraph-aligned chunks of <= 65520 bytes
 *  - omf_link.py places the chunks at consecutive paragraph bases
 *    outside DGROUP, so DGROUP+stack stays under the 64K cap and the
 *    huge array lives in its own image region
 *
 * This probe declares static char arr[80000] and writes sentinels at
 * arr[0], arr[65535] (last byte of first chunk), arr[65536] (first
 * byte of second chunk -- crosses the 64K segment boundary), and
 * arr[79999] (last byte of the array).  It reads each back via both
 * arr[i] and *(arr + i) and prints the byte value.  All four pairs of
 * lines must match, and the read-back values must equal the sentinels.
 *
 * Build:  tools/build-example.sh --model=huge \
 *             minic/dos/examples/hugeprobe.c
 * Verify: tools/run-dos-exe.sh build/examples/hugeprobe/hugeprobe.exe \
 *             | diff - minic/dos/tests/hugeprobe.golden.txt
 */

#include <stdio.h>

static char arr[80000];

int
main(void)
{
	int v;

	/* Write four sentinels across the array. */
	arr[0]     = 0x11;
	arr[65535] = 0x22;
	arr[65536] = 0x33;
	arr[79999] = 0x44;

	/* Read back via arr[i] (subscript syntax). */
	v = (unsigned char) arr[0];
	printf("idx0=%d\r\n", v);
	v = (unsigned char) arr[65535];
	printf("idx65535=%d\r\n", v);
	v = (unsigned char) arr[65536];
	printf("idx65536=%d\r\n", v);
	v = (unsigned char) arr[79999];
	printf("idx79999=%d\r\n", v);

	/* Read back via *(arr + i) (explicit pointer arith — routed
	 * through _qbe_huge_add by minic.y::huge_ptr_binop). */
	v = (unsigned char) *(arr + 0);
	printf("ptr0=%d\r\n", v);
	v = (unsigned char) *(arr + 65535);
	printf("ptr65535=%d\r\n", v);
	v = (unsigned char) *(arr + 65536);
	printf("ptr65536=%d\r\n", v);
	v = (unsigned char) *(arr + 79999);
	printf("ptr79999=%d\r\n", v);

	printf("OK\r\n");
	return 0;
}
