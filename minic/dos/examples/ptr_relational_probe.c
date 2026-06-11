/*
 * ptr_relational_probe.c -- C pointer RELATIONAL compares must be UNSIGNED
 * (the §4o latent bug; see NEXT_SESSION.md).
 *
 * minic lowered pointer <, <=, >, >= to the SIGNED cslt/csle (pointer
 * types never carry the UNSIGNED flag, so the emit-site unsigned branch
 * never fired).  C11 6.5.8 pointer comparison is an address comparison:
 * it must be unsigned.  The signed lowering is wrong whenever the two
 * operands straddle the sign bit -- a near pointer offset >= 0x8000
 * compared against one below, or a far pointer whose SEGMENT word is
 * >= 0x8000 compared against one below.  It was harmless by luck in the
 * MicroPython images only because every far-data segment there is
 * >= 0x8000 (all "negative", so signed ordering matches unsigned).
 *
 * Discriminating cases (signed lowering inverts the result):
 *   1-5. far pointers with segments 0x9000 vs 0x7000 (Kl compare; the
 *        32-bit seg:off value's sign bit is segment bit 15).  Far
 *        pointers are Kl in EVERY model, so these discriminate under
 *        medium and compact alike.
 *   6-7. default `char *` from synthetic addresses 0x9000 vs 0x7000.
 *        Under medium that is the near Kw compare path (sign bit =
 *        offset bit 15, discriminating).  Under far-data models the
 *        cast widens to a far pointer; the expected output is the same
 *        either way, so one golden serves both models.
 * Regression guards:
 *   8.   in-array ordering (signed and unsigned agree; pins no-scaling).
 *   9-10. plain signed int compares must STAY signed -- the fix keys on
 *        KIND==PTR, not on widening every compare.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/ptr_relational_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/ptr_relational_probe/ptr_relational_probe.exe \
 *             | diff - minic/dos/tests/ptr_relational_probe.golden.txt
 *
 * Gated under medium (near Kw compare) + compact (far-data Kl compare).
 */

#include <stdio.h>
#include <dos.h>

/* Identity functions opaque to constant folding. */
char far *
fident(char far *p)
{
	return p;
}

char *
nident(char *p)
{
	return p;
}

int
iident(int x)
{
	return x;
}

int
main(void)
{
	char far *fhi = fident((char far *)MK_FP(0x9000, 4));
	char far *flo = fident((char far *)MK_FP(0x7000, 4));
	char *nhi = nident((char *)0x9000);
	char *nlo = nident((char *)0x7000);
	int arr[4];
	int *a = &arr[1];
	int *b = &arr[3];

	/* (1-5) far pointer ordering across the segment sign bit. */
	printf("ok1 %d\r\n", flo < fhi ? 1 : 0);   /* 1; signed bug: 0 */
	printf("ok2 %d\r\n", fhi > flo ? 1 : 0);   /* 1; signed bug: 0 */
	printf("ok3 %d\r\n", flo <= fhi ? 1 : 0);  /* 1; signed bug: 0 */
	printf("ok4 %d\r\n", fhi >= flo ? 1 : 0);  /* 1; signed bug: 0 */
	printf("ok5 %d\r\n", fhi < flo ? 1 : 0);   /* 0; signed bug: 1 */

	/* (6-7) default pointer ordering across the offset sign bit. */
	printf("ok6 %d\r\n", nlo < nhi ? 1 : 0);   /* 1; near signed bug: 0 */
	printf("ok7 %d\r\n", nhi <= nlo ? 1 : 0);  /* 0; near signed bug: 1 */

	/* (8) well-defined in-array ordering. */
	printf("ok8 %d\r\n", a < b ? 1 : 0);       /* 1 */

	/* (9-10) signed int compares stay signed. */
	printf("ok9 %d\r\n", iident(-1) < iident(1) ? 1 : 0);          /* 1 */
	printf("ok10 %d\r\n", iident(-28672) < iident(0x7000) ? 1 : 0); /* 1; -28672 = 0x9000 as u16 — unsigned would say 0 */

	return 0;
}
