# MicroPython → 8086/8088 DOS Port — Master Tracker

**Goal:** Run an interactive MicroPython REPL on a 1982-era 8086/8088 real-mode DOS machine,
built with the existing `minic → qbe (-t i8086) → nasm → omf_link` pipeline.

This is the **single source of truth** for the port's status. It indexes the supporting docs
and tracks phase progress. Update the status table + running log as work lands.

## Document index

| Doc | Purpose |
|---|---|
| **MICROPYTHON_PORT.md** (this file) | Master status + running log |
| `MICROPYTHON_PORT_ANALYSIS.md` | Original gap analysis (language/runtime/memory-model) |
| `MICROPYTHON_SPIKE_REPORT.md` | Phase 0 feasibility-spike findings (the ranked gap inventory) |
| `NEXT_SESSION.md` | Concrete next-session work plan (currently: Tier-1 declaration grammar) |
| `~/.claude/plans/i-downloaded-projects-micropython-...md` | The approved overall implementation plan |

## Target configuration (locked)

- Interactive **REPL** (`MICROPY_ENABLE_COMPILER=1`)
- **medium** memory model: near data (16-bit pointers, ≤64 KB DGROUP heap) + far multi-segment code
- 16-bit `mp_obj_t` (`MICROPY_OBJ_REPR_A`, ~15-bit small ints)
- `MICROPY_FLOAT_IMPL_NONE`, `MICROPY_NLR_SETJMP=1`, `MICROPY_OPT_COMPUTED_GOTO=0`
- `int64_t`/`long long` are **32-bit** on this target (no true 64-bit) — config must not depend on 64-bit
- MicroPython **v1.29.0-preview** at `~/projects/micropython`

## Phase status

