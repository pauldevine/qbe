/*
 * gc_bigheap_probe.c — regression guard for §4i: far-pointer offset
 * arithmetic (`far_ptr + idx`) for an in-segment byte offset >= 0x8000.
 *
 * STATUS: GATED (compact + large) in tools/test-dos.sh.  It was the repro for
 * the §4h bug and is now the guard for the §4i fix.
 *
 * THE BUG (§4h, fixed in §4i):
 *   On i8086 far-data, minic USED to lower `far_ptr + index` (variable index)
 *   as a FLAT 32-bit add of a SIGN-EXTENDED index (minic.y Scale path always
 *   `sext`s a sub-long index before scaling).  When the index is an UNSIGNED
 *   byte offset >= 0x8000 (top bit of the 16-bit offset set), extsw makes it
 *   negative, so `ptr + off` became a wild address BELOW the object.
 *   MicroPython's gc_alloc returns exactly this shape —
 *   `gc_pool_start + start_block * BYTES_PER_BLOCK` — so on a >32 KB heap any
 *   block in the upper half (start_block >= 2048) was handed back at a bogus
 *   address => heap corruption.  This is why the 49 KB dos8086 heap corrupted
 *   under churn while a 16 KB heap (blocks < 1024, offsets < 0x4000) was clean.
 *
 * THE FIX (§4i): a far pointer's segment is fixed per object (objects <= 64 KB)
 *   and its 16-bit offset wraps within the segment, so `far_ptr ± idx` must add
 *   the index to the OFFSET word only, segment preserved — emitted as the
 *   dedicated `addfo`/`subfo` backend ops (offset-only, 16-bit wraparound).
 *   That is correct for BOTH a true large offset (>= 0x8000) AND a 16-bit
 *   wrapped "negative" size_t delta, which neither extsw nor extuw of a flat
 *   32-bit add can handle simultaneously.  See [[project-far-ptr-unsigned-index-bug]].
 *
 *   What this probe shows: rt (BLOCK_FROM_PTR, far-ptr DIFFERENCE) and vp
 *   (VERIFY_PTR, far-ptr COMPARE) round-trip correctly for every block;
 *   `direct` (= pool[off], i.e. far_ptr + unsigned index) is now also correct
 *   for off >= 0x8000 (b >= 2048).
 *
 * HUGE is NOT gated here: under --model=huge, `far_ptr ± idx` does NOT use
 *   addfo/subfo — objects can exceed 64 KB so a genuine segment carry is
 *   needed, and minic routes it through huge_ptr_binop -> _qbe_huge_add
 *   (a normalising libstub helper).  This probe still FAILS under huge because
 *   that helper has its own pre-existing >= 0x8000 gap (orthogonal to §4i, which
 *   does not touch the huge path; huge codegen is byte-identical before/after).
 *
 * Build:  QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *             minic/dos/examples/gc_bigheap_probe.c
 *         (QBE_FAR_STATIC_DATA routes the 48 KB pool into a far segment so it
 *          spans past offset 0x8000.)
 * Run:    tools/run-dos-exe.sh \
 *             build/examples/gc_bigheap_probe/gc_bigheap_probe.exe
 *   FIXED -> every line "...direct=<0x41+i>" + "ALL OK".
 *   BUGGY -> b>=2048 lines show the wrong/zero `direct` byte + "FAIL".
 */

/* build-example.sh does NOT pass -DFAR_DATA (only build-micropython.sh does),
 * so self-define it to get the 32-bit uintptr_t a far pointer needs — matching
 * how MicroPython is actually compiled.  (A harness gap worth fixing; see §4h.) */
#define FAR_DATA 1
#define DOS_FAR_DATA 1

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned char byte;

#define BYTES_PER_BLOCK 16

/* Mirror mp_state_mem_area_t's pointer-bearing shape: far-pointer members
 * loaded from a struct and used in pointer arithmetic. */
struct area {
	byte *pool_start;
	size_t byte_len;
	byte *pool_end;
};

/* py/gc.c macros, verbatim. */
#define BLOCK_FROM_PTR(a, ptr) (((byte *)(ptr) - (a)->pool_start) / BYTES_PER_BLOCK)
#define PTR_FROM_BLOCK(a, block) (((block) * BYTES_PER_BLOCK + (uintptr_t)(a)->pool_start))
#define VERIFY_PTR(a, ptr) ( \
	((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) == 0 \
	&& (void *)(ptr) >= (void *)(a)->pool_start \
	&& (void *)(ptr) < (void *)(a)->pool_end)

/* A pool that spans past offset 0x8000 within its far segment. */
static byte pool[48000];

int main(void)
{
	struct area a;
	size_t blocks[8];
	int nb, i, allok;

	a.pool_start = pool;
	a.byte_len = sizeof(pool);
	a.pool_end = pool + sizeof(pool);

	/* blocks straddling 0x8000 (block 2048 -> offset 32768) */
	blocks[0] = 0;
	blocks[1] = 1;
	blocks[2] = 100;
	blocks[3] = 2047;
	blocks[4] = 2048;
	blocks[5] = 2049;
	blocks[6] = 2500;
	blocks[7] = 2999;
	nb = 8;

	allok = 1;
	for (i = 0; i < nb; i++) {
		size_t b = blocks[i];
		size_t off = b * BYTES_PER_BLOCK;
		byte *p = (byte *)PTR_FROM_BLOCK(&a, b);
		byte expect = (byte)(0x41 + i);
		size_t rt;
		int vp;

		*p = expect;                  /* write via computed far ptr */
		rt = BLOCK_FROM_PTR(&a, p);   /* ptr -> block round-trip */
		vp = VERIFY_PTR(&a, p);
		printf("b=%u off=%u rt=%u vp=%d direct=%d\r\n",
		       (unsigned)b, (unsigned)off, (unsigned)rt, vp,
		       (int)pool[off]);
		if (rt != b || vp != 1 || pool[off] != expect)
			allok = 0;
	}
	printf("%s\r\n", allok ? "ALL OK" : "FAIL");
	return 0;
}
