/*
 * local_typedef_probe.c -- function-local typedef of a function-pointer
 * type, needed by py/stream.c (MicroPython port; see NEXT_SESSION.md).
 *
 * minic's typedefs were file-scope only.  py/stream.c declares one inside
 * a function body:
 *   typedef mp_uint_t (*io_func_t)(mp_obj_t, void *, mp_uint_t, int *);
 *   io_func_t io_func;
 * A new `dcls` production accepts the function-pointer typedef form in
 * block scope (the name goes into minic's single global typedef table).
 *
 * Exercises runtime: declare the fnptr typedef locally, pick one of two
 * functions through it based on a flag, and call through the pointer.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/local_typedef_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/local_typedef_probe/local_typedef_probe.exe \
 *             | diff - minic/dos/tests/local_typedef_probe.golden.txt
 *
 * Frontend-only / model-agnostic: runs under medium + large.
 */

#include <stdio.h>

static int
do_add(int a, int b)
{
	return a + b;
}

static int
do_mul(int a, int b)
{
	return a * b;
}

static int
apply(int flag, int x, int y)
{
	/* Function-local typedef of a function-pointer type, then a
	 * variable of that type chosen at runtime and called through. */
	typedef int (*binop_t)(int a, int b);
	binop_t op;

	if (flag)
		op = do_mul;
	else
		op = do_add;
	return op(x, y);
}

int
main(void)
{
	printf("add=%d\r\n", apply(0, 6, 7));   /* 13 */
	printf("mul=%d\r\n", apply(1, 6, 7));   /* 42 */
	return 0;
}
