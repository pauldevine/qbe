# Next session — MicroPython port: clear the next minic grammar layers (array typedef → comma-expr)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **The preprocessing wall is down.** As of 2026-05-29, cpp passes on all 132 `py/*.c`
> (CPP_FAIL 132→0) and the spike harness reports honestly (`clang -E`, not `cpp`; pipefail).
> The MINIC tally is now real grammar signal: **26/132 OK, 106 pure `parse error`** — zero
> remaining limit caps. Gate **77/77**.

## What changed last session (so you don't redo it)

- **Harness:** `build/mp-spike/run-spike.sh` uses `clang -E` (macOS `cpp` doesn't strip `//`
  before `#if`) and checks cpp's real exit status before the `tr|sed` pipe. CPP_FAIL is honest now.
- **Config landed:** `~/projects/micropython/ports/dos8086/mpconfigport.h` (locked target config +
  `MP_ENDIANNESS_LITTLE`), `ports/dos8086/mphalport.h` (stub). Spike `-I` points at `ports/dos8086`.
- **Headers:** shipped `minic/include/stdint.h` fixed (`int32_t`=`long`; `intptr_t`/`SIZE_MAX`/etc.,
  FAR_DATA-conditional; pinned by `stdint_probe.c`). Spike `stubinc/unistd.h` added; `stdint.h`
  shadow deleted.
- **`NVar` 512→4096** (`minic/minic.y`): was the #1 blocker (62 files) once cpp was clean.

## Scope for next session — grammar, in convergence order

Re-run the spike first (`build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c`) to confirm the
baseline, then peel layers. The two universal blockers are already pinned in isolation against minic:

### Layer 1 (do first) — **array typedefs**: `typedef int jmp_buf[8];`
The single biggest convergence point: `jmp_buf` comes through `setjmp.h` → `nlr.h` → `obj.h`, so it
hits essentially every file. minic's `typedef` grammar accepts `typedef BASE NAME;` and pointer/
function forms but **not** the array form `typedef BASE NAME '[' expr ']'`. Add the array-declarator
case to the typedef rule (the existing struct-member array-dim path + `const_eval` for the dim is the
model). `jmp_buf` must stay a real array typedef (C requires it so `jmp_buf` decays to a pointer when
passed to `setjmp`) — do **not** paper over it in the header. Add a runtime probe (`sizeof` of an
array-typedef'd local, pass-by-decay to a function).

### Layer 2 — **comma-operator expression statements**: `((void)(n), m_free(p));`
The `m_malloc`/`m_free`/`m_del` family expands to parenthesized comma expressions used as statements.
minic's `expr` has no binary `,` operator. Add it (lowest precedence; evaluate left for side effects,
value is the right operand). Cast-to-void `(void)x` already parses; the gap is purely the `,` operator.

### Layer 3 — re-measure, THEN re-scope §1b (the standing pause point)
With Layers 1–2 cleared, re-run the spike and read the fresh tally. The **§1b aggregate/designated-
initializer emitter** (`{ … }`, `.field =`, `[i] =`; approved plan's `dataitem()`) is still expected
to be the dominant *remaining* grammar blocker, but scope it from the post-Layer-2 numbers, not from
guesswork. `NGlo` (256) has not been hit yet but may surface after the initializer work (MicroPython
emits many static globals) — raise it if/when "too many globals" appears.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; the local `yacc` prints conflict counts (currently
  **126 s/r, 0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. The local miniyacc is
  picky: **no `/* … */` between a production head and its `:`, none trailing a rule's action**; per-rule
  RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **77/77**). Add/extend a probe per runtime-bearing feature.
- Spike harness now uses **`clang -E`** (not `cpp`) and reports CPP_FAIL honestly. **minic's error line
  points just *past* the failing construct** — and worse, on a full file the reported line is the
  parser's *lookahead* line, which can lag the real construct by many lines (binary-search-by-`head`
  is unreliable because truncating mid-function gives spurious failures). Isolate suspect constructs by
  feeding minimal snippets to `minic -m medium` directly. The harness `NORMALIZE` sed is a **no-op under
  macOS BSD sed** (`\b` unsupported) — minic must accept canonical type names natively.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

`void *` pointer comparison hits "void has no size"; `sizeof` only takes `( type | ident )`, not a
general expression; tagged nested aggregate definitions used as members (`struct Foo { … } x;`) are
not handled (only untagged `struct { … } x;`); only struct-member array-dim sites take const-exprs.
