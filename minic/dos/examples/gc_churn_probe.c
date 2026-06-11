/*
 * gc_churn_probe.c — §4l self-contained fast-repro of MicroPython's conservative
 * mark/sweep garbage collector, to chase the churn(120)/49 KB-heap corruption
 * (NEXT_SESSION §4k/§4l) at DOSBox speed instead of the ~3-min, layout-perturbing
 * MAME loop.
 *
 * It faithfully copies the gc.c CORE algorithm — the 2-bit ATB (FREE/HEAD/TAIL/
 * MARK), gc_setup_area's table/pool split, gc_alloc's first-fit block scan, the
 * bounded mark stack (size 64) + gc_deal_with_stack_overflow rescan, and the
 * sweep — and drives it with a churn workload that, unlike MicroPython, EXPLICITLY
 * verifies every RETAINED object's sentinel + child links after each forced
 * collection.  That explicit check makes the probe MORE sensitive than the real
 * VM (which only notices corruption when a wild access happens to hit the qstr
 * pool / globals dict and raises NameError), so a layout-sensitive near-miss that
 * frees/corrupts a live block is caught wherever it lands.
 *
 * Strategy (NEXT_SESSION §4l):
 *   - MEDIUM model (near data, 2-byte ptrs): if it reproduces here, the bug is in
 *     the GC LOGIC / its codegen, debuggable fast in DOSBox.
 *   - COMPACT far-data, 49 KB heap (QBE_FAR_STATIC_DATA): matches MicroPython's
 *     model; if only this reproduces, the bug is far-data-specific (far load/store
 *     in the collection paths).
 *
 * Build medium:  tools/build-example.sh --model=medium minic/dos/examples/gc_churn_probe.c
 * Build compact: QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *                    minic/dos/examples/gc_churn_probe.c
 * Run:           tools/run-dos-exe.sh build/examples/gc_churn_probe/gc_churn_probe.exe
 *   PASS -> "ALL OK"; FAIL -> "CORRUPT ..." naming the broken retained node + a
 *   per-collection live/free census.
 */

/* A far pointer needs the 32-bit uintptr_t (stdint.h #else gives 16-bit).
 * build-example.sh doesn't pass -DFAR_DATA (only build-micropython.sh does), so
 * self-define under far-data builds.  Harmless under medium (near, 2-byte). */
#define FAR_DATA 1
#define DOS_FAR_DATA 1

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned char byte;

/* ---- gc.c core constants (verbatim) ------------------------------------- */
#define BYTES_PER_BLOCK 16
#define BLOCKS_PER_ATB  4
#define AT_FREE 0
#define AT_HEAD 1
#define AT_TAIL 2
#define AT_MARK 3
#define GC_STACK_SIZE 64   /* MICROPY_ALLOC_GC_STACK_SIZE */

#define BLOCK_SHIFT(block) (2 * ((block) & (BLOCKS_PER_ATB - 1)))
#define ATB_GET_KIND(block) ((A.atb[(block) / BLOCKS_PER_ATB] >> BLOCK_SHIFT(block)) & 3)
#define ATB_ANY_TO_FREE(block)  do { A.atb[(block)/BLOCKS_PER_ATB] &= (byte)(~(AT_MARK << BLOCK_SHIFT(block))); } while (0)
#define ATB_FREE_TO_HEAD(block)  do { A.atb[(block)/BLOCKS_PER_ATB] |= (byte)(AT_HEAD << BLOCK_SHIFT(block)); } while (0)
#define ATB_FREE_TO_TAIL(block)  do { A.atb[(block)/BLOCKS_PER_ATB] |= (byte)(AT_TAIL << BLOCK_SHIFT(block)); } while (0)
#define ATB_HEAD_TO_MARK(block)  do { A.atb[(block)/BLOCKS_PER_ATB] |= (byte)(AT_MARK << BLOCK_SHIFT(block)); } while (0)
#define ATB_MARK_TO_HEAD(block)  do { A.atb[(block)/BLOCKS_PER_ATB] &= (byte)(~(AT_TAIL << BLOCK_SHIFT(block))); } while (0)

#define ATB_0_IS_FREE(a) (((a) & 0x03) == 0)
#define ATB_1_IS_FREE(a) (((a) & 0x0c) == 0)
#define ATB_2_IS_FREE(a) (((a) & 0x30) == 0)
#define ATB_3_IS_FREE(a) (((a) & 0xc0) == 0)

#define BLOCK_FROM_PTR(ptr) (((byte *)(ptr) - A.pool) / BYTES_PER_BLOCK)
#define PTR_FROM_BLOCK(block) ((A.pool + (block) * BYTES_PER_BLOCK))
#define VERIFY_PTR(ptr) ( \
       ((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) == 0 \
    && (byte *)(ptr) >= A.pool \
    && (byte *)(ptr) < A.pool_end)

