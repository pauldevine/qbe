# Next session — MicroPython port: next grammar blocker (post typedef-shadow)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 87/132 OK** (was 82 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **84/84**.

## What changed last session (so you don't redo it)

§1e — **two independent wins**, both landed (spike 82 → 87):

1. **Typedef-name shadowing (lexer disambiguation).** C lets an ordinary identifier
   (parameter / variable / struct member / member-access) share a typedef's spelling; once a
   type-specifier is consumed the next identifier is in the declarator namespace. minic's lexer
   used to return `TNAME` for any typedef-matching identifier, so `qstr qstr` (the real
   `py/scope.h` shape: `id_info_t *scope_find_or_add_id(scope_t *scope, qstr qstr, …)`) was two
   type-names → parse error. Fixed with three rules in the `yylex()`/`yylex_inner()` wrapper
   (`minic/minic.y`):
   - After a complete type-specifier token (a type keyword or a `TNAME`), a typedef-matching
     identifier lexes as `IDENT` (it begins a declarator). typhget is probed into a scratch
     ctyp (not `yylval`) so the `IDENT` node value survives; `g_td_array{dim,elem}` are cleared.
   - A name already in scope as a local/parameter (`var_islocal()`, a new helper) lexes as
     `IDENT` in body uses. The previous function's locals are dropped when its body's closing
     `}` is consumed — but **deferred one token** (`pending_varclr` + `brace_depth` in the
     wrapper), because `}` is the lookahead that reduces/emits the body's last statement (which
     still reads locals via `varget`). Clearing at `}` rather than only at the next function's
     `init` marker means a typedef-named parameter type in the *next* function isn't shadowed by
     a stale local.
   - After `.` / `->` (the member namespace), an identifier always lexes as `IDENT`.
   0 new conflicts (still 108 s/r, 0 r/r). Pinned by `typedef_shadow_probe.c` (medium + large).

2. **Capacity bumps.** `NStruct` 64→256, `NTyp` 128→512 (`minic/minic.y`). This — **not** the
   shadow fix — is what actually flipped the 8 `scope_kind_t` files. Their reported source line
   `} scope_kind_t;` was a red herring (lookahead lag); the real `die()` message was
   *"too many struct/union definitions"* then *"too many typedefs"*. **Lesson reinforced: read
   the actual error MESSAGE from `build/mp-spike/err/<file>.minic.err`, not just the source line
   in `summary.tsv`.** minic is host-compiled so large tables are cheap.

## Scope for next session — the new dominant grammar blocker

Run the spike, then per-file error messages:
```sh
for e in build/mp-spike/err/*.minic.err; do
  base=$(basename "$e" .minic.err)
  grep -q 'parse error' "$e" && {
    el=$(grep -m1 error: "$e" | sed 's/error:\([0-9]*\).*/\1/')
    printf "%-16s L%-5s %s\n" "$base" "$el" "$(sed -n "${el}p" build/mp-spike/pp/$base.pp.c | sed 's/^ *//' | cut -c1-70)"
  }
done
```
~31 files report a bare `parse error`. The largest reported cluster (~6 files: `compile`,
`emitcommon`, `emitnative`, `emitnx86`, `nativeglue`, `persistentcode`) points at
`int (*vprintf_)(const mp_print_t *print, const char *fmt, va_list args);` — **but that line is
lookahead-lagged and the isolated snippet PARSES FINE**, so it is *not* the real site. Don't trust
it; bisect.

### How to find the true site (the lag-proof technique)
A `head -N | minic` of a prefix that ends *inside* a struct/function fails by truncation, masking
real errors. So only test prefixes that end at a **top-level (column-0) `}` or `;`** boundary, and
treat a prefix as a real failure only when its reported error line is *well before* `N` (truncation
reports a line ≈ `N`). Walk those boundaries upward; the first one whose reported error line stays
fixed and small is just past the true construct. Then extract that construct into a standalone file
(with the few typedefs it references stubbed) and confirm it reproduces under `minic -m medium`
directly. (This is exactly how last session found the `scope_kind_t` files were a capacity limit,
not the shadow.)

### Other known clusters still open (likely quicker wins)
- **Scalar array initializers** — `static const uint8_t rule_act_table[] = { … };` (`parse`,
  `scope`, `objmodule`, `unicode`). The `T NAME[] = {…}` rule only dispatches struct / pointer
  element types; a scalar element type `die`s ("array initializer requires struct or pointer
  type"). Small extension to `emit_struct_array_data`'s sibling. Several files, self-contained.
- A handful of `unsupported operation in constant expression`, `bitfield initializer
  unsupported`, `invalid assignment`, `void has no size`, `unknown struct type`,
  `unsupported address-of in static initializer`, `double definition` — 1–3 files each. Triage
  by the actual error message.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is picky:
  **no `/* … */` between a production head and its `:`, none trailing a rule's action, and no
  comment block immediately preceding a production head**; per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **84/84**) and `make check` (SSA, "All is fine!"). Add/extend
  a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`) and reports CPP_FAIL honestly. **minic's error
  line is the parser's *lookahead* line, which lags the real construct by many lines.** Confirm
  any suspected construct by feeding a **minimal isolated snippet** to `minic -m medium` directly,
  and read the real message from `build/mp-spike/err/<file>.minic.err`. The `NORMALIZE` sed is a
  no-op under macOS BSD sed (`\b`) — minic must accept canonical names.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

- **`long` struct member from an opaque source** — `[[qbe-loadc-wordsize-i8086]]`; MicroPython
  structs are word/`size_t`-sized so it doesn't gate.
- **Inline `100000L` literal** — lexer drops the `L` bit and the int→long conversion sign-extends
  a 16-bit-truncated immediate (`[[minic-long-literal-int-vararg]]`); build large longs from
  small-literal arithmetic or stage through a long local.
- **Inline tagged-aggregate definition + var + initializer** (`union ival { int i; long l; } U = {…};`)
  parse-errors — define the type first, then declare the variable.
- `sizeof` of a *local* array returns pointer size; `void *` pointer comparison hits "void has
  no size"; `sizeof` only takes `( type | ident )`, not a general expression; out-of-order /
  bitfield-member designated initializers `die()`.
