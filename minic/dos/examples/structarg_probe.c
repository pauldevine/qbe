/*
 * structarg_probe.c -- struct/union pass-BY-VALUE (argument) gate.
 *
 * Pins the by-value aggregate-argument ABI added 2026-05-31.  Before this,
 * minic passed a whole struct argument as a SINGLE scalar word, truncating
 * every member past the first.  The dominant MicroPython blocker: the lexer's
 *   mp_lexer_t *mp_lexer_new(qstr src_name, mp_reader_t reader)
 * takes a 12-byte `mp_reader_t` (a far fn-ptr `readbyte` at offset 4 + a far
 * `data` ptr + `close`) by value; only the first word arrived, so the callee
 * read an uninitialised far function pointer and next_char's first indirect
 * `call far` jumped to garbage (a deterministic reboot on the real Victor,
 * ~4 allocs into compiling print(1+2)).
 *
 * The ABI mirrors struct return-by-value (see sret_probe.c): a by-value
 * aggregate argument crosses the call boundary as a POINTER to its storage —
 * the caller yields the aggregate's address, the callee copies *ptr into its
 * own local (C copy semantics).  Type-driven on both ends, so it agrees across
 * separate compilation.
 *
 * Exercises (codegen + runtime):
 *   1. A struct with a FUNCTION-POINTER member passed by value, callee calls
 *      through the member and reads the other members (the exact reader shape;
 *      under far-data the fn-ptr is a 4-byte far code ptr).
 *   2. C copy semantics: the callee mutates its copy; the caller's original is
 *      unchanged.
 *   3. A struct argument between scalar arguments (positional correctness).
 *   4. Pass-through: receive by value, pass by value again (nested copy).
 *   5. An indirect call (through a fn-ptr variable) whose parameter is a
 *      by-value struct.
 *
 * Build:  tools/build-example.sh --model=compact minic/dos/examples/structarg_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/structarg_probe/structarg_probe.exe \
 *             | diff - minic/dos/tests/structarg_probe.golden.txt
 *
 * Wired into tools/test-dos.sh (compact + large) — the far-data models the
 * MicroPython port uses, where the by-value aggregate copy uses the opaque
 * far loadfw/storefw path.
 *
 * MEDIUM (near-data) is intentionally OMITTED: the callee's by-value copy is a
 * run of near storew's, which QBE's load.c forwards + reconstructs back into
 * the member `loadl` (a 4-byte far code-ptr) via Kl shl/and/or; on i8086 that
 * reconstruction clobbers a live value rega parked in AX (the call's `data`
 * arg), so use_reader returns garbage.  That is a pre-existing, ORTHOGONAL
 * backend bug (the [[i8086-kl-shift-clobbers-ax]] / [[qbe-loadc-wordsize-i8086]]
 * family — Kl ops not declaring their AX/DX clobber to rega), independent of
 * this struct-argument ABI, and it does not affect the far-data target.
 */

#include <stdio.h>

struct R {
	int *data;
	int (*fn)(int *);
	int tag;
};

static int reader_fn(int *p) { return *p + 100; }

/* Receive a struct by value; call through its fn-ptr member + read members. */
static int use_reader(struct R r) {
	return r.fn(r.data) + r.tag;
}

/* Mutate the by-value copy; caller's original must be unaffected. */
static void bump(struct R r) {
	r.tag = 999;
	r.data = 0;
}

/* A struct argument between two scalar arguments. */
static int mixed(int a, struct R r, int b) {
	return a + r.tag + b + r.fn(r.data);
}

/* Receive by value, then pass by value again (nested copy + indirect member). */
static int passthru(struct R r) {
	return use_reader(r);
}

int main(void) {
	int val = 7;
	struct R r;
	int (*use_via_ptr)(struct R) = use_reader;

	r.data = &val;
	r.fn = reader_fn;
	r.tag = 5;

	printf("use=%d (want 112)\r\n", use_reader(r));     /* (7+100) + 5 */
	bump(r);
	printf("tag=%d (want 5)\r\n", r.tag);               /* copy mutated, not r */
	printf("data=%d (want 7)\r\n", *r.data);            /* r.data still &val */
	printf("mixed=%d (want 1124)\r\n", mixed(1000, r, 12)); /* 1000+5+12+107 */
	printf("passthru=%d (want 112)\r\n", passthru(r));
	printf("viaptr=%d (want 112)\r\n", use_via_ptr(r)); /* indirect call, struct arg */

	return 0;
}