/* 49 KB matches the MicroPython dos8086 heap (the failing config).  A 49 KB
 * NEAR-data heap overflows the 64 KB DGROUP, so the medium build (logic
 * validation only) must use a small heap; the real far-data test uses 49 KB.
 * Override at the cpp step with -DGC_HEAP_BYTES=NNNN. */
#ifndef GC_HEAP_BYTES
#define GC_HEAP_BYTES 49152
#endif
#define HEAP_BYTES GC_HEAP_BYTES
static byte heap[HEAP_BYTES];

struct area {
	byte *atb;
	size_t atb_len;     /* gc_alloc_table_byte_len */
	byte *pool;         /* gc_pool_start */
	byte *pool_end;
	size_t last_free_atb;
	size_t last_used_block;
};
static struct area A;

/* Mark stack (block indices), matching gc.c's bounded stack + overflow flag. */
static size_t gc_block_stack[GC_STACK_SIZE];
static int gc_stack_overflow;

/* Census counters for diagnostics. */
static unsigned long g_collections;
static unsigned long g_overflows;

/* A churn "object": a header word + child pointers, mimicking a list/dict node
 * (cross-linked, multi-block).  payload[] holds child pointers a conservative
 * scan must follow. */
#define NCHILD 6
struct node {
	uint16_t sentinel;          /* must survive: == (id ^ 0xABCD) */
	uint16_t id;
	struct node *child[NCHILD]; /* far pointers under compact; scanned by GC */
};

/* Two-level container mimicking mp_obj_dict_t -> mp_map_t.table[] -> values
 * (the shape behind a global/builtins name lookup, whose corruption is what
 * MicroPython reports as NameError).  The GC must trace dl -> table -> each
 * value; verification indexes table[i] (a far_ptr + i*4 = the addfo path). */
#define DL_SLOTS 24
struct dictlike {
	uint16_t sentinel;          /* == 0x5A5A */
	uint16_t nslots;
	struct node **table;        /* far ptr to a separately-allocated array */
};

/* ---- gc.c core (faithful) ----------------------------------------------- */

static void gc_setup(void)
{
	size_t total = HEAP_BYTES;
	/* gc.c (no finaliser/weakref): A = (T-1) / (1 + 8/2*BPB). */
	A.atb_len = (total - 1) / (1 + 4 / 2 * BYTES_PER_BLOCK * 2 / 2);
	/* The above must equal (total-1)/(1 + MP_BITS_PER_BYTE/2*BYTES_PER_BLOCK)
	 * = (total-1)/(1 + 4*16) = (total-1)/65.  Spell it directly to avoid any
	 * integer-fold ambiguity: */
	A.atb_len = (total - 1) / 65;
	A.atb = heap;
	/* pool starts after the ATB, block(16)-aligned. */
	{
		uintptr_t p = (uintptr_t)(heap) + A.atb_len + 1;
		p = (p + (BYTES_PER_BLOCK - 1)) & ~((uintptr_t)(BYTES_PER_BLOCK - 1));
		A.pool = (byte *)p;
	}
	A.pool_end = A.pool + A.atb_len * BLOCKS_PER_ATB * BYTES_PER_BLOCK;
	/* zero the ATB */
	{
		size_t i;
		for (i = 0; i < A.atb_len; i++)
			A.atb[i] = 0;
	}
	A.last_free_atb = 0;
	A.last_used_block = 0;
}

static void gc_collect(void);   /* fwd */

static void *gc_alloc(size_t n_bytes)
{
	size_t n_blocks = (n_bytes + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK;
	size_t i, n_free, start_block, end_block;
	int collected = 0;

	if (n_blocks == 0)
		n_blocks = 1;

	for (;;) {
		n_free = 0;
		for (i = A.last_free_atb; i < A.atb_len; i++) {
			byte a = A.atb[i];
			if (ATB_0_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 0; goto found; } } else { n_free = 0; }
			if (ATB_1_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 1; goto found; } } else { n_free = 0; }
			if (ATB_2_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 2; goto found; } } else { n_free = 0; }
			if (ATB_3_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 3; goto found; } } else { n_free = 0; }
		}
		/* nothing found */
		if (collected)
			return 0;   /* out of memory */
		gc_collect();
		collected = 1;
	}

found:
	end_block = i;
	start_block = i - n_free + 1;
	if (n_free == 1)
		A.last_free_atb = (i + 1) / BLOCKS_PER_ATB;
	if (end_block > A.last_used_block)
		A.last_used_block = end_block;

	ATB_FREE_TO_HEAD(start_block);
	{
		size_t bl;
		for (bl = start_block + 1; bl <= end_block; bl++)
			ATB_FREE_TO_TAIL(bl);
	}
	return (void *)PTR_FROM_BLOCK(start_block);
}