| Phase | Status | Notes |
|---|---|---|
| **0 — Feasibility spike** | ✅ **Done** (2026-05-29) | Host `ports/minimal` builds (114 KB); 132/132 core files fail in `minic` at `py/obj.h` on declaration grammar. See spike report. |
| **1 — minic frontend** | ⏳ **In progress** | Tier 1 + layer-2 + nested members + aggregate initializers + **struct return-by-value** + **typedef-name shadowing** + **capacity bumps** done (2026-05-29).  Spike on honest grammar signal (cpp clean on all 132): **87/132 OK**.  New dominant blocker (~6 files: compile/emitcommon/emitnative/emitnx86/nativeglue/persistentcode): reported at the `int (*vprintf_)(…, va_list args)` fn-ptr-member line, but that line is lookahead-lagged — the true site needs clean-boundary bisection (see `NEXT_SESSION.md`).  Other clusters: scalar array initializers (`static const uint8_t t[] = {…}`), macro-expanded calls.  See running log + `NEXT_SESSION.md`. |
| 1b — Aggregate/designated initializers | ✅ **Done** (2026-05-29) | File-scope `T NAME = { ... };` for struct/union/global vars: designated `.field =`, nested braces, partially-initialized array members (trailing zero-fill), string-literal + `&global` pointer fields, casts and constant-folded values.  New `gaggr`/`gilist`/`gitem`/`gival` grammar (0 new conflicts) + a constant-initializer folder (`cival_eval`) + a layout-aware data emitter (`emit_global_aggregate`, inserts `z N` for designator gaps).  Pinned by `aggregate_init_probe.c`.  **Spike 69→77/132.** |
| 1d — Struct/union return-by-value (ABI) | ✅ **Done** (2026-05-29) | Hidden-pointer lowering (caller-allocated result storage; callee copies through it and returns the pointer), all in the minic frontend as scalar QBE IR — no backend changes.  Function header + `ret` + call site + `lval(call)`; `emit_struct_copy` helper.  Pinned by `sret_probe.c` (medium + large).  Closes the `return source_line;` cluster.  **Spike 77→82/132.**  Orthogonal: `long` struct members read from an opaque (call-result) source lose their high word — pre-existing `[[qbe-loadc-wordsize-i8086]]` bug; MicroPython's returned structs are word/`size_t`-sized so it doesn't gate. |
| 1e — Typedef-name shadowing + capacity | ✅ **Done** (2026-05-29) | (a) **Lexer disambiguation:** a declarator / parameter / member / member-access identifier that collides with a typedef name now lexes as `IDENT`, not `TNAME` — the real `py/scope.h` shape `scope_find_or_add_id(scope_t *scope, qstr qstr, …)`.  Three rules in the `yylex()`/`yylex_inner()` wrapper: (1) after a complete type-specifier token (type keyword or `TNAME`) → `IDENT`; (2) in-scope local/param (`var_islocal()`) → `IDENT`, with the previous function's locals dropped on its body's closing `}` (deferred one token via `pending_varclr`/`brace_depth` so the body's last statement still resolves, and so the next function's typedef-named param type isn't shadowed by a stale local); (3) after `.`/`->` (member namespace) → `IDENT`.  0 new conflicts.  Pinned by `typedef_shadow_probe.c` (medium + large).  (b) **Capacity:** `NStruct` 64→256, `NTyp` 128→512 — preprocessed MicroPython TUs define >64 aggregates / >128 typedefs; minic is host-compiled so this is cheap.  This (not the shadow fix) was what gated the 8 `scope_kind_t` files (their real error was *"too many struct/union definitions"*, masked behind the lookahead-lagged `} scope_kind_t;` source line).  **Spike 82→87/132.** |
| 1c — `long long`, limits, `_Bool` | 🟡 **Partial** | `long long` ✅. Member cap 16→64 ✅. **`NVar` 512→4096 ✅ (2026-05-29)** — was the #1 blocker once cpp was clean (62/132 files); raising it flipped 17→26 OK and eliminated *all* limit-cap failures (remaining 106 are pure grammar).  `_Bool` + `NGlo` raise still pending (not yet hit). |
| **2 — Runtime / libc** | 🟡 **cpp-clean** | `stdint.h` fixed + shipped (2026-05-29): `int32_t`/`uint32_t`=`long` (were wrongly 16-bit `int`), added `intptr_t`/`uintptr_t` (FAR_DATA-conditional) + `INTPTR_MAX`/`SIZE_MAX`/`INTPTR_UMAX`; pinned by `stdint_probe.c`.  Spike stub `unistd.h` added.  `limits.h` stub already had target widths.  **All 132 files preprocess cleanly (CPP_FAIL 132→0).**  Still pending: real `setjmp`/`longjmp` (medium model — note `jmp_buf` is an array typedef, blocked on Phase-1 grammar), `realloc`/`calloc`. |
| **3 — Port glue + build** | 🟡 **config landed** | `ports/dos8086/mpconfigport.h` created (2026-05-29) — locked target config + explicit `MP_ENDIANNESS_LITTLE`; this is what unblocked cpp.  Minimal `mphalport.h` stub added.  Spike now points `-I` at `ports/dos8086` (replacing the ad-hoc `stubinc` shims).  Still pending: `mphalport.c`, `main.c`, `tools/build-micropython.sh`. |
| **4 — DOSBox bring-up** | ⛔ Gated | Link, run headless, debug codegen/stack/heap. Target: `print(1+2)` → `3`. |

## Key reusable assets

- **Spike harness:** `build/mp-spike/run-spike.sh` (cpp+minic over a file list; tallies OK/CPP_FAIL/MINIC_FAIL + failing source line). Re-run after each Tier of grammar work to peel the next failure layer.
- **Spike stub headers:** `build/mp-spike/stubinc/` (temporary `assert/stdarg/limits/setjmp/errno/math/alloca/unistd`).  The `stdint.h` shadow was **deleted** 2026-05-29 — the shipped `minic/include/stdint.h` is now correct and validated by the spike directly.
- **Port config:** `~/projects/micropython/ports/dos8086/mpconfigport.h` + `mphalport.h` (the spike's `-I` target; the real port build will grow from here).
- **Generated MP headers:** `~/projects/micropython/ports/minimal/build/genhdr/` (host build output).

## Running log

- **2026-05-29 — struct/union return-by-value (codegen/ABI); spike 77 → 82/132; gate 80→82/82.**
  Closed the dominant post-§1b blocker — a function whose return type is a struct/union
  (`mp_code_lineinfo_t mp_bytecode_decode_lineinfo(...)`; callers `mp_code_lineinfo_t x = decode(&p);`).
  Lowered System-V style with a **hidden first pointer parameter** (caller-allocated result storage),
  entirely in the minic frontend as scalar QBE IR — **no i8086 backend / QBE changes**.
  - **Function header** (`ansi_func_proto`, `emit_knr_func_typed`): a struct/union return type emits
    `function <ptrclass> $f(<ptrclass> %t0, …)` (ptrclass = `DATAPTR_T()`: `w` near / `l` far),
    spills the hidden pointer to a fixed `%__sret` slot, and sets `cur_fn_sret`/`cur_fn_sret_ctyp`
    (reset in every `init*` action).
  - **`ret`** (`stmt` case Ret): copies the returned aggregate into `*%__sret` via the new
    `emit_struct_copy(dst, src)` helper (factored out of the existing struct-assignment word/byte
    copy loop) and `ret`s the hidden pointer.
  - **Call site** (`call()` direct + fn-ptr branches, `expr` case `'I'`): allocates the result slot
    (`alloc_sret_slot`), passes its address as the hidden first arg, and yields the slot address as
    the aggregate rvalue.  `lval()` now treats a struct-returning call as an lvalue (so `f().field`
    works); the struct-assignment path reuses its own `s0` for `'C'/'I'` sources to avoid emitting
    the call twice.  Far/near-ness of the slot tracks `!NEAR_DATA()`.
  - **No new yacc conflicts** (stays 108 s/r, 0 r/r — pure C-action change).  Pinned by
    **`sret_probe.c`** (medium + large, DOSBox-verified): assignment-from-call, decl-init-from-call,
    member-of-call-result, nested struct-returning calls, an odd-sized `{int;char}` (byte-tail copy),
    the exact `{size_t;size_t}` MicroPython shape, a union return, and `&result` passed on.
  - **Orthogonal limitation found, not chased:** a struct member of type `long` (4 bytes, read back
    via `loadl`) loses its high word **when the source words are opaque to QBE** (a call result):
    QBE forwards the `loadl` through the word-by-word copy and reconstructs `lo | (hi<<16)`, but the
    i8086 backend lowers the final `or` 16-bit-wide — the pre-existing `[[qbe-loadc-wordsize-i8086]]`
    family bug, independent of this ABI.  **All MicroPython structs returned by value are
    word/`size_t`-sized** (`size_t` = 2 bytes here), so this does not gate the port; `sret_probe.c`
    documents and avoids it.
  - **Spike re-run:** **82/132 OK** (was 77).  New dominant blocker (8 files, `py/scope.h`): a
    **declarator named the same as a typedef** — `id_info_t *scope_find_or_add_id(scope_t *scope,
    qstr qstr, id_info_kind_t kind)` where `qstr` is both a type and the parameter name.  minic's
    lexer returns `TNAME` for the param identifier, so `qstr qstr` parses as two type-names → parse
    error.  Lexer/parser disambiguation (an identifier in declarator position after a type-specifier
    should lex as `IDENT`).  See `NEXT_SESSION.md`.
- **2026-05-29 — Phase 1 §1b: file-scope aggregate / designated initializers; spike 69 → 77/132; gate 79→80/80.**
  Closed the standing §1b pause point.  `T NAME = { ... };` for struct/union/global variables now parses and emits a correct QBE `data` block.  All edits in `minic/minic.y` (rebuilt via local `yacc`; **shift/reduce stays 108, reduce/reduce 0** — no new conflicts).
  - **Grammar:** new `gaggr: '{' gilist opt_trailing_comma '}'` (mirrors the `sai_list` trailing-comma pattern) → `gilist` (left-recursive chain of `gitem`) → `gitem` (`gival` | `'.' IDENT '=' gival` 'D'-node | `'[' expr ']' '=' gival` 'd'-node) → `gival` (`expr` | nested `gaggr`).  Wired into `typed_decl_rest` as `'=' gaggr ';'` (covers `const`/`static` prefixes since both route through `type_and_ident typed_decl_rest`).  The initializer is a generic, type-agnostic Node tree; the type context is applied at emit time.
  - **Emitter (`emit_global_aggregate` + helpers):** walks the Node tree against the target struct/union/array type, tracking byte offset against member offsets and inserting `z N` zero-fill for any gap a designator skips (and a trailing `z` to the full type size).  A constant-initializer folder (`cival_eval`/`cival_addr`) reduces each scalar to either an integer or a symbol+addend — handling enum constants, `&global` / function-name / array-name address decay, string literals (`$gloN`), value-preserving casts (`'K'` nodes), and `+`/`-`/bitwise/shift constant arithmetic.  Out-of-order designators and bitfield initializers `die()` with a clear message (not exercised by MicroPython; revisit if a real consumer needs them).
  - **Verified layout** (medium, near-data): designated `.base = { &g }` → `w $g`; partial `.slots = { 11,22,33 }` of `int[4]` → `w 11, w 22, w 33, z 2`; partial struct `{ .name = 5 }` → leading/trailing `z`; `union { int; long }` `{ 0x1234 }` → `w 4660, z 2`.  **`aggregate_init_probe.c`** (8 lines) builds via `tools/build-example.sh --model=medium` and round-trips in DOSBox — including a `&global` pointer field read back via deref and a `"hi"` string-literal field — proving the 2-byte near-data symbol relocations land correctly.
  - **Spike re-run** (`build/mp-spike/run-spike.sh py/*.c`): **77/132 OK** (was 69).  The new dominant blocker is **struct return-by-value** (`mp_code_lineinfo_t mp_bytecode_decode_lineinfo(...)` returning a struct; callers `mp_code_lineinfo_t x = decode(&p);`) — the `return source_line;` lookahead cluster, ~20 files.  That's a codegen/ABI feature (hidden-pointer return or small-struct-in-DX:AX), **not grammar** — its own session.  See `NEXT_SESSION.md`.
- **2026-05-29 — Phase 2/3 config bring-up: spike CPP_FAIL 132→0; honest grammar re-measure → 26/132 OK; NVar 512→4096. Gate 76→77/77.**
  Executed `NEXT_SESSION.md` Steps 0–2.  **The spike can now preprocess every core file cleanly**, so the MINIC tally is real grammar signal for the first time since the config wall went up.
  - **Step 0 — harness honesty** (`build/mp-spike/run-spike.sh`): switched `cpp` → `clang -E`.  The macOS `cpp` driver runs in a traditional mode that does **not** strip `//` comments before evaluating `#if`, so `#if defined(__cplusplus) // …` (mpconfig.h:31) errored out — this, not config, was the first cpp failure (NEXT_SESSION.md had mislabeled it config).  `clang -E` is a conforming preprocessor (handles `//`, `__has_builtin`, `#elif` correctly — all three "config" errors in `misc.h` evaporated).  Also captured cpp output to a temp file and check **its** exit status before the `tr|sed` pipe, so cpp failures report `CPP_FAIL` honestly instead of being masked by `sed`'s exit 0.
  - **Step 1 — config/headers so cpp passes:**
    1. **`ports/dos8086/mpconfigport.h`** (new, in the micropython tree) — the locked target config (REPR_A, FLOAT_IMPL_NONE, NLR_SETJMP, COMPUTED_GOTO=0, LONGINT_IMPL_NONE, MINIMUM ROM level + REPL/GC on) with an explicit `#define MP_ENDIANNESS_LITTLE (1)` (kills the endianness auto-detect `#error`).  Spike `-I` now points here instead of `ports/minimal`.
    2. **`minic/include/stdint.h`** (shipped, fixed) — `int32_t`/`uint32_t` were wrongly `int` (16-bit!) on this target; now `long` (4 bytes).  Added `intptr_t`/`uintptr_t` (FAR_DATA-conditional: 16-bit near / 32-bit far), the exact-width + `INTPTR_MAX`/`INTPTR_MIN`/`UINTPTR_MAX` limit macros, `SIZE_MAX`, and the non-standard `INTPTR_UMAX` MicroPython references.  These were the `Unexpected MP_INT_MAX/SIZE_MAX value` `#error`s: `MP_INT_MAX = INTPTR_MAX` was undefined→0.  The spike's `stubinc/stdint.h` shadow is **deleted** so the spike validates the real shipped header.  Pinned by **`stdint_probe.c`** (medium; sizeofs + a 100000×23 round-trip that would truncate if int32_t were still 16-bit + INT32_MAX/INTPTR_MAX/SIZE_MAX).
    3. **`build/mp-spike/stubinc/unistd.h`** (new spike stub) — `ssize_t` + read/write/close/lseek/unlink prototypes (5 files `#include <unistd.h>`).  **`ports/dos8086/mphalport.h`** (new) — minimal `mp_hal_ticks_ms`/`mp_hal_set_interrupt_char` stubs (4 files).
  - **Step 1c — `NVar` 512 → 4096** (`minic/minic.y`): once cpp was clean, "too many variables" was the **#1 blocker (62/132 files)** — MicroPython pulls ~214 `MP_QSTR_*` enum constants from genhdr plus hundreds of `extern const mp_obj_type_t` globals into the never-cleared `varh[]` open-addressing table.  Host-only memory.  No new yacc conflicts (it's an `enum` bound, not grammar).
  - **Result:** **CPP_FAIL 132→0**, OK 17→**26** (the earlier "23 OK" were garbled-but-parseable files; honest baseline is lower then climbs).  All 106 remaining failures are now **pure `parse error`** — zero limit caps.  Next universal grammar layer, peeled by a temporary pointer-typedef `jmp_buf` experiment: **(1) array typedefs** `typedef int jmp_buf[8];` (setjmp/nlr, hits ~all files), then **(2) comma-operator expression statements** `((void)(n), m_free(p));` (the `m_malloc`/`m_free` macros).  Both confirmed in isolation against minic.  **Gate 76 → 77/77** (added stdint_probe).  See `NEXT_SESSION.md` for the re-scoped grammar plan.
- **2026-05-29 — Phase 1 nested-aggregate members + flexible-array decay + void-returning fn-ptrs; spike 16 → 23/132; spike measurement now cpp-bound.**
  All edits in `minic/minic.y` (rebuilt via local `yacc`; **shift/reduce stays 126, reduce/reduce 0** — no new conflicts). **Gate 74 → 76/76** — two new runtime probes (`nested_member_probe.c`, `voidfnptr_probe.c`) + goldens pin the codegen.
  - **Features landed:**
    1. **Nested NAMED struct/union members** (`union { … } fun;` / `struct { … } pt;`) — the universal MicroPython blocker at `obj.h:2973`.  New `nestedagg` rule with shared `nested_s_begin`/`nested_u_begin` begins; replaces the old single-level `anonstruct`/`anonunion`/`anonmembers` machinery.  A `structstk[]` + `structstksp` curstruct save/restore stack supports arbitrary nesting depth (the old `parentstruct` single-global is now unused).  The nested body is full `smembers` (recursion), so anonymous C11 members (hoist) and named members share one path; nested aggregates may themselves contain nested aggregates.  Unique `__nested_%d` tag names dodge the structadd "already defined" trap.
    2. **Flexible-array-member rvalue decay** (real bug, unblocked `objtuple.h`'s `&self->items[0]`) — the `m->count > 0` decay test at the `expr()` `'.'`/member-read site failed for flexible arrays (`T x[];`, count 0, indistinguishable from a scalar), so `self->items` loaded a scalar instead of decaying to its address → "void has no size" / "dereference of a non-pointer".  Added an `isflex` flag to `struct Member` (set in `structaddarrmember` when `count==0`, copied in `hoistanonymous`); decay test is now `m->count > 0 || m->isflex`.
    3. **void-returning function pointers** (`void (*fp)(…)`) — the #2 spike blocker (18 files: `mp_reader_t::close`, `mp_obj_type_t` slots).  Removed the over-eager `if (… == NIL) die("invalid void function pointer")` guard at all 6 fn-ptr declarator sites (5 variable + 1 struct member); `FUNC(NIL)` already encodes a void return.  Mirrored the `call()`-function void-result suppression into the indirect-call `case 'I'` (was unconditionally emitting `sr =%c call`, hitting `irtyp_ret(NIL)` → `SIZE(NIL)` → "void has no size").
    4. **Function-pointer struct-member sizing fix** (companion to #3, a real latent corruption) — `SIZE()` of a pointer-to-function returned `DATAPTR_SZ()` (2 under near-data/medium) while the value is a far **code** pointer stored/loaded as 4 bytes (`storel`/`loadl`).  A fn-ptr member and the member after it overlapped: in `struct dispatch { int (*op)(); int tag; }`, `d.tag = 0` zeroed `op`'s segment word.  Fix: `SIZE()` now sizes `PTR`→`FUN` with `CODEPTR_SZ()`.  Only medium changes (near-code and far-data models already agreed); `fnptrprobe.c` now exercises the corrected layout, gate stays green.
  - **Spike re-run** (`build/mp-spike/run-spike.sh py/*.c`): **23/132 OK** (was 16).  The fn-ptr-member blocker is gone (the 18 files now reach a later point; 15 hit minic's `NVar` "too many variables" cap — a Phase-1c limit).  **Key finding: the spike is now cpp-bound, not grammar-bound.** All **132/132 files fail cpp** (exit 1) on shared-config errors in `py/mpconfig.h` (`invalid token at start of preprocessor expression`, `Unexpected MP_INT_MAX value`, `Unexpected SIZE_MAX value`, `endianness not defined and cannot detect it`) and `py/misc.h` (`__has_builtin` unsupported, `#elif after #else`).  The harness pipeline `cpp | tr | sed` masks cpp's non-zero exit with `sed`'s 0, so these get mislabeled MINIC_FAIL "parse error" (94 of them); minic then chokes on cpp's leftover raw `#if`/comments.  The "23 OK" are just files whose garbled cpp output stayed parseable.  **Frontend grammar coverage can no longer be measured through this broken preprocessing environment** — the next high-value step is Phase-2/3 config bring-up (a real `mpconfigport.h` + proper stub headers), NOT the §1b initializer emitter.  See `NEXT_SESSION.md`.
- **2026-05-29 — Phase 1 layer-2 declaration-grammar sweep; spike 12 → 16/132, `py/obj.h` type/enum/struct grammar fully cleared.**
  All edits in `minic/minic.y` (rebuilt via local `yacc`; **shift/reduce 117 → 126, reduce/reduce 0** — the +9 s/r are all benign: +4 trailing-`int` shift-vs-reduce on `long`/`long long`/`short`, +5 in the `enum Tag` / `const struct` / bitfield-expr family, every one resolved by shift). **Gate 72 → 74/74** — two new runtime probes (`ellipsis_probe.c`, `declgram2_probe.c`) + goldens pin the codegen-bearing features in DOSBox.
  - **Features landed (each peeled by a spike re-run, in order):**
    1. **`...` ellipsis in prototypes** — new `ELLIPSIS` token (lexer recognises the three-dot run, distinct from the `.5`/`.` paths); `par1`/`fptpar1` accept a trailing/bare `...`. Variadic *definitions* that read only their fixed params also work (cdecl caller-cleanup); variadic *consumption* (va_list) is Phase-2 libc.
    2. **Canonical trailing-`int` integer specifiers + `signed`** — `long int`, `long long int`, `unsigned long long int`, `short int`, `unsigned {short,long,long long} int`, and the full `signed …` set (new `TSIGNED` keyword/token; `signed` is the default signedness so each form just drops it). **Key discovery:** the spike harness's `NORMALIZE` sed (meant to fold `long long int`→`long long` etc.) is a **silent no-op under macOS BSD `sed`** (`\b` word-boundaries unsupported), and the Phase-3 port build has no NORMALIZE at all — so minic must accept these canonical forms natively. MicroPython uses `long long int` (360×) / `unsigned long long int` (357×) / `long int` (235×) pervasively.
    3. **enum const-expression initializers + trailing comma** — `enum: IDENT '=' expr` folded by the existing `const_eval` (which already resolves prior enum constants via the `'V'` case), so `MP_QSTRstart_of_main = MP_QSTRnumber_of_static - 1` works; `enums: enums ','` absorbs a trailing comma uniformly across **both** bare `enum {…};` and `typedef enum {…} name;`.
    4. **`const/volatile struct/union/enum Tag` + bare `enum Tag` as type-specifiers** — the `type:` rule had `CONST TNAME` but not `CONST STRUCT IDENT` etc.; `enum E e;` / `enum E f(…)` now parse (enum value is an `int`).
    5. **const-expression bitfield widths** — `':' NUM` → `':' expr` + `const_eval`, so `size_t total_prev_len : (8 * sizeof(size_t) - 1)` works (`sizeof(type)` already folds to an `'N'` node).
    6. **const-expression / parenthesized struct-array-member dimensions** — `smembers`/`anonmembers` `'[' NUM ']'` → `'[' expr ']'` + `const_eval`, so `void *regs[((13))]` works. (The 13 other `[NUM]` array-dim sites were left alone — not exercised by the spike, and broadening all of them risks conflicts.)
    7. **`const/volatile void`** — added `CONST TVOID`/`VOLATILE TVOID` → `NIL`, for `const void *` (used as `mp_const_obj_t`).
    8. **Incomplete-struct forward declaration** — `STRUCT/UNION IDENT` (+ const/volatile) now `structadd_forward()` instead of `die("undefined struct")`, so `extern const struct _mp_obj_str_t foo;` (legal reference to an incomplete type) parses.
    9. **`NString` 32 → 128** — MicroPython identifiers reach 49 chars (`MP_MAP_LOOKUP_ADD_IF_NOT_FOUND_OR_REMOVE_IF_FOUND`); host-only memory.
  - **Spike re-run** (`build/mp-spike/run-spike.sh py/*.c`): **16/132 OK** (was 12). More tellingly, the universal convergence point marched ~1000 lines deeper into `obj.h`: top-of-`obj.h` → `misc.h` 1887 (`long long int`) → enum 2010 → struct 2031 (bitfield) → 2226 (`typedef void *`) → 2767 (`extern const struct …`, incomplete) → **2973 (nested NAMED `union { … } fun;` member)** — the next blocker.
  - **Next session** (`NEXT_SESSION.md` updated): **nested NAMED struct/union members** (`union { … } name;` / `struct { … } name;` as a struct member). Truly-anonymous C11 members already work; the named nested-aggregate-definition path does not. This is the recursive-aggregate-member grammar — a distinct, larger layer than the localized rules above. Then re-run the spike and re-scope the §1b aggregate/designated-initializer emitter from the fresh tally (the standing pause point). Pre-existing orthogonal limitations noted in passing: `void *` pointer comparison (`p == 0`) hits "void has no size", and `sizeof` only takes a parenthesized type/ident (no `sizeof expr`).
- **2026-05-29 — Phase 1 Tier 1 done + 3 bonus decl-grammar rules; spike 0 → 12/132.**
  All edits in `minic/minic.y` (rebuilt via local `yacc`; **shift/reduce 113 → 117, reduce/reduce 0**;
  the +4 s/r are all on `TLNGLNG` in the 4 `dcls`-context states that *already* conflict on every
  other type-start token `TINT`/`TLNG`/… — same pre-existing dangling-decl family, resolved by
  shift, not a new ambiguity class). **Gate stays green (72/72)** — added `declgram_probe.c`
  (medium) + golden, pinning all the features at runtime.
  - **Tier 1 (the planned three):** (1) abstract/unnamed params in prototypes (`par1: type` /
    `type ',' par1` via `abstract_param()`, which treats a bare `void` as the empty list so
    `(void)` keeps working — also let me drop the now-redundant `par0: TVOID` rule that would
    otherwise have caused a reduce/reduce on `)`); (2) forward `typedef struct Foo Foo;`
    (`typedef_struct_tag` now creates an incomplete tag via new `structadd_forward()` instead of
    `die()`ing; `structadd` reuses a forward slot when the body arrives); (3) flexible array
    members `T name[];` (new `smembers` rule, `structaddarrmember(...,0)`). Plus member cap 16→64
    and a new `forward` flag on the struct table.
  - **Lexer namespace fix (needed for the same-name idiom `typedef struct Foo Foo;`):** an
    identifier directly after `struct`/`union`/`enum` is a *tag*, so it must lex as `IDENT` even
    when a same-named typedef exists. Added `prevtok` tracking via a thin `yylex()` wrapper around
    `yylex_inner()`. (MicroPython itself only uses the distinct-name form `struct _foo_t` /
    `foo_t`, which worked without this — but the same-name idiom is ubiquitous C.)
  - **Bonus (the next universal layers the re-run revealed, each trivial & in-family):** bare
    `long long` (`type: TLNGLNG → LNG`); `const`/`volatile <typedef-name>` (`CONST TNAME` /
    `VOLATILE TNAME` — minic had `const char` etc. but not `const <typedef>`, blocking
    `const byte *` / `const mp_obj_t`); bare forward struct/union decl (`STRUCT IDENT ';'` /
    `UNION IDENT ';'` → `structadd_forward`, for `struct _mp_print_t;`).
  - **Spike re-run** (`build/mp-spike/run-spike.sh py/*.c`): **12/132 OK** (was 0/132) —
    asm*/emitn* + objringio + ringbuf. **`py/obj.h` declaration grammar is fully cleared**; the
    remaining 120 now fail later, clustered in `py/misc.h` (~lines 1770–1845) on the **`...`
    ellipsis in function prototypes** (variadic decls like `void vstr_printf(vstr_t *, const char *, ...);`).
    A spike-only `build/mp-spike/stubinc/stdint.h` shadow was added (defines `intptr_t`/`uintptr_t`/
    `intmax_t`) to see past a `py/mpconfig.h` gap — **shipped `minic/include/stdint.h` lacks
    `intptr_t`/`uintptr_t`** (real header gap; width is model-dependent — `int` near / `long` far —
    so fix wants a `FAR_DATA` `#ifdef`). Not committed (under gitignored `build/`).
  - **Next session** (`NEXT_SESSION.md` updated): `...` ellipsis in prototypes → re-run → then
    `enum Tag` as a type-specifier (Tier 2 #4) → then the aggregate/designated-initializer emitter
    (the explicit pause point, §1b). Plus the `intptr_t`/`uintptr_t` stdint fix.
- **2026-05-29 — Phase 0 spike complete.** Host minimal port builds (proves config sound).
  Pushed all 132 `py/*.c` through cpp+minic: all fail at shared header `py/obj.h` on standard-C
  declaration grammar. **Key revision:** the dominant blocker is declaration grammar (unnamed
  params, forward typedefs, flexible arrays, enum-tag types), not just aggregate initializers —
  Phase 1 re-sequenced declaration-grammar-first and re-estimated upward. Wrote
  `MICROPYTHON_SPIKE_REPORT.md`. Next: implement Tier 1 (`NEXT_SESSION.md`), re-run spike.
- **2026-05-28 — Analysis + plan.** Empirically scoped the port; established the 8088 has the
  capacity (256 KB code / 16 KB RAM; medium model; near heap in one segment) and the real
  blockers are the minic frontend, `setjmp`, and small limits. Wrote `MICROPYTHON_PORT_ANALYSIS.md`
  and the approved spike-first implementation plan.
