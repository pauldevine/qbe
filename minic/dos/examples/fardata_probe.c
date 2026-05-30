/*
 * fardata_probe.c -- additional far data segment(s) outside DGROUP.
 *
 * In a far-data memory model (compact/large/huge) qbe addresses EVERY
 * global far ("mov ax, seg _sym; mov es,ax; es:[bx]") — it never assumes
 * DGROUP.  So a module's statics can live in their OWN far segment
 * (`<BASE>_DATA` / `<BASE>_BSS`, class FAR_DATA/FAR_BSS) placed by
 * omf_link OUTSIDE DGROUP, addressed by their own segment selector.  That
 * is what lets total static data exceed the single 64KB DGROUP: DGROUP is
 * left holding only the hand-asm crt0/libstub near data plus the stack.
 *
 * This probe defines ~48KB of statics.  Under the OLD scheme (everything
 * coalesced into DGROUP) that plus the ~37KB libstub DGROUP image would
 * blow past 64KB and the link would die "DGROUP + stack overflows 64KB".
 * With far-data placement it links: the 48KB lives in a far FAR_BSS/
 * FAR_DATA segment, DGROUP stays small.  The reads below prove the far
 * segment is correctly placed and reachable via `seg`-relocated access.
 *
 * Build:  tools/build-example.sh --model=large \
 *             minic/dos/examples/fardata_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/fardata_probe/fardata_probe.exe \
 *             | diff - minic/dos/tests/fardata_probe.golden.txt
 */

#include <stdio.h>

/* ~48KB of uninitialized statics -> FAR_BSS, outside DGROUP. */
static long big[12000];

/* A small initialized table -> FAR_DATA, outside DGROUP. */
static int seed[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };

/* A second module-local array to exercise two distinct objects in the
 * same far segment (intra-segment near offsets must still resolve). */
static int tag[3] = { 700, 800, 900 };

int
main(void)
{
	int i;
	long acc;

	/* Fill the big far array with a deterministic pattern. */
	for (i = 0; i < 12000; i++)
		big[i] = (long)i * 3 + 1;

	/* Read back across the whole 48KB span (low / mid / high index). */
	printf("big[0]=%ld\n", big[0]);
	printf("big[6000]=%ld\n", big[6000]);
	printf("big[11999]=%ld\n", big[11999]);

	/* Sum a stride to make sure many far loads across the segment work. */
	acc = 0;
	for (i = 0; i < 12000; i += 1000)
		acc += big[i];
	printf("stride_sum=%ld\n", acc);

	/* Initialized far DATA content survived placement. */
	printf("seed=%x %x %x %x\n", seed[0], seed[1], seed[2], seed[3]);
	printf("tag=%d %d %d\n", tag[0], tag[1], tag[2]);
	return 0;
}