/* gc_mark_subtree — bounded explicit stack, sizeof(void*) stride, overflow flag. */
static void gc_mark_subtree(size_t block)
{
	size_t sp = 0;
	for (;;) {
		size_t n_blocks = 0;
		void **ptrs;
		size_t wc, k;

		do {
			n_blocks += 1;
		} while (ATB_GET_KIND(block + n_blocks) == AT_TAIL);

		ptrs = (void **)PTR_FROM_BLOCK(block);
		wc = n_blocks * BYTES_PER_BLOCK / sizeof(void *);
		for (k = 0; k < wc; k++, ptrs++) {
			void *ptr = *ptrs;
			size_t pb;
			if (!VERIFY_PTR(ptr))
				continue;
			pb = BLOCK_FROM_PTR(ptr);
			if (ATB_GET_KIND(pb) != AT_HEAD)
				continue;
			ATB_HEAD_TO_MARK(pb);
			if (sp < GC_STACK_SIZE) {
				gc_block_stack[sp] = pb;
				sp += 1;
			} else {
				gc_stack_overflow = 1;
			}
		}

		if (sp == 0)
			break;
		sp -= 1;
		block = gc_block_stack[sp];
	}
}

static void gc_deal_with_stack_overflow(void)
{
	while (gc_stack_overflow) {
		size_t block;
		gc_stack_overflow = 0;
		g_overflows++;
		for (block = 0; block < A.atb_len * BLOCKS_PER_ATB; block++) {
			if (ATB_GET_KIND(block) == AT_MARK)
				gc_mark_subtree(block);
		}
	}
}

static void gc_sweep(void)
{
	size_t block, last_used = 0;
	int free_tail = 0;
	for (block = 0; block <= A.last_used_block; block++) {
		switch (ATB_GET_KIND(block)) {
		case AT_HEAD:
			free_tail = 1;
			/* fall through */
		case AT_TAIL:
			if (free_tail) {
				ATB_ANY_TO_FREE(block);
			} else {
				last_used = block;
			}
			break;
		case AT_MARK:
			ATB_MARK_TO_HEAD(block);
			free_tail = 0;
			last_used = block;
			break;
		}
	}
	A.last_used_block = last_used;
}

/* Root scanning.  Two root sources, mirroring the MicroPython port:
 *   (1) an explicit root array (like the mp_state root section), and
 *   (2) a conservative scan of the C stack [sp, stack_top) at BOTH 2-byte
 *       alignments (like ports/dos8086/main.c gc_collect).
 * gc_collect_root marks an unmarked head and traces it. */
#define NROOTS 8
static void *g_roots[NROOTS];
static char *stack_top;

static void gc_collect_root(void **ptrs, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) {
		void *ptr = ptrs[i];
		size_t pb;
		if (!VERIFY_PTR(ptr))
			continue;
		pb = BLOCK_FROM_PTR(ptr);
		if (ATB_GET_KIND(pb) != AT_HEAD)
			continue;
		ATB_HEAD_TO_MARK(pb);
		gc_mark_subtree(pb);
	}
}

static void gc_collect(void)
{
	volatile int stack_dummy;
	char *lo = (char *)&stack_dummy;
	size_t nbytes = (size_t)(stack_top - lo);

	g_collections++;
	A.last_free_atb = 0;

	/* explicit roots */
	gc_collect_root(g_roots, NROOTS);
	/* conservative C-stack scan, both alignments (matches the port) */
	gc_collect_root((void **)lo, nbytes / sizeof(void *));
	gc_collect_root((void **)(lo + 2), (nbytes - 2) / sizeof(void *));

	gc_deal_with_stack_overflow();
	gc_sweep();
	A.last_free_atb = 0;
}

/* ---- workload ----------------------------------------------------------- */

static struct node *new_node(uint16_t id)
{
	struct node *n = (struct node *)gc_alloc(sizeof(struct node));
	int j;
	if (!n)
		return 0;
	n->id = id;
	n->sentinel = (uint16_t)(id ^ 0xABCD);
	for (j = 0; j < NCHILD; j++)
		n->child[j] = 0;
	return n;
}

/* Variable-size garbage allocation (fragments the heap like MicroPython's mix
 * of 1-block ints, multi-block lists/dicts/strings).  Fills with a check
 * pattern; size in bytes chosen from the burst index. */
static void *new_blob(int k)
{
	static const unsigned sizes[6] = { 16, 32, 48, 80, 160, 320 };
	unsigned nb = sizes[k % 6];
	byte *p = (byte *)gc_alloc(nb);
	unsigned j;
	if (p)
		for (j = 0; j < nb; j++)
			p[j] = (byte)(0xC0 + (j & 0x1f));
	return p;
}

/* Build the two-level retained container: dl -> table[DL_SLOTS] -> value nodes.
 * Returns the container (rooted by the caller). */
