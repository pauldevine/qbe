/*
 * nested_member_probe.c -- Phase 1 nested-aggregate-member gate.
 *
 * Pins the nested NAMED struct/union member feature added 2026-05-29
 * (see NEXT_SESSION.md / MICROPYTHON_PORT.md).  Before this, only a
 * truly-anonymous C11 member (`union { ... };`, members promote into the
 * parent) parsed; a nested aggregate DEFINITION given a member name
 * (`union { ... } fun;` / `struct { ... } pt;`) did not.  This was the
 * universal MicroPython blocker (114/132 files) at py/obj.h:2973.
 *
 * Exercises layout + aliasing (codegen, not just parse):
 *   1. Named nested `struct { ... } pt;` — independent members at the
 *      right offsets.
 *   2. Named nested `union { ... } u;` — members alias (write the wide
 *      member, read it back through the narrow one).
 *   3. A named nested struct that itself contains a named nested union
 *      (exercises the curstruct save/restore stack to depth 2).
 *   4. Regression: a truly-anonymous nested union still promotes its
 *      members into the parent.
 *   5. sizeof(outer) reflects all of the above.
 *
 * Build:  tools/build-example.sh --model=medium minic/dos/examples/nested_member_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/nested_member_probe/nested_member_probe.exe \
 *             | diff - minic/dos/tests/nested_member_probe.golden.txt
 *
 * Wired into tools/test-dos.sh under the medium runtime section.
 */

#include <stdio.h>

struct outer {
	int tag;
	struct {            /* Feature 1: named nested struct */
		int x;
		int y;
	} pt;
	union {             /* Feature 2: named nested union (members alias) */
		unsigned int word;
		unsigned char byte[2];
	} u;
	struct {            /* Feature 3: named nested struct with nested union */
		int kind;
		union {
			int i;
			short s;
		} v;
	} grp;
	union {             /* Feature 4: anonymous union -> promote into outer */
		int promoted;
		short promoted_lo;
	};
};

int main()
{
	struct outer o;

	/* Feature 1: the nested struct's members are independent. */
	o.pt.x = 10;
	o.pt.y = 20;
	o.tag = 7;
	printf("pt=%d,%d tag=%d (want 10,20,7)\r\n", o.pt.x, o.pt.y, o.tag);

	/* Feature 2: union members alias.  0x4241 little-endian -> byte[0]=0x41
	 * ('A'=65), byte[1]=0x42 ('B'=66). */
	o.u.word = 0x4241;
	printf("u=%d,%d (want 65,66)\r\n", o.u.byte[0], o.u.byte[1]);

	/* Feature 3: nested-in-nested named members reach the right slot. */
	o.grp.kind = 3;
	o.grp.v.i = 1000;
	printf("grp=%d,%d (want 3,1000)\r\n", o.grp.kind, o.grp.v.s);

	/* Feature 4: the anonymous union promotes; write wide, read narrow. */
	o.promoted = 0x00FF;
	printf("anon=%d (want 255)\r\n", o.promoted_lo);

	/* Feature 5: sizeof reflects layout.  tag(2) + pt(4) + u(2) + grp(4) +
	 * anon(2) = 14. */
	printf("size=%d (want 14)\r\n", (int)sizeof(struct outer));

	return 0;
}
