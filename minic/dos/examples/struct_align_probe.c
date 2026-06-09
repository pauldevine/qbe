/*
 * struct_align_probe.c — far-data natural alignment of 4-byte struct
 * members (§4g).  Pins the minic ABI change that makes MicroPython's
 * conservative GC work as-designed.
 *
 * THE BUG IT GUARDS (far-data only; NEAR_DATA models are byte-identical
 * and stay PACKED, so this probe is compact/large/huge):
 *   minic used to lay out struct members fully PACKED (no alignment
 *   padding: m->offset = size; size += SIZE).  Under far-data a far
 *   pointer / mp_obj_t is 4 bytes but a short/size_t is 2, so a pointer
 *   that follows a 2-byte field landed at a 2-mod-4 byte offset.
 *   MicroPython's conservative collector scans memory in sizeof(void*)=4
 *   strides, so such a pointer was split across two scan words and NEVER
 *   recognised -> the block it roots was freed while live (the §4d/§4e/§4f
 *   churn corruption).  §4f worked around it in the scanner (2-byte stride
 *   + a dual-aligned mp_state rescan); §4g fixes the root cause by
 *   4-byte-aligning 4-byte members so the sizeof(void*)-strided scan finds
 *   every pointer at a stride boundary.
 *
 * Bug-loud against the UNFIXED compiler: off_child prints 2 (golden 4),
 * and scan_obj/scan_arr report 0 hits (golden 1/3) because the stride scan
 * skips the 2-mod-4 pointer.
 *
 * Build:  tools/build-example.sh --model=compact \
 *             minic/dos/examples/struct_align_probe.c
 * Verify: diff <(tools/run-dos-exe.sh \
 *              build/examples/struct_align_probe/struct_align_probe.exe) \
 *              minic/dos/tests/struct_align_probe.golden.txt
 */

#include <stdio.h>
#include <stddef.h>

/* A 2-byte tag forces the following far pointers to a 2-mod-4 offset
 * when packed.  This is the mp_obj/qstr_pool shape that defeated the GC. */
struct gcobj {
	unsigned short tag;
	void *child;
	void *child2;
};

/* Nested: the inner struct's alignment (4, because it holds a pointer)
 * must propagate so `in` is placed at a 4-aligned offset in the outer. */
struct inner { unsigned short a; void *p; };
struct outer { char c; struct inner in; };

/* Regression: a pointer-free struct has alignment 1 and must stay PACKED
 * (byte-identical to the old layout) even under far-data. */
struct small { char a; unsigned short b; char c; };

/* Conservative sizeof(void*)-stride scan, exactly like py/gc.c. */
static int scan_count(void *base, unsigned nbytes, void *target)
{
	char *p = (char *)base;
	unsigned off;
	int n = 0;
	for (off = 0; off + sizeof(void *) <= nbytes; off += sizeof(void *))
		if (*(void **)(p + off) == target)
			n++;
	return n;
}

int main(void)
{
	long val = 0x12345678L;
	long val2 = 0x2468ACE0L;
	struct gcobj obj;
	struct outer o;
	struct gcobj arr[3];
	int i, allok;

	/* 1. a far-ptr member after a 2-byte field is 4-aligned, and the
	 * struct is tail-padded so an array of it keeps every element aligned. */
	printf("off_child=%d\r\n", (int)offsetof(struct gcobj, child));
	printf("off_child2=%d\r\n", (int)offsetof(struct gcobj, child2));
	printf("sizeof_gcobj=%d\r\n", (int)sizeof(struct gcobj));

	/* 2. the GC-style stride scan finds both pointers (1 hit each). */
	obj.tag = 7;
	obj.child = &val;
	obj.child2 = &val2;
	if (scan_count(&obj, sizeof(obj), &val) == 1 &&
	    scan_count(&obj, sizeof(obj), &val2) == 1)
		printf("scan_obj ok\r\n");
	else
		printf("scan_obj FAIL\r\n");

	/* 3. nested struct: the inner pointer stays 4-aligned in the outer. */
	printf("off_outer_p=%d\r\n", (int)offsetof(struct outer, in.p));
	o.c = 'x';
	o.in.a = 9;
	o.in.p = &val;
	if (scan_count(&o, sizeof(o), &val) == 1)
		printf("scan_outer ok\r\n");
	else
		printf("scan_outer FAIL\r\n");

	/* 4. an array of pointer-bearing structs: one sweep over the whole
	 * array finds the pointer in EVERY element (each child at a stride). */
	for (i = 0; i < 3; i++) {
		arr[i].tag = (unsigned short)i;
		arr[i].child = &val;
		arr[i].child2 = &val2;
	}
	if (scan_count(arr, sizeof(arr), &val) == 3 &&
	    (sizeof(arr) % sizeof(void *)) == 0)
		printf("scan_arr ok\r\n");
	else
		printf("scan_arr FAIL\r\n");

	/* 5. regression: the pointer-free struct stays packed (no padding). */
	printf("off_small_b=%d\r\n", (int)offsetof(struct small, b));
	printf("off_small_c=%d\r\n", (int)offsetof(struct small, c));
	printf("sizeof_small=%d\r\n", (int)sizeof(struct small));

	/* 6. member access uses the new aligned offsets consistently. */
	allok = obj.tag == 7
	    && *(long *)obj.child == 0x12345678L
	    && *(long *)obj.child2 == 0x2468ACE0L
	    && o.in.a == 9
	    && *(long *)o.in.p == 0x12345678L
	    && arr[2].child == &val;
	if (allok)
		printf("readback ok\r\n");
	else
		printf("readback FAIL\r\n");

	return 0;
}
