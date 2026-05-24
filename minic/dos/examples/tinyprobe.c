/*
 * tinyprobe.c -- tiny-model (.COM) runtime gate.
 *
 * The tiny memory model puts code + data + stack in a single 64KB
 * segment.  Output is the awkward part: libstub.asm's `_printf` is a
 * `mov ax,0; ret` stub for the .COM pipeline (the real printf lives
 * in tools/libstub_to_exe.py and is .EXE-only).  So we cannot use
 * printf here.  Instead we sprintf into a buffer (libstub _sprintf
 * has a real implementation in libstub.asm) and then write the
 * buffer to DOS handle 1 (stdout) via inline-asm INT 21h AH=40h.
 * DOSBox's `> OUT.TXT` shell redirect captures that, and the runner
 * in tools/test-dos.sh diffs against tinyprobe.golden.txt.
 *
 * What this exercises in the tiny .COM pipeline:
 *   - integer arithmetic + 32-bit (long) divmod via libstub helpers
 *   - sprintf %d / %s / %ld / %lu / %x / %u / width-padding
 *   - near-pointer arg passing into helper functions
 *   - function pointer indirection via a near vtable
 *   - struct globals + static locals in DGROUP
 *   - INT 21h AH=40h shell-redirect through DOSBox autoexec
 *
 * Validation pattern: each output line is one sprintf -> dos_write,
 * so a single bad helper return shows up as a single diff line.
 *
 * Build:  tools/build-com-test.sh --model=tiny minic/dos/examples/tinyprobe.c
 * Verify: tools/run-dos-exe.sh build/com-test/tinyprobe/tinyprobe.com \
 *             | diff - minic/dos/tests/tinyprobe.golden.txt
 */

#include <stdio.h>      /* for sprintf prototype */
#include <string.h>     /* for strlen prototype */

/* Tiny .COM mustn't use printf (stub), fopen (stub), malloc (works
 * but heap is small).  Everything goes through this helper.
 *
 * NOTE: minic's inline-asm operand substitution (%0, %1) emits
 * `[bp-%name]` placeholders that nasm can't resolve, so we instead
 * read args by their fixed cdecl/near-call stack offsets: [bp+4] is
 * the first arg (char *), [bp+6] is the second (int).  This is
 * tiny/.COM-specific -- under .EXE far-call ABI, return address is
 * 4 bytes and these offsets shift by 2.
 *
 * minic clobbers are documented but NOT surfaced to qbe's rega, so we
 * push/pop the registers we touch within the asm body. */
static int
dos_write(char *s, int len)
{
	__asm__ volatile (
		"push bx\n\tpush cx\n\tpush dx\n\tmov dx, [bp+4]\n\tmov cx, [bp+6]\n\tmov bx, 1\n\tmov ah, 0x40\n\tint 0x21\n\tpop dx\n\tpop cx\n\tpop bx"
	);
	return len;
}

static char buf[64];

static void
emit(char *s)
{
	int n;
	n = 0;
	while (s[n]) n++;
	dos_write(s, n);
	dos_write("\r\n", 2);
}

/* --- helpers exercising near-pointer arg pass, fn ptrs --- */

static int
add_through_ptr(int *a, int *b)
{
	return *a + *b;
}

typedef int (*binop)(int, int);

static int
op_add(int a, int b)
{
	return a + b;
}

static int
op_sub(int a, int b)
{
	return a - b;
}

static int
op_mul(int a, int b)
{
	return a * b;
}

static binop ops[3];

struct point {
	int x;
	int y;
};

static struct point origin;

int
main(void)
{
	int a;
	int b;
	int i;
	long la;
	long lb;
	long lq;
	unsigned long ua;
	int *ip;
	char *p;
	static int sticky;

	/* 1. integer math + sprintf %d */
	a = 12345;
	b = 67890;
	sprintf(buf, "sum=%d", a + b);
	emit(buf);

	/* 2. near-pointer pass-by-pointer */
	sprintf(buf, "addptr=%d", add_through_ptr(&a, &b));
	emit(buf);

	/* 3. fn-ptr table dispatch (near code) */
	ops[0] = op_add;
	ops[1] = op_sub;
	ops[2] = op_mul;
	sprintf(buf, "op_add=%d", ops[0](7, 5));
	emit(buf);
	sprintf(buf, "op_sub=%d", ops[1](7, 5));
	emit(buf);
	sprintf(buf, "op_mul=%d", ops[2](7, 5));
	emit(buf);

	/* 4. static struct global in DGROUP */
	origin.x = 100;
	origin.y = 200;
	sprintf(buf, "origin=(%d,%d)", origin.x, origin.y);
	emit(buf);

	/* 5. static local survives across calls */
	for (i = 0; i < 4; i++)
		sticky = sticky + i;
	sprintf(buf, "sticky=%d", sticky);
	emit(buf);

	/* 6. 32-bit divmod via libstub helpers */
	la = 1000000L;
	lb = 7L;
	lq = la / lb;
	sprintf(buf, "sdiv=%ld", lq);
	emit(buf);
	sprintf(buf, "srem=%ld", la % lb);
	emit(buf);

	ua = 4000000000UL;
	sprintf(buf, "udec=%lu", ua);
	emit(buf);

	/* 7. %x / %u / width pad */
	sprintf(buf, "hex=%x", 0xbeef);
	emit(buf);
	sprintf(buf, "uhex=%x", -1);
	emit(buf);
	sprintf(buf, "pad=[%5d]", 42);
	emit(buf);
	sprintf(buf, "zpad=[%05d]", 42);
	emit(buf);

	/* 8. near-pointer walk + cmp */
	p = buf;
	for (i = 0; i < 5; i++)
		p[i] = '0' + i;
	p[5] = 0;
	emit(buf);

	/* 9. int* deref over a local array */
	{
		int arr[4];
		arr[0] = 11;
		arr[1] = 22;
		arr[2] = 33;
		arr[3] = 44;
		ip = arr;
		sprintf(buf, "arr=%d,%d,%d,%d", ip[0], ip[1], ip[2], ip[3]);
		emit(buf);
	}

	emit("OK");
	return 0;
}
