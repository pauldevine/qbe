/*
 * float_fardata_probe.c — single-precision float (Ks) through FAR pointers.
 *
 * Under a far-data model (compact/large/huge) every global/array/struct datum
 * lives in a far segment, so reading or writing a `float` global goes through
 * the i8086 far load/store path.  Before the loadfs/storefs ops existed, minic
 * routed a far float through loadfw/storefw (16-bit) and silently truncated the
 * value to its low half.  This probe is the runtime proof that a full 32-bit
 * IEEE-754 binary32 value survives a far load, a far store, far arithmetic, a
 * far pointer deref, a far array element, and a far struct member.
 *
 * Build:  tools/build-example.sh --softfloat --model=compact \
 *             minic/dos/examples/float_fardata_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/float_fardata_probe/float_fardata_probe.exe \
 *             | diff - minic/dos/tests/float_fardata_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under COMPACT with --softfloat.  The bit
 * patterns are moved through a `union` (a near stack local) so the 32-bit
 * value is reinterpreted without a float literal; a far load/store that
 * dropped the high word would print a wrong %08lx pattern.
 */
#include <stdio.h>

union fb { float f; unsigned long u; };

/* Far-data globals (live in a far segment under compact/large/huge). */
float g_a;
float g_b;
float g_c;
float g_arr[4];

struct pt { float x; float y; };
struct pt g_pt;

int
main(void)
{
	union fb x;
	float *p;

	/* (1) Round-trip a full 32-bit pattern: near union -> far store
	 * (storefs) -> far load (loadfs) -> near union.  If the far path
	 * truncated to 16 bits, the high word (4049) would be lost. */
	x.u = 0x40490fdbUL;          /* 3.14159265 */
	g_a = x.f;                   /* far store */
	x.f = g_a;                   /* far load  */
	printf("rt %08lx\r\n", x.u); /* 40490fdb */

	/* (2) Arithmetic reading and writing far globals. */
	x.u = 0x40400000UL; g_a = x.f;  /* 3.0 */
	x.u = 0x40000000UL; g_b = x.f;  /* 2.0 */
	g_c = g_a + g_b;  x.f = g_c; printf("add %08lx\r\n", x.u);  /* 40a00000 5.0 */
	g_c = g_a * g_b;  x.f = g_c; printf("mul %08lx\r\n", x.u);  /* 40c00000 6.0 */
	g_c = g_a / g_b;  x.f = g_c; printf("div %08lx\r\n", x.u);  /* 3fc00000 1.5 */
	g_c = g_a - g_b;  x.f = g_c; printf("sub %08lx\r\n", x.u);  /* 3f800000 1.0 */

	/* (3) Float through an explicit far pointer. */
	p = &g_a;
	x.u = 0xc0e00000UL; *p = x.f;   /* -7.0 */
	x.f = *p; printf("ptr %08lx\r\n", x.u);   /* c0e00000 */

	/* (4) Far float array element. */
	x.u = 0x41200000UL; g_arr[2] = x.f;   /* 10.0 */
	x.f = g_arr[2]; printf("arr %08lx\r\n", x.u);  /* 41200000 */

	/* (5) Far float struct member (offset > 0). */
	x.u = 0x3dcccccdUL; g_pt.y = x.f;   /* 0.1 */
	x.f = g_pt.y; printf("mem %08lx\r\n", x.u);  /* 3dcccccd */

	/* (6) Comparison reading far globals. */
	x.u = 0x3f800000UL; g_a = x.f;  /* 1.0 */
	x.u = 0x40000000UL; g_b = x.f;  /* 2.0 */
	printf("lt %d\r\n", (g_a < g_b));   /* 1 */
	printf("gt %d\r\n", (g_a > g_b));   /* 0 */
	return 0;
}
