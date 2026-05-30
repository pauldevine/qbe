# Next session — MicroPython port: remaining grammar blockers (post §1h)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 111/132 OK** (was 100 at the start of last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **89/89**. 109 s/r, 0 r/r. `make check` green.

## What changed last session (§1h — so you don't redo it)

Ten independent minic frontend wins (spike 100 → 111), all in `minic/minic.y`; no
i8086/QBE backend changes. Pinned by `minic/dos/examples/mp_grammar_probe.c`
(medium + large; golden in `minic/dos/tests/`). Four commits: `8c6bbf4`, `cc46af4`,
`444cd24`, `c858bd2`.

1. **Const-expr local array dimensions** — `T a[expr]` folds via `const_eval` (was bare
   NUM). All local array-dim rules + the bare global-array rule switched NUM→expr to
   avoid a shift/reduce conflict on a bare-literal dim.
2. **sizeof(arrayvar)** — varh records each array declarator's total byte size (new
   `arraybytes` field + `var_set_arraybytes`/`var_arraybytes`); was a silent miscompile
   (returned pointer size). Covers local / file-scope scalar / struct arrays.
3. **Adjacent string-literal concatenation** — `"a" "b"` lexes as one string (peek past
   whitespace for another quote, append into the same buffer).
4. **`static __attribute__((...)) T f(...)`** — attribute after `static` (new
   `STATIC attrspec type_and_ident_noattr` production).
5. **`_Static_assert(general-const-expr, msg)`** — condition is a folded expr; non-foldable
   offsetof comparisons are accepted+skipped via `constfoldable()`.
6. **Local aggregate initializer** — `struct P p = {…}` (+ `.field=`) desugars to a
   compound-literal assignment (dcls + block-statement contexts).
7. **File-scope designated array init** — `static const T t[] = { [IDX]=v, … }` via
   `sai_designate` (zero-fills gaps; in-source-order only — out-of-order dies loudly).
8. **Sized file-scope array init** — `T t[N] = {…}` with fewer items than N
   (`sai_pad_to_count` zero-fills the tail).
9. **General `sizeof(expr)`** — new `SIZEOF '(' expr ')'` routes through `typeof_expr()`,
   which runs the normal `expr()` emitter with `of` redirected to /dev/null and the scratch
   counters restored (sizeof is unevaluated, so discarding the code is correct). Unblocks
   the count idiom `sizeof(arr)/sizeof(arr[0])` and `sizeof(*ptr)`. A bare array var still
   reports its whole-array size (the `'V'` + `var_arraybytes` short-circuit).
10. **Local unsized array init + block-scoped array init + arithmetic init items** —
    `T a[] = {x,y}` (count inferred), `if(c){ int a[N]={…}; }` (deferred stores via
    `mk_local_array_init` comma-chain + `mkstmt(Expr,…)`), and `inititem` widened from
    `pref` to `expr` so `{ x, x*2 }` / `.field = expr` work.

**Found, not fixed (orthogonal i8086 backend bug):** two `/` or `%` divisions feeding the
*same call* corrupt the first result (div clobbers AX/DX without telling rega; same shape
as the imul DX-clobber family). See `[[i8086-two-div-one-call-clobber]]`. Fix is in
i8086/emit.c div/rem handlers (push/pop or kl_save_axdx bracket). `mp_grammar_probe`
deliberately prints each count separately to sidestep it.

## Scope for next session — remaining 21 failures

Per-file actual MESSAGES (read `build/mp-spike/err/<file>.minic.err`, NOT the lagged
summary.tsv source line):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  msg=$(grep -m1 'error:' build/mp-spike/err/$base.minic.err | sed -E 's/.*error:[0-9]*:?//')
  printf "%-14s %s\n" "$base" "$msg"
