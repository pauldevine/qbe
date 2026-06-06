/*
 * dos_far_probe.c -- runtime gate for the far-pointer DOS API wrappers
 * added in session 2026-05-24 (s).
 *
 * Under --model=large (and huge), every data pointer is 4 bytes (off:seg).
 * The unmangled _intdos/_int86/_segread in libstub.asm consume *near*
 * pointers, so callers passing 4-byte far pointers would have the
 * helpers read garbage.  This probe exercises the new _far_intdos /
 * _far_int86 / _far_segread variants that minic mangles to under
 * far-data memory models.
 *
 * Mirrors dosapi_probe.c (medium-model gate) but built under --model=large.
 * Validation pattern: each assertion reduces to a single boolean,
 * printed as `name=%d (want 1)` to match the cstrprobe shape.
 *
 * Covered:
 *   - segread:  CS != 0, DS == SS, ES != 0  (real-mode invariants)
 *   - intdos AH=0x30 (get DOS version):  major >= 3, major <= 15
 *   - intdos AH=0x3300 (get ctrl-break): DL is 0 or 1
 *   - int86  0x21 AH=0x30:               same shape via int86 entry
 *
 * Build:  tools/build-example.sh --model=large minic/dos/examples/dos_far_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/dos_far_probe/dos_far_probe.exe \
 *             | diff - minic/dos/tests/dos_far_probe.golden.txt
 */

#include <dos.h>
#include <stdio.h>

int main()
{
	union REGS r;
	union REGS out;
	struct SREGS segs;

	/* === segread === */
	segs.cs = 0;
	segs.ds = 0;
	segs.ss = 0;
	segs.es = 0;
	segread(&segs);
	printf("seg_cs_nz=%d (want 1)\r\n", segs.cs != 0);
	printf("seg_ds_eq_ss=%d (want 1)\r\n", segs.ds == segs.ss);
	printf("seg_es_nz=%d (want 1)\r\n", segs.es != 0);

	/* === intdos AH=0x30 (get DOS version) === */
	r.h.ah = 0x30;
	r.h.al = 0;
	intdos(&r, &out);
	printf("dos_major_ge_3=%d (want 1)\r\n", out.h.al >= 3);
	printf("dos_major_le_15=%d (want 1)\r\n", out.h.al <= 15);

	/* === intdos AH=0x3300 (get ctrl-break state) === */
	r.h.ah = 0x33;
	r.h.al = 0x00;
	intdos(&r, &out);
	printf("brk_dl_in_range=%d (want 1)\r\n", out.h.dl <= 1);

	/* === int86 0x21 AH=0x30 (get DOS version via int86) === */
	r.h.ah = 0x30;
	r.h.al = 0;
	int86(0x21, &r, &out);
	printf("i86_dos_ge_3=%d (want 1)\r\n", out.h.al >= 3);

	/* === puts: writes s + CRLF to stdout via _far_puts === */
	puts("puts_ok=1 (want 1)");

	return 0;
}
