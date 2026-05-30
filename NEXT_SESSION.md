# Next session — MicroPython port: declarator name shadowing a typedef

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 82/132 OK** (was 77 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` to confirm before peeling more.
> Gate **82/82**.

## What changed last session (so you don't redo it)

§1d — **struct/union return-by-value** — is **done** (spike 77 → 82). A function whose return
type is a struct/union is now lowered System-V style with a hidden first pointer parameter
(caller-allocated result storage); the callee copies the returned aggregate through it and
returns the pointer; the caller allocates the slot, passes its address, and treats the call
result as the aggregate's address. All in the minic frontend as scalar QBE IR — **no i8086
backend / QBE changes**. New `emit_struct_copy(dst,src)` helper (factored from the struct-assignment
copy loop) + `alloc_sret_slot()`; `cur_fn_sret`/`cur_fn_sret_ctyp` state; `lval()` accepts a
struct-returning call so `f().field` works. Pinned by `sret_probe.c` (medium + large).

**Known orthogonal limitation (don't chase):** a struct member of type `long` (4 bytes) loses its
high word when the source words are opaque to QBE (a call result) — QBE forwards the `loadl`
through the word copy and reconstructs `lo | (hi<<16)`, but the i8086 backend lowers the final
`or` 16-bit-wide. This is the pre-existing `[[qbe-loadc-wordsize-i8086]]` family bug, independent
of the struct-return ABI. MicroPython's returned structs are all word/`size_t`-sized (`size_t` = 2
bytes on this target), so it doesn't gate the port.

## Scope for next session — declarator name shadowing a typedef (8 files, `py/scope.h`)

The new dominant spike blocker. Minimal repro (feed to `minic -m medium` directly):
```c
typedef unsigned short qstr;
int foo(qstr qstr, int x);     /* param named the same as the typedef */
```
→ `error:1: parse error`. The real MicroPython site (`py/scope.h`):
```c
id_info_t *scope_find_or_add_id(scope_t *scope, qstr qstr, id_info_kind_t kind);
```
`qstr` is both a typedef name and the parameter's name. C allows this: once the type-specifier
`qstr` has been consumed, the next identifier is in the *declarator* (ordinary-identifier)
namespace, so it's a parameter name, not a type. minic's lexer unconditionally returns `TNAME`
for any identifier that matches a typedef, so it sees `qstr qstr` as two type-names and the
grammar has no production for it.

This is a **lexer/parser disambiguation** problem, the same class as the existing `prevtok`
tag-namespace hack (an identifier right after `struct`/`union`/`enum` must lex as `IDENT` even if
a same-named typedef exists — see the `yylex()` wrapper around `yylex_inner()` and the
`typedef struct Foo Foo;` note in the running log). The fix is analogous but for declarator
position rather than tag position:

- After a type-specifier has been parsed, an identifier that begins a *declarator* (or names a
  parameter / variable / member) should lex as `IDENT`, not `TNAME`, even when it collides with a
  typedef. The tricky part is that the lexer can't always tell "declarator position" from
  "another type-specifier" with one token of lookahead (e.g. `unsigned int` vs `qstr qstr`).
- Practical heuristic that likely suffices for MicroPython: once a *complete* type-specifier has
  been seen in the current declaration, the **next** identifier is a declarator name → return
  `IDENT`. A typedef name can only appear as the *first* type token of a declaration (or after
  `struct`/`union`/`enum`/qualifiers), never immediately after another full type-specifier. Track
  this with a small "saw a type token, expecting declarator" flag in the `yylex()` wrapper,
  parallel to `prevtok`. Confirm it doesn't break `const qstr x` / `qstr *p` / `qstr (*fp)()` and
  the existing prototype/param grammar.
- Alternative (more surgical, less risk): allow `TNAME` in the parameter/declarator-name grammar
  positions and register it as an ordinary identifier there. This avoids lexer state but adds
  grammar productions (watch for new conflicts).

Prefer the lexer-state heuristic if it stays conflict-neutral; it generalizes to locals/members
(`int qstr;`), which MicroPython also has. Pin with a probe that declares a variable, a parameter,
and a struct member all named after a typedef, then uses each. Re-run the spike and re-scope.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is picky:
  **no `/* … */` between a production head and its `:`, none trailing a rule's action, and no
  comment block immediately preceding a production head**; per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **82/82**). Add/extend a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`) and reports CPP_FAIL honestly. **minic's error
  line is the parser's *lookahead* line, which lags the real construct by many lines** — e.g. the
  struct-return cluster reported `return source_line;` and this one reports `} scope_kind_t;`, both
  many lines before the true site. Confirm any suspected construct by feeding a **minimal isolated
  snippet** to `minic -m medium` directly. The `NORMALIZE` sed is a no-op under macOS BSD sed
  (`\b`) — minic must accept canonical names.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

- **`long` struct member from an opaque source** — see the §1d limitation above
  (`[[qbe-loadc-wordsize-i8086]]`).
- **Inline `100000L` literal** — the lexer drops the `L` bit and the int→long conversion sign-
  extends a 16-bit-truncated immediate (`[[minic-long-literal-int-vararg]]`); build large longs
  from small-literal arithmetic or stage through a long local.
- **Scalar array initializers** (`static const uint8_t rule_act_table[] = { ... };`) — the
  `T NAME[] = {...}` rule only dispatches struct / pointer element types; a scalar element type
  `die`s ("array initializer requires struct or pointer type"). Surfaced as a spike blocker
  (`compile.c`). Small extension to `emit_struct_array_data`'s sibling.
- **Inline tagged-aggregate definition + var + initializer** (`union ival { int i; long l; } U = {…};`)
  parse-errors — define the type first, then declare the variable.
- `sizeof` of a *local* array returns pointer size; `void *` pointer comparison hits "void has
  no size"; `sizeof` only takes `( type | ident )`, not a general expression; out-of-order /
  bitfield-member designated initializers `die()`.
