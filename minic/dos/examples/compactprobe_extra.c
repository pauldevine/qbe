/*
 * compactprobe_extra.c — compact-mode gaps not covered by cstrprobe.c.
 *
 * Two suspected/latent bugs we want a gate for:
 *
 *  (1) Far-pointer inc/dec round-trip.  cstrprobe never executes
 *      `++p; *p` on a far-pointer local.  The store-back in
 *      minic.y:2280 (prefix ++/--) and minic.y:2387 (postfix ++/--)
 *      emits `storefw` whenever `ISFAR(sl.ctyp)`, with no
 *      `KIND != PTR` exclusion -- so for `char *p` in compact mode
 *      (which is implicitly `__far char *`), the new pointer value is
 *      written *through* the slot's far interpretation instead of
 *      *into* the slot.  Same shape as the assignment-side bug fixed
 *      in [[minic-far-var-assign-storefw]].
 *
 *  (2) Kl arithmetic on CAddr constants.  i8086/emit.c has ~10 sites
 *      (lines 986/1000/1047/1060/1105/1115/1150/1173/1196/...) that
 *      read `fn->con[r].bits.i` without checking the RCon flavour
 *      ([[i8086-cmp-caddr-emits-zero]] fixed cmp/loadl/storel; latent
 *      elsewhere).  This is hit by e.g. `(char __far *)0x12340000L + 7`
 *      where the parser folds the cast into a CAddr and then emits
 *      `Oadd Kl, CAddr, $7`.
 *
 * Validation strategy: every assertion prints one helper-returning
 * value per `printf` line to avoid the loadfb-aliases-AX trap
 * ([[i8086-compact-loadfb-aliases-ax]]).
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/compactprobe_extra.c
 * Verify: tools/run-dos-exe.sh build/examples/compactprobe_extra/compactprobe_extra.exe \
 *             | diff - minic/dos/tests/compactprobe_extra.golden.txt
 */

#include <stdio.h>
#include <string.h>

int main()
{
	char buf[8];
	char *p;
	char *q;

	/* ------------------------------------------------------------------ */
	/* (1) Far-pointer inc/dec round-trip                                 */
	/* ------------------------------------------------------------------ */
	/* Fill a known byte pattern -- A,B,C,D,E,\0 -- so a corrupted
	 * inc-store leaves a detectable signature.                          */
	strcpy(buf, "ABCDE");

	p = buf;
	printf("base0=%d (want 65)\r\n", *p);             /* 'A' */

	++p;
	printf("preinc1=%d (want 66)\r\n", *p);           /* 'B' */

	++p;
	printf("preinc2=%d (want 67)\r\n", *p);           /* 'C' */

	--p;
	printf("predec1=%d (want 66)\r\n", *p);           /* 'B' */

	--p;
	printf("predec2=%d (want 65)\r\n", *p);           /* 'A' */

	/* Postfix forms.  Use separate lines so we exercise post-inc *value*
	 * (must equal the OLD *p) and the *side effect* (next *p must equal
	 * the NEXT byte).  Two helper-returning loadfbs in one printf would
	 * trip the AX-alias bug -- one per line, again. */
	p = buf;
	q = p++;
	printf("postinc_val=%d (want 65)\r\n", *q);       /* 'A' (old) */
	printf("postinc_next=%d (want 66)\r\n", *p);      /* 'B' */

	p = buf + 2;
	q = p--;
	printf("postdec_val=%d (want 67)\r\n", *q);       /* 'C' (old) */
	printf("postdec_next=%d (want 66)\r\n", *p);      /* 'B' */

	/* Round-trip the same slot many times -- catches a corrupting
	 * store that only fires after the slot has been written once. */
	p = buf;
	++p; ++p; ++p; ++p;   /* p -> buf[4] = 'E' */
	printf("multi_inc=%d (want 69)\r\n", *p);         /* 'E' */
	--p; --p; --p; --p;   /* p -> buf[0] = 'A' */
	printf("multi_dec=%d (want 65)\r\n", *p);         /* 'A' */

	/* ------------------------------------------------------------------ */
	/* (2) Kl arithmetic on CAddr (constant address + small offset)       */
	/* ------------------------------------------------------------------ */
	/* The cast pushes minic to materialise the constant as a 32-bit
	 * far ptr, then `+ N` becomes `Oadd Kl, $0xSSSS0000, $N` after the
	 * P/M scaling.  If emit.c reads bits.i without checking CAddr type
	 * the offset comes out as 0. */
	p = (char *)0x12345000L + 7;
	printf("kl_caddr_add=%p (want 12345007)\r\n", p);

	p = (char *)0x12345020L - 8;
	printf("kl_caddr_sub=%p (want 12345018)\r\n", p);

	/* Verify the segment half survives a Kl add that carries into the
	 * upper 16 bits.  (i8086 segmentation doesn't normalise, so the
	 * carry is just a 32-bit add: 0xCAFE_0008 - 4 = 0xCAFE_0004.)     */
	p = (char *)0xCAFE0008L - 4;
	printf("kl_caddr_hi=%p (want cafe0004)\r\n", p);

	/* ------------------------------------------------------------------ */
	/* (3) %p zero-pads to 8 hex digits regardless of value width.        */
	/* ------------------------------------------------------------------ */
	/* Pre-fix _far_sprintf %p emitted variable-width hex with no padding
	 * (cosmetic per [[compactprobe_extra %p padding]]).  The fix forces
	 * width=8 + zero-fill so short values like 0x42 print as 00000042. */
	p = (char *)0x00000042L;
	printf("kl_padded=%p (want 00000042)\r\n", p);

	return 0;
}
