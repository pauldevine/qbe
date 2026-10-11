/*
 * const_addr_probe.c — bug-loud gate for a literal integer ADDRESS used as
 * a memory operand (`*(int *)0x10`).
 *
 * THE BUG (i8086/emit.c `%M` formatter): a constant that reaches a
 * load/store as the address operand was printed bare, so a load read the
 * IMMEDIATE (`mov ax, 16` instead of `mov ax, [16]`) and a store did not
 * even assemble (`mov word 0, _e`, gcc.dg torture/pr69117).
 * THE PROBE: read two words at literal DS-relative offsets both through
 * the literal (constant-folded address) and through an opaque volatile
 * pointer variable holding the same address, and require the pairs to
 * agree.  On the unfixed compiler the literal reads yield 16 and 32 (the
 * addresses themselves) — both memory words would have to equal their own
 * offsets for a false pass.  Near-data models only (small/medium): under a
 * far-data model `(int *)16` is a seg:off far pointer, a different path.
 */
#include <stdio.h>

volatile int *vp1, *vp2;

int main(void)
{
	int a1, a2, b1, b2;
	vp1 = (volatile int *)16;
	vp2 = (volatile int *)32;
	a1 = *(volatile int *)16;
	a2 = *(volatile int *)32;
	b1 = *vp1;
	b2 = *vp2;
	printf("const-addr loads agree: %d %d\n", a1 == b1, a2 == b2);
	printf("const_addr_probe done\n");
	return 0;
}
