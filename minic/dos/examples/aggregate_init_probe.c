/*
 * aggregate_init_probe.c -- Phase 1 §1b aggregate / designated
 * file-scope initializer gate.
 *
 * Pins the `T NAME = { ... };` struct/union/global initializer feature
 * added 2026-05-29 (see NEXT_SESSION.md / MICROPYTHON_PORT.md).  Before
 * this, even a local `struct P g = { .a = 1 };` parse-errored; minic had
 * no struct/global aggregate-initializer support.  This was the standing
 * §1b pause point and the MicroPython `mp_obj_type_t` object-file
 * blocker (the `.base = { ... }, .flags = ..., .slots = { ... }` cluster).
 *
 * Exercises layout + the constant-initializer folder (codegen, not just
 * parse):
 *   1. Designated `.field = value`, out of declaration-adjacency.
 *   2. Nested braces for a struct member (`.base = { &g }`), with a
 *      pointer field relocated to a global's address (read back via deref).
 *   3. An array member partially initialized (`.slots = { 11, 22, 33 }`)
 *      with the trailing element zero-filled.
 *   4. A nested struct member (`.pt = { 7, 9 }`).
 *   5. A string-literal pointer field (`.label = "hi"`).
 *   6. Constant-folded value with a cast and bit-or (`.flags = 0x8|0x1`).
 *   7. Sequential (non-designated) init of a plain struct.
 *   8. Partial init: only one field set, the rest auto-zero.
 *   9. Union init (first member), padded to the union's size.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/aggregate_init_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/aggregate_init_probe/aggregate_init_probe.exe \
 *             | diff - minic/dos/tests/aggregate_init_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

struct base { int *type; };
struct point { int x; int y; };

struct objtype {
	struct base base;
	unsigned int flags;
	int name;
	int slots[4];
	struct point pt;
	char *label;
};

int g_type = 1234;

const struct objtype T = {
	.base = { &g_type },
	.flags = (unsigned int)(0x8 | 0x1),
	.name = 42,
	.slots = { 11, 22, 33 },
	.pt = { 7, 9 },
	.label = "hi",
};

/* Sequential (non-designated). */
struct point P = { 100, 200 };

/* Partial: only .name set, the rest auto-zero. */
struct objtype Q = { .name = 5 };

/* Union: first member, padded to the wider member's size. */
union ival { int i; long l; };
union ival U = { 0x1234 };

int
main(void)
{
	printf("base=%d (want 1234)\r\n", *T.base.type);
	printf("flags=%d name=%d (want 9,42)\r\n", (int)T.flags, T.name);
	printf("slots=%d,%d,%d,%d (want 11,22,33,0)\r\n",
	       T.slots[0], T.slots[1], T.slots[2], T.slots[3]);
	printf("pt=%d,%d (want 7,9)\r\n", T.pt.x, T.pt.y);
	printf("label=%s (want hi)\r\n", T.label);
	printf("P=%d,%d (want 100,200)\r\n", P.x, P.y);
	printf("Q.name=%d Q.flags=%d (want 5,0)\r\n", Q.name, (int)Q.flags);
	printf("U=%d (want 4660)\r\n", U.i);
	return 0;
}
