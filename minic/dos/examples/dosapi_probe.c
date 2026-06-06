/*
 * dosapi_probe.c -- runtime gate for the side-effect-free DOS API
 * wrappers added in commits 28941ae / d36f103.
 *
 * `int86x_probe.c` already covers int86x / intdosx / segread by visual
 * inspection of the DOSBox output.  This probe runs a small subset
 * (the side-effect-free reads) and asserts deterministic 0/1 results
 * so they can be diff'd against a golden under `tools/test-dos.sh`.
 *
 * Covered:
 *   - segread:    CS != 0, DS == SS, ES != 0  (medium model invariants)
 *   - intdos AH=0x30 (get DOS version):   major version >= 3
 *   - intdos AH=0x3300 (get ctrl-break):  DL is 0 or 1
 *   - bdos   AH=0x19 (get current drive): AL < 26
 *
 * What we do NOT cover (intentionally):
 *   - set_video_mode / putpixel — graphics mode flips the display and
 *     wedges DOSBox's headless capture.
 *   - kbhit / getche / mouse_* — require user input.
 *   - intdosx with AH=2A (date) — host clock leaks in, output not
 *     diff-stable.
 *   - The exact DOS-version major/minor numbers — DOSBox reports 5.00
 *     by default but can be configured to mimic other versions.
 *
 * Validation pattern: each assertion reduces to a single boolean,
 * printed as `name=%d (want 1)` to match the cstrprobe shape and to
 * keep the output stable across host configurations.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/dosapi_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/dosapi_probe/dosapi_probe.exe \
 *             | diff - minic/dos/tests/dosapi_probe.golden.txt
 */

#include <dos.h>
#include <stdio.h>

int main()
{
	union REGS r;
	struct SREGS segs;
	int drive;

	/* === segread === */
	segs.cs = 0;
	segs.ds = 0;
	segs.ss = 0;
	segs.es = 0;
	segread(&segs);
	printf("seg_cs_nz=%d (want 1)\r\n", segs.cs != 0);
	printf("seg_ds_eq_ss=%d (want 1)\r\n", segs.ds == segs.ss);
	printf("seg_es_nz=%d (want 1)\r\n", segs.es != 0);

	/* === intdos AH=0x30 (get DOS version) ===
	 * Returns: AL=major, AH=minor.  Every DOSBox release reports >= 3. */
	r.h.ah = 0x30;
	r.h.al = 0;
	intdos(&r, &r);
	printf("dos_major_ge_3=%d (want 1)\r\n", r.h.al >= 3);
	printf("dos_major_le_15=%d (want 1)\r\n", r.h.al <= 15);

	/* === intdos AH=0x3300 (get ctrl-break state) ===
	 * Returns: DL = 0 (off) or 1 (on).  DOSBox defaults to 0; we just
	 * assert the value is one of those two so the test is stable
	 * regardless of host config. */
	r.h.ah = 0x33;
	r.h.al = 0x00;
	intdos(&r, &r);
	printf("brk_dl_in_range=%d (want 1)\r\n", r.h.dl <= 1);

	/* === bdos AH=0x19 (get current drive) ===
	 * Returns: AX (AL = drive 0=A..25=Z, AH = function code we set).
	 * Mask to AL since the bdos() shim returns the raw AX. */
	drive = bdos(0x19, 0, 0) & 0xFF;
	printf("drive_in_range=%d (want 1)\r\n", drive >= 0 && drive < 26);

	return 0;
}
