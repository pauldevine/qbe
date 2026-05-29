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
| **1 — minic frontend** | ⏳ **In progress** | **Tier 1 done** (2026-05-29): unnamed/abstract params, forward `typedef struct X X;` (+ lexer tag-vs-typedef namespace split), flexible array members, 16→64 member cap. **Bonus declaration-grammar rules** also landed (the immediate next universal layers, all trivial): bare `long long`, `const`/`volatile <typedef>`, bare forward struct/union decl `struct Tag;`. Spike: **0 → 12/132 core files now fully parse; `py/obj.h` cleared** (core files now fail later, in `misc.h`). Next universal blocker: **`...` ellipsis in prototypes** (variadic decls). See running log + `NEXT_SESSION.md`. |
| 1b — Aggregate/designated initializers | ⛔ Gated on more decl grammar | The `dataitem()` emitter design (approved plan §1a). Reached only after variadic protos + `enum Tag` type. |
| 1c — `long long`, limits, `_Bool` | 🟡 **Partial** | `long long` ✅ (aliases to 32-bit `LNG`). Member cap 16→64 ✅. `_Bool` + `NGlo` raise still pending. |
| **2 — Runtime / libc** | ⛔ Gated on spike re-run | `setjmp`/`longjmp` (medium model), `realloc`/`calloc`, missing headers (`assert/stdarg/limits/setjmp/errno/math`). |
| **3 — Port glue + build** | ⛔ Gated | `ports/dos8086/` (`mpconfigport.h`, `mphalport`, `main.c`), `tools/build-micropython.sh`. |
| **4 — DOSBox bring-up** | ⛔ Gated | Link, run headless, debug codegen/stack/heap. Target: `print(1+2)` → `3`. |

## Key reusable assets

- **Spike harness:** `build/mp-spike/run-spike.sh` (cpp+minic over a file list; tallies OK/CPP_FAIL/MINIC_FAIL + failing source line). Re-run after each Tier of grammar work to peel the next failure layer.
- **Spike stub headers:** `build/mp-spike/stubinc/` (temporary `assert/stdarg/limits/setjmp/errno/math/alloca`).
- **Generated MP headers:** `~/projects/micropython/ports/minimal/build/genhdr/` (host build output).

## Running log

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
