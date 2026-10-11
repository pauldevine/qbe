/*
 * float_narrow_probe.c — bug-loud gate for float → sub-int integer
 * conversions (gcc.dg sweep, tools/gccdg).
 *
 * THE BUG: a float converted to a char/short target emitted `=b stosi` /
 * `=h stosi` (the MEMORY class of the target), which QBE rejects; and a
 * float stored into a BITFIELD was masked as-is (`and` on an s-class
 * temp — "invalid type for first operand in and", c99-bool-3).
 * THE FIX: the conversion computes at word width (opkls) and a float
 * bitfield RHS converts to the field's integer type before masking.
 * Golden = C truncation-toward-zero semantics (native cc run).  Builds
 * with --softfloat; the UNFIXED compiler fails at the QBE stage.
 */
#include <stdio.h>

struct bf { unsigned char a : 3; unsigned short b : 9; unsigned char c : 4; };

static float opaque(float x) { return x; }

int main(void)
{
	float f1 = opaque(3.75f), f2 = opaque(65.9f), f3 = opaque(-7.5f);
	short s = f1;
	char c = f2;
	unsigned char uc = f2;
	short sn = f3;
	struct bf x;

	x.a = 0; x.b = 0; x.c = 0;
	x.a = opaque(5.9f);
	x.b = opaque(300.2f);
	x.c = opaque(9.7f);
	printf("short=%d char=%d uchar=%d neg=%d cast=%d\n", s, c, uc, sn, (int)(short)opaque(1000.9f));
	printf("bf a=%d b=%d c=%d\n", x.a, x.b, x.c);
	printf("float_narrow_probe done\n");
	return 0;
}
