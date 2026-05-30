/*
 * mp_designated_array_probe.c -- struct/union array initializers with
 * per-element designated members (MicroPython port, §1i; see
 * NEXT_SESSION.md / MICROPYTHON_PORT.md).
 *
 * Before this work, `T arr[] = { {.f=v}, {.f=v} }` parse-errored: the
 * struct-array init path used a flat round-robin (sai_*) item list that
 * could not express `.field =` designators inside an element, and the
 * block-scope `static T arr[] = {...}` path shared it.  Both file-scope
 * and function-local `static` array initializers now route through the
 * generic aggregate machinery (agg_emit_array -> agg_emit_struct), so:
 *
 *   1. File-scope struct array, per-element designated members, with
 *      unspecified members zero-filled -- `{ {.a=1}, {.a=2,.c=6} }`.
 *
 *   2. Mixed positional + designated elements in one array.
 *
 *   3. Function-local `static` struct array whose element carries a
 *      *union* member initialised by a designator -- the MicroPython
 *      `static const mp_arg_t allowed_args[] = { {q, f, {.u_rom_obj=…}} }`
 *      idiom (objlist/modbuiltins).  The union is emitted as its
 *      designated member's value, zero-filled to the union footprint.
 *
 *   4. The `sizeof(arr) / sizeof(arr[0])` element-count idiom over these
 *      tables (one division per printf -- see the two-div note below).
 *
 *   5. Sized local static struct array with a zero-filled trailing
 *      element -- `static struct point sp[3] = { {1,2,3}, {4,5,6} }`.
 *
 * Layout is model-agnostic (no pointers in the union, so sizes match
 * across medium/large): unsigned short = 2 bytes, long = 4 bytes on
 * i8086.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/mp_designated_array_probe.c
 * Verify: tools/run-dos-exe.sh \
 *             build/examples/mp_designated_array_probe/mp_designated_array_probe.exe \
 *             | diff - minic/dos/tests/mp_designated_array_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under medium + large (frontend-only,
 * near/far-agnostic).
 */

#include <stdio.h>

struct point { int a; int b; int c; };  /* 6 bytes (i8086 int = 2) */

/* (1)+(2) file-scope struct array: designated, designated-with-gap,
 * and a plain positional element. */
static const struct point fp[] = {
	{ .a = 1 },           /* {1, 0, 0} */
	{ .a = 2, .c = 6 },   /* {2, 0, 6} */
	{ 7, 8, 9 },          /* {7, 8, 9} */
};

/* (3) union-bearing struct, MicroPython mp_arg_t shape. */
typedef union { unsigned short u_flag; long u_val; } uarg_t;  /* 4 bytes */
struct arg { unsigned short qst; unsigned short flags; uarg_t def; };  /* 8 */

int
main(void)
{
	/* (3) function-local static array with per-element union
	 * designators. */
	static const struct arg aa[] = {
		{ 5, 0x12, { .u_val = 1000 } },
		{ 7, 0x11, { .u_flag = 3 } },
	};
	/* (5) sized local static struct array, trailing element zeroed. */
	static struct point sp[3] = {
		{ 1, 2, 3 },
		{ 4, 5, 6 },
	};
	int i;

	/* (1)+(2) read back file-scope table. */
	for (i = 0; i < 3; i++)
		printf("fp[%d]=%d,%d,%d\r\n", i, fp[i].a, fp[i].b, fp[i].c);
	/* want: 1,0,0 / 2,0,6 / 7,8,9 */

	/* (3) union members. */
	printf("aa0 q=%d f=%d val=%ld\r\n",
	       aa[0].qst, aa[0].flags, aa[0].def.u_val);     /* 5 18 1000 */
	printf("aa1 q=%d f=%d flag=%d\r\n",
	       aa[1].qst, aa[1].flags, aa[1].def.u_flag);     /* 7 17 3 */

	/* (4) count idiom (one division each). */
	printf("n_fp=%d (want 3)\r\n", (int)(sizeof(fp) / sizeof(fp[0])));
	printf("n_aa=%d (want 2)\r\n", (int)(sizeof(aa) / sizeof(aa[0])));
	printf("sz_arg=%d (want 8)\r\n", (int)sizeof(struct arg));

	/* (5) sized static array, third element zero-filled. */
	for (i = 0; i < 3; i++)
		printf("sp[%d]=%d,%d,%d\r\n", i, sp[i].a, sp[i].b, sp[i].c);
	/* want: 1,2,3 / 4,5,6 / 0,0,0 */

	return 0;
}
