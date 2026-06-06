/*
 * sret_probe.c -- struct/union return-by-value gate.
 *
 * Pins the hidden-pointer struct-return ABI added 2026-05-29 (see
 * NEXT_SESSION.md / MICROPYTHON_PORT.md).  Before this, a function whose
 * return type was a struct/union emitted a word-typed `ret` and the
 * struct-typed initializer at the call site parse-errored ("invalid
 * lvalue") — the dominant MicroPython spike blocker
 * (`mp_code_lineinfo_t mp_bytecode_decode_lineinfo(...)` returns a struct
 * by value; callers `mp_code_lineinfo_t x = decode(&p);`).
 *
 * The lowering is System-V style: a struct-returning function takes a
 * hidden first pointer parameter (caller-allocated result storage),
 * copies the returned aggregate through it, and returns the pointer.
 * The caller allocates the slot, passes its address, and treats the
 * call result as the aggregate's address.
 *
 * Exercises (codegen + runtime, not just parse):
 *   1. Assignment from a struct-returning call (`q = make_pt(...)`).
 *   2. Local declaration initialized from a call (`struct Pt r = ...`).
 *   3. Member of a call result as an rvalue (`make_pt(...).y`).
 *   4. A struct-returning function that itself calls one (nested ABI).
 *   5. An odd-sized struct (`{ int; char }` = 3 bytes) to drive the
 *      word-copy loop's trailing single-byte tail.
 *   6. The exact MicroPython shape: `{ size_t; size_t }` (two words).
 *   7. A union return.
 *   8. Taking the address of the filled result and passing it on.
 *
 * NOTE (orthogonal limitation): a struct member of type `long` (4 bytes,
 * read back via `loadl`) is NOT exercised here.  When the source words
 * are opaque to QBE (a call result), forwarding a `loadl` through the
 * word-by-word copy reconstructs the 32-bit value as `lo | (hi << 16)`
 * but the i8086 backend lowers the final `or` as 16-bit, dropping the
 * high word — the pre-existing [[qbe-loadc-wordsize-i8086]] wide-load-
 * from-narrow-stores family bug, independent of this ABI.  All
 * MicroPython structs returned by value are word/size_t-sized, so this
 * does not gate the port.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/sret_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/sret_probe/sret_probe.exe \
 *             | diff - minic/dos/tests/sret_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (medium + large).
 */

#include <stdio.h>
#include <stddef.h>

struct Pt { int x; int y; int z; };
struct Sm { int a; char b; };          /* 3 bytes -> byte tail */
struct Line { size_t bc; size_t line; }; /* mirrors mp_code_lineinfo_t */
union Tag { int i; struct Pt p; };

struct Pt
make_pt(int a, int b)
{
	struct Pt p;
	p.x = a;
	p.y = b;
	p.z = a * b;
	return p;
}

struct Sm
make_sm(int a, int b)
{
	struct Sm s;
	s.a = a;
	s.b = (char)b;
	return s;
}

struct Line
decode(int bc, int line)
{
	struct Line l;
	l.bc = (size_t)bc;
	l.line = (size_t)line;
	return l;
}

/* A struct-returning function that itself calls one. */
struct Pt
doubled(int a, int b)
{
	struct Pt p;
	p = make_pt(a, b);
	p.x = p.x * 2;
	p.y = p.y * 2;
	p.z = p.z * 2;
	return p;
}

union Tag
make_tag(int v)
{
	union Tag t;
	t.i = v;
	return t;
}

int
sum_pt(struct Pt *p)
{
	return p->x + p->y + p->z;
}

int
main(void)
{
	struct Pt q;
	struct Pt r = make_pt(5, 6);   /* decl initialized from a call */
	struct Sm s;
	struct Line l;
	union Tag t;
	int viay;

	q = make_pt(3, 4);
	printf("q=%d,%d,%d (want 3,4,12)\r\n", q.x, q.y, q.z);
	printf("r=%d,%d,%d (want 5,6,30)\r\n", r.x, r.y, r.z);

	viay = make_pt(10, 20).y;
	printf("viay=%d (want 20)\r\n", viay);

	q = doubled(7, 8);
	printf("dbl=%d,%d,%d (want 14,16,112)\r\n", q.x, q.y, q.z);

	s = make_sm(1000, 77);
	printf("sm=%d,%d (want 1000,77)\r\n", s.a, s.b);

	l = decode(42, 9);
	printf("line=%d,%d (want 42,9)\r\n", (int)l.bc, (int)l.line);

	t = make_tag(31337);
	printf("tag=%d (want 31337)\r\n", t.i);

	q = make_pt(1, 2);   /* z = 1*2 = 2; sum = 1+2+2 = 5 */
	printf("sum=%d (want 5)\r\n", sum_pt(&q));

	return 0;
}
