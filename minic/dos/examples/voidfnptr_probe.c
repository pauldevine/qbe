/*
 * voidfnptr_probe.c -- Phase 1 void-returning-function-pointer gate.
 *
 * Pins the void-returning function pointer support added 2026-05-29
 * (see NEXT_SESSION.md / MICROPYTHON_PORT.md).  Before this, minic
 * rejected every `void (*fp)(...)` declarator with "invalid void
 * function pointer" — a function pointer whose return type is void is
 * perfectly legal C and is the #2 MicroPython spike blocker (18 files):
 * `mp_reader_t::close`, the `mp_obj_type_t` slots, etc.
 *
 * Also pins the companion codegen fix for function-pointer STRUCT
 * members: SIZE() now sizes a pointer-to-function with CODEPTR_SZ()
 * (4 bytes under far code) instead of DATAPTR_SZ() (2 under near data),
 * so a fn-ptr member and the member after it no longer overlap.  The
 * struct below is the exact shape of MicroPython's mp_reader_t.
 *
 * Exercises (codegen, not just parse):
 *   1. A standalone `void (*)(void)` variable, called for effect.
 *   2. A void-returning fn-ptr STRUCT member, called through `s.close(s.data)`.
 *   3. A non-void fn-ptr member adjacent to the void one — both reachable,
 *      proving the layout no longer overlaps (the SIZE fix).
 *   4. sizeof() of the mp_reader_t-shaped struct (2 + 4 + 4 = 10).
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/voidfnptr_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/voidfnptr_probe/voidfnptr_probe.exe \
 *             | diff - minic/dos/tests/voidfnptr_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

typedef unsigned int mp_uint_t;

/* Exact shape of MicroPython's mp_reader_t. */
struct rdr {
	void *data;
	mp_uint_t (*readbyte)(void *d);   /* non-void fn-ptr member */
	void (*close)(void *d);           /* void-returning fn-ptr member */
};

static int closed_flag;

static mp_uint_t my_readbyte(void *d)
{
	int *counter = (int *)d;
	return (mp_uint_t)((*counter)++);
}

static void my_close(void *d)
{
	closed_flag = 99;
}

static void bump(void)
{
	closed_flag = closed_flag + 1;
}

int main()
{
	struct rdr r;
	int counter;
	void (*fp)(void) = bump;   /* Feature 1: standalone void fn-ptr var */

	/* Feature 1: call a void fn-ptr variable for effect. */
	closed_flag = 0;
	fp();
	fp();
	printf("var=%d (want 2)\r\n", closed_flag);

	/* Feature 3: both members reachable, no overlap.  readbyte returns a
	 * value and advances the counter; close fires for effect. */
	counter = 40;
	r.data = &counter;
	r.readbyte = my_readbyte;
	r.close = my_close;

	printf("rb=%d,%d (want 40,41)\r\n",
	    (int)r.readbyte(r.data), (int)r.readbyte(r.data));

	/* Feature 2: call the void-returning fn-ptr member. */
	closed_flag = 0;
	r.close(r.data);
	printf("close=%d (want 99)\r\n", closed_flag);

	/* Feature 4: SIZE() of the fn-ptr members no longer underflows. */
	printf("size=%d (want 10)\r\n", (int)sizeof(struct rdr));

	return 0;
}