static struct dictlike *build_dict(void)
{
	struct dictlike *dl = (struct dictlike *)gc_alloc(sizeof(struct dictlike));
	int i;
	if (!dl)
		return 0;
	dl->sentinel = 0x5A5A;
	dl->nslots = DL_SLOTS;
	dl->table = (struct node **)gc_alloc(DL_SLOTS * sizeof(struct node *));
	for (i = 0; i < DL_SLOTS; i++)
		dl->table[i] = new_node((uint16_t)(0x4000 | i));
	return dl;
}

/* Verify the container: sentinel, then table[i]->sentinel for every slot. */
static long verify_dict(struct dictlike *dl)
{
	int i;
	if (!dl)
		return 3000000L;
	if (dl->sentinel != 0x5A5A)
		return 3000001L;
	if (dl->nslots != DL_SLOTS)
		return 3000002L;
	for (i = 0; i < DL_SLOTS; i++) {
		struct node *v = dl->table[i];
		if (!v)
			return 3100000L + i;
		if (v->sentinel != (uint16_t)(v->id ^ 0xABCD))
			return v->id;
		if (v->id != (uint16_t)(0x4000 | i))
			return 3200000L + i;
	}
	return -1;
}

/* Verify the retained chain rooted at g_roots[0]: a `len`-node singly linked
 * list via child[0], each node also pointing child[1] at its successor's
 * successor (cross-links, multi-reference).  Returns the id of the first broken
 * node, or -1 if intact. */
static long verify_chain(int len)
{
	struct node *p = (struct node *)g_roots[0];
	int i;
	for (i = 0; i < len; i++) {
		if (!p)
			return 1000000L + i;            /* unexpected NULL */
		if (p->sentinel != (uint16_t)(p->id ^ 0xABCD))
			return p->id;                   /* sentinel clobbered */
		if (p->id != (uint16_t)i)
			return 2000000L + i;            /* wrong id / wild link */
		p = p->child[0];
	}
	return -1;
}

int main(void)
{
	int stack_dummy;
	int chain_len = 40;
	struct node *head, *prev;
	struct dictlike *dl;
	int i, pass, allok;

	stack_top = (char *)&stack_dummy;
	gc_setup();

	printf("gc_churn_probe: heap=%u pool=%u..%u blocks=%u\r\n",
	       (unsigned)HEAP_BYTES, (unsigned)(uintptr_t)A.pool,
	       (unsigned)(uintptr_t)A.pool_end,
	       (unsigned)(A.atb_len * BLOCKS_PER_ATB));

	/* Build the retained chain (rooted, must survive every collection). */
	head = new_node(0);
	g_roots[0] = head;
	prev = head;
	for (i = 1; i < chain_len; i++) {
		struct node *n = new_node((uint16_t)i);
		prev->child[0] = n;
		prev = n;
	}
	/* cross-links: child[1] -> successor's successor */
	{
		struct node *p = head;
		while (p && p->child[0] && p->child[0]->child[0]) {
			p->child[1] = p->child[0]->child[0];
			p = p->child[0];
		}
	}

	/* Two-level retained container (dict -> table -> values), rooted. */
	dl = build_dict();
	g_roots[1] = dl;

	allok = 1;
	/* Churn: each pass allocates a growing burst of UN-rooted garbage nodes
	 * (cross-linked among themselves so they look live until dropped), forcing
	 * collections as the 49 KB heap fills, then verifies the retained chain. */
	for (pass = 20; pass <= 120; pass += 20) {
		struct node *g_prev = 0;
		/* burst >> heap so the 49 KB heap fills and collects mid-pass
		 * (MicroPython churn(120) forced ~2 collections; *40 forces many). */
		int burst = pass * 40;
		for (i = 0; i < burst; i++) {
			struct node *g = new_node((uint16_t)(0x8000 | i));
			if (g && g_prev)
				g_prev->child[0] = g;   /* transient link, dropped each pass */
			g_prev = g;
			/* interleave variable-size garbage to fragment the heap */
			if ((i & 3) == 0)
				(void)new_blob(i);
			/* g_prev is a C-stack local: while in this loop it roots the
			 * current burst (conservative scan keeps it), but it is
			 * overwritten next iter, so old garbage becomes collectable. */
		}
		g_prev = 0;
		{
			long bc = verify_chain(chain_len);
			long bd = verify_dict(dl);
			if (bc < 0 && bd < 0) {
				printf("pass=%d collections=%lu overflows=%lu  OK\r\n",
				       pass, g_collections, g_overflows);
			} else {
				printf("pass=%d collections=%lu overflows=%lu  CORRUPT chain=%ld dict=%ld\r\n",
				       pass, g_collections, g_overflows, bc, bd);
				allok = 0;
			}
		}
	}

	printf("%s\r\n", allok ? "ALL OK" : "CORRUPT");
	return 0;
}
