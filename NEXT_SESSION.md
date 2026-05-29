# Next session — MicroPython port, Phase 1 continued (nested named aggregate members)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Tier 1 + layer 2 are DONE** (2026-05-29). The entire `py/obj.h` type/enum/struct
> declaration grammar is cleared. This document is the work plan for the *next* layer.

## Where we are

Layer 2 landed nine localized `minic.y` features (see `MICROPYTHON_PORT.md` running log):
`...` ellipsis protos, canonical trailing-`int` specifiers + `signed`, enum const-expr
initializers + trailing comma, `const/volatile struct/union/enum` + bare `enum Tag` types,
const-expr bitfield widths, const-expr array-member dims, `const/volatile void`,
incomplete-struct forward decls, and `NString` 32→128. Conflicts **117 → 126 s/r, 0 r/r**
(all benign). Gate **74/74** (`ellipsis_probe.c` + `declgram2_probe.c` pin the runtime-bearing
features). Spike **12 → 16/132 fully parse**; the convergence point marched ~1000 lines deep
into `obj.h`.

**The current universal blocker (114/132 files): nested NAMED struct/union members.**

```c
typedef struct _mp_obj_fun_builtin_fixed_t {
    mp_obj_base_t base;
    union {                 // <-- a nested union DEFINITION used as a member,
        mp_fun_0_t _0;      //     WITH a member name `fun`
        mp_fun_1_t _1;
        mp_fun_2_t _2;
        mp_fun_3_t _3;
    } fun;
} mp_obj_fun_builtin_fixed_t;
```

Truly-*anonymous* C11 members (`union { … };` with no trailing name — members promote into
the enclosing struct) **already work**. The gap is a nested struct/union *definition* that is
then given a member name (`} fun;`). `struct { … } pt;` fails the same way.

## Scope (same discipline as before)

All edits in `minic/minic.y`. After each: `cd minic && make minic`, **watch yacc conflict
counts (no new reduce/reduce; justify any new shift/reduce against the existing same-state
token family)**, run `tools/test-dos.sh` (must stay **74/74**), re-run the spike to peel the
next layer. Add/extend a probe per runtime-bearing feature.

### Step 1 — nested named struct/union member definitions

The member grammar lives in `smembers` (named structs, `minic.y` ~4206) and `anonmembers`
(the anonymous-aggregate path that handles the C11 promote-into-parent case). You need a
`smembers` alternative that:
- opens a nested struct/union body (reuse the existing `structstart`/`typedefstructstart`
  machinery, or a fresh nested-struct start that pushes/pops `curstruct`),
- parses its `smembers` body recursively,
- then on `} IDENT ;` registers a **named** member of that nested aggregate type in the
  *outer* struct (via `structaddmember(outer, name, nested_struct_ctyp)`).

Watch out for the `curstruct` global — nested definitions need a save/restore stack (the
inner body must not clobber the outer struct's member-accumulation state). Check how
`typedefstruct`/`sdcl` set and use `curstruct` before designing the recursion.

- **Probe:** a struct with both a named nested `union { … } u;` and a named nested
  `struct { … } s;`; write/read members through `outer.u.field` and `outer.s.field`; verify
  `sizeof(outer)` and that the union members alias. Runtime probe (layout + aliasing are
  codegen, not just parse).

### Step 2 — re-run spike, re-scope §1b (the standing pause point)

**Do NOT start the aggregate/designated-initializer emitter (approved plan §1b, the
`dataitem()` design) without re-running the spike first.** After nested members parse, the
spike will almost certainly surface initializers (`{ … }` for structs/arrays, designated
`.field =` / `[i] =`) as the dominant remaining blocker — that is the substantial piece.
**Pause and re-scope §1b from the fresh tally** before writing the emitter.

## Guardrails

- Rebuild with `cd minic && make minic`; the local `yacc` prints conflict counts. The local
  miniyacc is picky: **no trailing `/* … */` comment after a rule's action `{ … }`** (put
  comments on their own line or inside the action), and it has a per-rule RHS length limit.
- Flow is system `cpp -P -nostdinc` + `minic/include` → `minic`. Spike harness:
  `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then read `summary.tsv` /
  `err/<file>.minic.err`. **minic's reported error line points just *past* the failing
  construct** — read a few lines back, and isolate a minimal repro through `minic -m medium`.
  Also: the harness `NORMALIZE` sed is a **no-op under macOS BSD sed** (`\b` unsupported), so
  treat the pp files as carrying canonical un-normalized type names.
- Keep edits localized to declarator / type-specifier / struct-member / param rules.
- Orthogonal pre-existing limits (don't chase unless a real consumer needs them): `void *`
  pointer comparison hits "void has no size"; `sizeof` only takes `( type | ident )`, not a
  general expression; only the struct-member array-dim sites take const-exprs (the other 13
  `[NUM]` sites still want a plain NUM).
