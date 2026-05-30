/*
 * tentative_def_probe.c -- tentative-definition fix needed by py/objdict.c
 * (MicroPython port; see NEXT_SESSION.md).
 *
 * MicroPython forward-declares a const type object and then defines it
 * later in the same translation unit:
 *
 *     static const mp_obj_type_t mp_type_dict_view_it;   // tentative
 *     ...
 *     static const mp_obj_type_t mp_type_dict_view_it =   // real
 *         { .base = { ... }, ... };
 *
 * minic buffers file-scope globals in ini[]/gloname[] and emits them at
 * end of translation, so the initialized definition can reuse the slot
 * that the tentative declaration reserved.  Previously the second
 * definition errored with "double definition".
 *
 * This probe exercises the struct/aggregate form (objdict's actual
 * idiom): a forward (tentative) declaration, then the real initialized
 * definition, with member reads and address-of at runtime.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/tentative_def_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/tentative_def_probe/tentative_def_probe.exe \
 *             | diff - minic/dos/tests/tentative_def_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

struct point { int x; int y; int tag; };

/* (1) Tentative aggregate definition, completed below. */
static const struct point origin;

/* (2) The real definition supersedes the tentative one. */
static const struct point origin = { 11, 22, 33 };

int
main(void)
{
	const struct point *p = &origin;
	printf("origin=%d,%d,%d\r\n", origin.x, origin.y, origin.tag);  /* 11,22,33 */
	printf("via_ptr=%d,%d,%d\r\n", p->x, p->y, p->tag);             /* 11,22,33 */
	printf("sum=%d\r\n", origin.x + origin.y + origin.tag);         /* 66 */
	return 0;
}
