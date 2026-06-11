/*
 * gc_offset_probe.c — §4m STATIC audit (no GC, no perturbation): does every
 * far-POINTER field in MicroPython's live heap object types land at a
 * sizeof(void*)-aligned (4-mod-0) offset under the compact/large far-data ABI?
 *
 * MicroPython's conservative collector scans a heap object's words at a
 * sizeof(void*) = 4 stride from its (16-aligned) block start, so a pointer field
 * at a 2-mod-4 offset is SPLIT across two reads and never recognised → the block
 * it roots is freed while live (the §4d/§4e/§4f bug class).  §4g made minic
 * 4-byte-align 4-byte struct members under far-data to fix this; this probe
 * VERIFIES that the alignment actually lands every pointer field on a 4-stride
 * boundary in the types behind a global/builtins name lookup (whose corruption
 * MicroPython reports as NameError — the §4k/§4l churn(120) symptom).
 *
 * The struct definitions are copied VERBATIM from the preprocessed
 * build/mp-link/gc.pp.c so the layout matches the real build exactly.
 *
 * Build:  QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *             minic/dos/examples/gc_offset_probe.c
 * Run:    tools/run-dos-exe.sh build/examples/gc_offset_probe/gc_offset_probe.exe
 *   Each pointer field prints "off=N a4=1" (4-aligned, GC-visible) or
 *   "off=N a4=0  *** NOT 4-ALIGNED — GC SCAN MISSES IT ***".  Final ALL-4-ALIGNED
 *   or MISALIGNED.
 */
#define FAR_DATA 1
#define DOS_FAR_DATA 1

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* ---- minimal MicroPython types (matching obj.h under this config) -------- */
typedef void *mp_obj_t;
typedef const void *mp_const_obj_t;
typedef struct _mp_obj_type_t mp_obj_type_t;     /* opaque; we only point at it */
typedef struct _mp_obj_base_t { const mp_obj_type_t *type; } mp_obj_base_t;
typedef uint8_t qstr_len_t;

/* ---- struct defs VERBATIM from build/mp-link/gc.pp.c --------------------- */
typedef struct _qstr_pool_t {
	const struct _qstr_pool_t *prev;
	size_t total_prev_len : (8 * sizeof(size_t) - 1);
	size_t is_sorted : 1;
	size_t alloc;
	size_t len;
	qstr_len_t *lengths;
	const char *qstrs[];
} qstr_pool_t;

typedef struct _mp_map_elem_t {
	mp_obj_t key;
	mp_obj_t value;
} mp_map_elem_t;

typedef struct _mp_map_t {
	size_t all_keys_are_qstrs : 1;
	size_t is_fixed : 1;
	size_t is_ordered : 1;
	size_t used : (8 * sizeof(size_t) - 3);
	size_t alloc;
	mp_map_elem_t *table;
} mp_map_t;

typedef struct _mp_obj_dict_t {
	mp_obj_base_t base;
	mp_map_t map;
} mp_obj_dict_t;

typedef struct _mp_obj_list_t {
	mp_obj_base_t base;
	size_t alloc;
	size_t len;
	mp_obj_t *items;
} mp_obj_list_t;

/* The stackless VM frame (heap-allocated): holds the live value stack state[].
 * Verbatim from build/mp-link/bc.pp.c. */
typedef uint8_t byte;
struct _mp_obj_fun_bc_t;
typedef struct _mp_code_state_t {
	struct _mp_obj_fun_bc_t *fun_bc;
	const byte *ip;
	mp_obj_t *sp;
	uint16_t n_state;
	uint16_t exc_sp_idx;
	mp_obj_dict_t *old_globals;
	struct _mp_code_state_t *prev;
	mp_obj_t state[0];
} mp_code_state_t;

/* mp_state_ctx root section (scanned by gc_collect_start at a void**=4 stride
 * over [offsetof(thread.dict_locals), offsetof(vm.qstr_last_chunk))).  Every
 * root POINTER in that window must be 4-aligned, else gc_collect_start misses
 * it.  Verbatim from build/mp-link/gc.pp.c; pointer-only pointee types are
 * opaque forward decls (only the 4-byte pointer size matters here). */
typedef uintptr_t mp_uint_t;
struct _nlr_buf_t; typedef struct _nlr_buf_t nlr_buf_t;
struct _nlr_jump_callback_node_t; typedef struct _nlr_jump_callback_node_t nlr_jump_callback_node_t;
struct _mp_obj_tuple_t; typedef struct _mp_obj_tuple_t mp_obj_tuple_t;
typedef struct _mp_obj_exception_t {
	mp_obj_base_t base;
	size_t traceback_alloc : (8 * sizeof(size_t) / 2);
	size_t traceback_len : (8 * sizeof(size_t) / 2);
	size_t *traceback_data;
	mp_obj_tuple_t *args;
} mp_obj_exception_t;
typedef struct _mp_state_vm_t {
	qstr_pool_t *last_pool;
	mp_obj_exception_t mp_emergency_exception_obj;
	mp_obj_dict_t mp_loaded_modules_dict;
	mp_obj_dict_t dict_main;
	const char *readline_hist[8];
	char *qstr_last_chunk;
	size_t qstr_last_alloc;
	size_t qstr_last_used;
	mp_uint_t mp_optimise_value;
} mp_state_vm_t;
typedef struct _mp_state_thread_t {
	char *stack_top;
	uint16_t gc_lock_depth;
	mp_obj_dict_t *dict_locals;
	mp_obj_dict_t *dict_globals;
	nlr_buf_t *nlr_top;
	nlr_jump_callback_node_t *nlr_jump_callback_top;
	volatile mp_obj_t mp_pending_exception;
	mp_obj_t stop_iteration_arg;
} mp_state_thread_t;
typedef struct _mp_state_ctx_t {
	mp_state_thread_t thread;
	mp_state_vm_t vm;
	/* mem section omitted — the root scan ends at vm.qstr_last_chunk */
} mp_state_ctx_t;

