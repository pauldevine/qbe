# Next session — MicroPython port: §1b aggregate/designated initializers + struct return-by-value

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 69/132 OK** (was 26 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` to confirm before peeling more.
> Gate **79/79**.

## What changed last session (so you don't redo it)

Last session cleared Layers 1 & 2 from the old plan plus two larger blockers that the
re-measure exposed, taking the spike 26 → 69:

- **Layer 1 — array typedefs** (`typedef int jmp_buf[8];`): new `tdcl` grammar rule +
  `typhadd_array()` storing `ctyp = IDIR(elem)` with `arraydim`/`arrayelem`. A parser
  global `g_td_arraydim` (set by `typhget`, reset on type keywords and `type '*'`) lets
  the local-var and struct-member decl rules size them as real arrays that decay to a
  pointer. Pinned by `arraytypedef_probe.c`. (NOTE: `sizeof()` of a *local* array — typedef'd
  or plain `int buf[8]` — still returns pointer size; pre-existing orthogonal gap, the probe
  pins the dimension through the struct-member path instead.)
- **Layer 2 — comma operator**: the grouping-paren rule is now `'(' comma_expr ')'` (was
  `'(' expr ')'`); the `,`-node codegen already existed. Handles
  `((void)(n), m_free(p));`. Pinned by `comma_probe.c`.
- **`prog` made left-recursive** (was right-recursive `externdcl prog | …`). This was the
  *actual* dominant blocker: right recursion put the entire file on miniyacc's fixed
  `stk[StackSize=500]`, so big headers overflowed and yyerror'd as a bare "parse error"
  many lines past the real spot. Left recursion bounds the stack and **also dropped s/r
  conflicts 126 → 108**. Biggest single win (+30 files). Conflicts now **108 s/r, 0 r/r**.
- **`const`/`volatile` after `*`** (`int *const *p`, `struct X *const *child_table`): added
  `type '*' CONST` / `type '*' VOLATILE` rules (no-op qualifier on the pointer). Common in
  MicroPython prototypes.

## Scope for next session — the two dominant remaining blockers (both re-measured & confirmed)

### §1b — aggregate / designated initializers (the standing pause point; START HERE)
The object files (`objbool`, `objnone`, `objdict`, …; the `}`-cluster, 13+ files) end with a
file-scope `mp_obj_type_t` definition:
```c
const mp_obj_type_t mp_type_bool = {
    .base = { &mp_type_type }, .flags = (0x0008), .name = MP_QSTR_bool,
    .slot_index_make_new = 1, …,
    .slots = { (const void *)(…) bool_make_new, … }
};
```
minic has **no** struct/global aggregate-initializer support today — even a *local*
`struct P g = { .a = 1, .b = 2 };` parse-errors (PR#12's "designated initializers" only
covered arrays / compound literals, not struct-variable `= {…}`). Needs: brace-init for
struct/global variables, designated `.field =`, nested braces (`.base = { … }`), array
members (`.slots = { … }`), and casts inside initializers. This is the approved plan's
`dataitem()` emitter. Add a runtime probe (read back several fields, incl. a nested struct
member and an array-member slot). `NGlo` (256) may surface here (MicroPython emits many
static globals) — raise it if "too many globals" appears.

### Struct return-by-value (newly discovered, 27 files via the `source_line` cluster)
`py/bc.h` defines `mp_code_lineinfo_t mp_bytecode_decode_lineinfo(…)` returning a struct by
value, and callers do `mp_code_lineinfo_t decoded = decode(&p);`. minic emits a word-typed
return and chokes on the struct-typed init ("invalid lvalue"). This is a codegen/ABI feature
(hidden-pointer return arg, or small-struct-in-DX:AX), **not grammar** — likely its own
session. Mirror the call/selret ABI work already done for Kl returns.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is picky:
  **no `/* … */` between a production head and its `:`, none trailing a rule's action**;
  per-rule RHS length limit (~5 symbols, though longer rules exist).
- Run `tools/test-dos.sh` (must stay **79/79**). Add/extend a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`) and reports CPP_FAIL honestly. **minic's error
  line is the parser's *lookahead* line, which lags the real construct by many lines.**
  Binary-search-by-`head` is **unreliable** — truncating mid-function/mid-struct/mid-enum
  gives *spurious* failures AND the OK/FAIL signal is **non-monotonic** (a later cut can pass
  where an earlier one failed). Bisect only at top-level statement boundaries, and confirm any
  suspected construct by feeding a **minimal isolated snippet** to `minic -m medium` directly.
  The `NORMALIZE` sed is a no-op under macOS BSD sed (`\b`) — minic must accept canonical names.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

`sizeof` of a *local* array returns pointer size (dims aren't tracked for locals); `void *`
pointer comparison hits "void has no size"; `sizeof` only takes `( type | ident )`, not a
general expression; tagged nested aggregate definitions used as members (`struct Foo { … } x;`)
are not handled (only untagged `struct { … } x;`); only struct-member array-dim sites take
const-exprs.
