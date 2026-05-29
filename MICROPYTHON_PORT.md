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
| **1 — minic frontend** | ⏳ **In progress** | **Tier 1 + layer-2 + nested members done** (2026-05-29): Tier 1, the layer-2 `obj.h` grammar sweep, then nested NAMED struct/union members (`union { … } fun;`), flexible-array rvalue decay, and void-returning function pointers (+ the fn-ptr-member sizing fix).  The `py/obj.h` declaration + nested-aggregate grammar is fully cleared.  Spike: **0 → 23/132**, but is now **cpp-bound, not grammar-bound** — all 132 fail cpp on shared `mpconfig.h`/`misc.h` config errors, so grammar coverage can't be measured further until Phase-2/3 config exists.  See running log + `NEXT_SESSION.md`. |
| 1b — Aggregate/designated initializers | ⛔ Gated on Phase-2/3 config | The `dataitem()` emitter design (approved plan §1a).  **Re-scoped 2026-05-29:** not the next blocker — the spike can't preprocess a clean TU yet, so this isn't measurable.  Build the config/header layer first. |
| 1c — `long long`, limits, `_Bool` | 🟡 **Partial** | `long long` ✅. Member cap 16→64 ✅. **`NVar` "too many variables" cap now blocks 15 spike files** (raise it). `_Bool` + `NGlo` raise still pending. |
| **2 — Runtime / libc** | ⏳ **Now the critical path** | Proper stub/real headers are the immediate spike blocker: `limits.h` (`INT_MAX`/`SIZE_MAX`/`MP_INT_MAX`), `stdint.h` (`intptr_t`/`uintptr_t`), endianness, `__has_builtin`. Then `setjmp`/`longjmp` (medium model), `realloc`/`calloc`. |
| **3 — Port glue + build** | ⏳ **Now the critical path** | A real `ports/dos8086/mpconfigport.h` is what unblocks cpp (endianness, INT/SIZE limits, feature gates) — needed before the spike can measure grammar again. Plus `mphalport`, `main.c`, `tools/build-micropython.sh`. |
| **4 — DOSBox bring-up** | ⛔ Gated | Link, run headless, debug codegen/stack/heap. Target: `print(1+2)` → `3`. |

## Key reusable assets

- **Spike harness:** `build/mp-spike/run-spike.sh` (cpp+minic over a file list; tallies OK/CPP_FAIL/MINIC_FAIL + failing source line). Re-run after each Tier of grammar work to peel the next failure layer.
- **Spike stub headers:** `build/mp-spike/stubinc/` (temporary `assert/stdarg/limits/setjmp/errno/math/alloca`).
- **Generated MP headers:** `~/projects/micropython/ports/minimal/build/genhdr/` (host build output).

## Running log

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