done | sort -k2
```
Current: **15 parse error**, 4 double definition, 1 void-has-no-size (`malloc`),
1 unknown-struct-type (`modsys`).

### TWO highest-value targets (each unblocks several files)

**A. Struct-array initializer with designated members** — `{ {.f=v, …}, {.f=v, …} }`.
Confirmed blocker: `struct P arr[] = { {.a=1}, {.a=2} }` parse-errors today, even though
file-scope designated structs, nested designated, and local designated structs all work
(landed §1h #6). The gaggr / struct-array (`emit_struct_array_data`, `sai_*`) path needs
per-element `.field =` designator support. Hits **objlist** (`allowed_args[]`),
**modbuiltins**, **objtype** (`member[2]`/lookup table), **runtime**, **objgenerator**,
**objdict**. Likely the single biggest remaining win.

**B. Inner-block scope** — the 4 `double definition` files (**vm, compile, sequence,
mpprint**) reuse a local name across sibling blocks with *different* types
(`{const byte *t;} … {size_t t;}`) or distinct `for (size_t i …)` inits. minic has one flat
per-function `varh`. Extend the §1e `var_islocal()` + lexer `brace_depth`/`pending_varclr`
machinery to **push/pop a scope at every `{`/`}`**: on block exit, drop names declared in
that block and restore any shadowed outer binding. **DO NOT** just relax the `varadd`
double-definition check to overwrite `varh[].ctyp` — that was tried and reverted in §1g
because minic emits in lexical order and the earlier block's already-emitted uses are fine,
but a later outer-scope use of the name would see the wrong (inner) type → silent
miscompile. Needs a real save/restore scope stack.

### Smaller, file-specific blockers (each distinct; isolate a snippet)
- **Local enum declaration** — `void f(){ enum { A, B }; … }` parse-errors. Needed by
  **modbuiltins** (`enum { ARG_sep, ARG_end, ARG_file };`). Add an enum-decl alternative
  to the statement/dcls rules (register the constants like file-scope enums).
- **malloc** `void has no size` — isolate; likely a `sizeof(void)` / `void`-typed object.
- **modsys** `unknown struct type` — a struct tag used before its definition; isolate.
- **obj** — `static const mp_obj_type_t *const types[] = { 0, &mp_type_int, … }` then
  `types[(uintptr_t)o_in & 0xf]`. The isolated `*const`-ptr-to-struct array with `0`+`&sym`
  items parses fine in isolation; re-bisect — the true site is elsewhere in the function.
- **parse** (line ~2336) — a very long `?:` ternary chain of `PADn_x >= 0x100 ? RULE_x : …`
  as a file-scope `static const size_t` initializer; check the const-expr/ternary path and
  initializer size.
- **bc** — declarations inside `do { … } while (0)` macro blocks with nested
  `for (unsigned n = …)`; likely overlaps inner-block scope (target B) or a decl-in-block
  edge.
- **gc / qstr / repl / binary / stream / objstr** — re-bisect (their reported lines lag;
  use the technique below). Several are `for (T x = …; …)` C99 for-init across blocks
  (overlaps B) or designated struct-array (overlaps A).

### How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct.
Forward-bisect: for each column-0 `}`/`;` boundary N, test `head -n N file.pp.c | minic -m
medium`; the FIRST boundary whose prefix errors brackets the construct (it lives between the
previous clean boundary and N). Extract into a standalone snippet (stub the few typedefs)
and confirm under `minic -m medium`.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **109
  s/r, 0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. miniyacc is picky:
  no `/* … */` between a production head and its `:`, none trailing a rule's action, no
  comment-only action body; **comments inside an action body must avoid `/`, `[`, `]`
  characters** (a `sizeof(arr)/sizeof(arr[0])` comment broke the yacc parse in §1h — keep
  action comments plain prose); per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **89/89**) and `make check` (SSA, "All is fine!").
  Add or extend a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`**. Read the real message from
  `build/mp-spike/err/<file>.minic.err`, not the lagged summary.tsv line.
- DOSBox capture is occasionally flaky (rare truncated output / SIGABRT). If a `--model=large`
  probe diff fails once, re-run 2–3× before believing it.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `mp_aggregate_probe` is medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Out-of-order designated array init** — `{ [3]=…, [0]=… }` dies (sai_designate is in-order only).
