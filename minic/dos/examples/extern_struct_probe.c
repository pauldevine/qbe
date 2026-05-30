/*
 * extern_struct_probe.c -- two parse fixes needed by py/modsys.c
 * (MicroPython port, §1i; see NEXT_SESSION.md).
 *
 *   1. `extern struct TAG name;` where TAG has not been seen yet now
 *      forward-declares the (incomplete) tag instead of dying with
 *      "unknown struct type".  MicroPython spells
 *        extern struct _mp_dummy_t mp_sys_stdin_obj;
 *      for opaque objects defined in another translation unit.  The
 *      same fix covers `extern struct TAG name[];` and
 *      `extern struct TAG *name;`.
 *
 *   2. A bare `;` at file scope is accepted as an empty declaration
 *      (was a parse error).  MicroPython's MP_REGISTER_MODULE(...)
 *      macro expands to nothing followed by a trailing `;`.
 *
 * Here the forward-declared tag is completed and the object defined in
 * the same TU so the probe links and runs, exercising address-of and
 * member reads through the forward-referenced declaration.
 *
 * Build:  tools/build-example.sh --model=medium \
 *             minic/dos/examples/extern_struct_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/extern_struct_probe/extern_struct_probe.exe \
 *             | diff - minic/dos/tests/extern_struct_probe.golden.txt
 *
 * Frontend-only / near-far-agnostic: runs under medium + large.
 */

#include <stdio.h>

/* (1) extern references before the tag is defined -> forward-declare;
 * covers the plain, the `[]`, and the `*` extern forms. */
extern struct thing the_thing;
extern struct thing the_table[];
extern struct thing *the_ptr;

;  /* (2) bare top-level empty declaration */

struct thing { int v; int w; };

struct thing the_thing = { 42, 7 };
struct thing the_table[] = { { 1, 2 }, { 3, 4 } };

;  /* (2) another empty declaration after definitions */

int
main(void)
{
	struct thing *p = &the_thing;
	printf("the_thing=%d,%d\r\n", p->v, p->w);            /* 42,7 */
	printf("table=%d,%d\r\n", the_table[1].v, the_table[1].w); /* 3,4 */
	printf("sum=%d\r\n", the_thing.v + the_thing.w);      /* 49 */
	return 0;
}