static int allok = 1;

static void chk(const char *what, unsigned off)
{
	int a4 = (off % 4) == 0;
	if (!a4)
		allok = 0;
	printf("%-28s off=%u a4=%d%s\r\n", what, off, a4,
	       a4 ? "" : "  *** NOT 4-ALIGNED -- GC SCAN MISSES IT ***");
}

int main(void)
{
	printf("sizeof: void*=%u size_t=%u mp_obj_t=%u\r\n",
	       (unsigned)sizeof(void *), (unsigned)sizeof(size_t),
	       (unsigned)sizeof(mp_obj_t));

	/* qstr_pool_t: prev / lengths / qstrs[] are far pointers the GC must find
	 * to keep the interned-string chunks alive (NameError if missed). */
	chk("qstr_pool_t.prev",    (unsigned)offsetof(qstr_pool_t, prev));
	chk("qstr_pool_t.lengths", (unsigned)offsetof(qstr_pool_t, lengths));
	chk("qstr_pool_t.qstrs",   (unsigned)offsetof(qstr_pool_t, qstrs));
	printf("qstr_pool_t sizeof=%u\r\n", (unsigned)sizeof(qstr_pool_t));

	/* mp_map_t.table -> mp_map_elem_t[]; the globals/builtins lookup path. */
	chk("mp_map_t.table",      (unsigned)offsetof(mp_map_t, table));
	chk("mp_map_elem_t.key",   (unsigned)offsetof(mp_map_elem_t, key));
	chk("mp_map_elem_t.value", (unsigned)offsetof(mp_map_elem_t, value));
	printf("mp_map_elem_t sizeof=%u mp_map_t sizeof=%u\r\n",
	       (unsigned)sizeof(mp_map_elem_t), (unsigned)sizeof(mp_map_t));

	/* mp_obj_dict_t.map.table — the actual nested offset the GC scans. */
	chk("mp_obj_dict_t.base.type", (unsigned)offsetof(mp_obj_dict_t, base));
	chk("dict.map.table (nested)",
	    (unsigned)(offsetof(mp_obj_dict_t, map) + offsetof(mp_map_t, table)));

	/* mp_obj_list_t.items -> mp_obj_t[]. */
	chk("mp_obj_list_t.items", (unsigned)offsetof(mp_obj_list_t, items));

	/* stackless code_state frame: pointer fields + the value stack state[]. */
	chk("code_state.fun_bc",     (unsigned)offsetof(mp_code_state_t, fun_bc));
	chk("code_state.ip",         (unsigned)offsetof(mp_code_state_t, ip));
	chk("code_state.sp",         (unsigned)offsetof(mp_code_state_t, sp));
	chk("code_state.old_globals",(unsigned)offsetof(mp_code_state_t, old_globals));
	chk("code_state.prev",       (unsigned)offsetof(mp_code_state_t, prev));
	chk("code_state.state[]",    (unsigned)offsetof(mp_code_state_t, state));
	printf("mp_code_state_t sizeof=%u\r\n", (unsigned)sizeof(mp_code_state_t));

	/* mp_state_ctx root section scanned by gc_collect_start.  thread is first
	 * (offset 0), so root pointer ctx-offsets == their offsetof in the
	 * sub-struct; vm starts at sizeof(thread). */
	{
		unsigned thr = (unsigned)sizeof(mp_state_thread_t);
		unsigned rs = (unsigned)offsetof(mp_state_thread_t, dict_locals);
		unsigned re = thr + (unsigned)offsetof(mp_state_vm_t, qstr_last_chunk);
		printf("root_start(dict_locals)=%u root_end(qstr_last_chunk)=%u "
		       "scan_window=[%u,%u)\r\n", rs, re, (rs / 4) * 4, re);
		chk("root_start", rs);   /* MUST be 4-aligned or the whole scan shifts */
		/* thread roots (ctx offset == offsetof in thread) */
		chk("thread.dict_locals",  (unsigned)offsetof(mp_state_thread_t, dict_locals));
		chk("thread.dict_globals", (unsigned)offsetof(mp_state_thread_t, dict_globals));
		chk("thread.nlr_top",      (unsigned)offsetof(mp_state_thread_t, nlr_top));
		chk("thread.pending_exc",  (unsigned)offsetof(mp_state_thread_t, mp_pending_exception));
		/* vm roots (ctx offset = sizeof(thread) + offsetof in vm) */
		chk("vm.last_pool",        thr + (unsigned)offsetof(mp_state_vm_t, last_pool));
		chk("vm.dict_main.base",   thr + (unsigned)offsetof(mp_state_vm_t, dict_main));
		chk("vm.dict_main.map.table",
		    thr + (unsigned)offsetof(mp_state_vm_t, dict_main)
		    + (unsigned)offsetof(mp_obj_dict_t, map)
		    + (unsigned)offsetof(mp_map_t, table));
		chk("vm.readline_hist",    thr + (unsigned)offsetof(mp_state_vm_t, readline_hist));
	}

	printf("%s\r\n", allok ? "ALL-4-ALIGNED" : "MISALIGNED");
	return 0;
}
