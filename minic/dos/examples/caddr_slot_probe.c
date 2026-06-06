/*
 * caddr_slot_probe.c -- 16-bit Ocopy of a relocatable address into a slot.
 *
 * Selecting between the addresses of static-array elements across a branch
 * produces a phi whose incoming values are folded address constants
 * (`=w add $tbl, 12` / `=w add $tbl, 17`).  When rega lands that phi value in
 * a stack slot, the i8086 backend lowered it via the generic Ocopy template
 * `mov %=, %0`, which emitted `mov [bp-N], _tbl+12` with NO size qualifier.
 * NASM's OBJ writer then rejected the relocation ("OBJ format can only handle
 * 16- or 32-bit relocations").  This is the shape that broke py/mpprint.c
 * (`_pad_common+17`) and py/objstr.c (`__str_uni_strip_whitespace`).  The
 * backend now emits `mov word [bp-N], _tbl+12` for an immediate-into-slot
 * Ocopy.
 *
 * Near-data (medium) only: under large/huge the pointer is far (Kl), which
 * already routed through the sized Kl-Ocopy path; address-of-static far
 * relocations are a separate known limit.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/caddr_slot_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/caddr_slot_probe/caddr_slot_probe.exe \
 *             | diff - minic/dos/tests/caddr_slot_probe.golden.txt
 */

#include <stdio.h>

static const char tbl[] = "0123456789abcdefghij";  /* tbl[12]='c', tbl[17]='h' */

/* Many folded address constants kept live across a call force rega to spill
 * the selected pointers into stack slots, so the phi/assignment lowers to the
 * `Ocopy Kw, RCon(CAddr) -> RSlot` shape that exercises the fix. */
static int
pick(int which)
{
	const char *p0, *p1, *p2, *p3, *p4, *p5;
	if (which) {
		p0 = &tbl[1];  p1 = &tbl[3];  p2 = &tbl[5];
		p3 = &tbl[7];  p4 = &tbl[9];  p5 = &tbl[11];
	} else {
		p0 = &tbl[12]; p1 = &tbl[13]; p2 = &tbl[14];
		p3 = &tbl[15]; p4 = &tbl[16]; p5 = &tbl[17];
	}
	printf(".");            /* pressure: clobbers caller-save regs */
	/* sum all six dereferenced chars so every pointer stays live across
	 * the call above */
	return *p0 + *p1 + *p2 + *p3 + *p4 + *p5;
}

int
main(void)
{
	/* which=1: '1'+'3'+'5'+'7'+'9'+'b' = 49+51+53+55+57+98 = 363 */
	printf("a=%d\r\n", pick(1));
	/* which=0: 'c'+'d'+'e'+'f'+'g'+'h' = 99+100+101+102+103+104 = 609 */
	printf("b=%d\r\n", pick(0));
	printf("c=%c\r\n", tbl[0]);    /* '0' */
	return 0;
}
