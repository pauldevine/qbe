# Next session — MicroPython port: remaining grammar blockers (post §1j)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 120/132 OK** (was 118 at the start of §1j). Re-run
> `bash build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **106/106**. 111 s/r, 0 r/r. `make check` green.

## What changed last session (§1j — so you don't redo it)

Three independent minic wins (spike 118 → 120), all in `minic/minic.y` (+ a one-line
`minic/yacc.c` change); no i8086/QBE backend changes. Three commits, each with its own
probe (medium + large), gate 100 → 106:

1. **Tentative-definition reuse** (`53b3942`) — a file-scope uninitialized definition
   (`static const T x;`) followed by an initialized one (`static const T x = {…};`) errored
   with "double definition". Globals are buffered in `ini[]`/`gloname[]` and emitted at end
   of translation, so the initialized definition reuses the tentative slot. New varh
   `istentative` field; `mark_tentative()` flags the uninitialized global; `glo_redef_index()`
   returns its buffered glo index (clearing the flag) so `emit_global_aggregate()` overwrites
   `ini[idx]` instead of double-emitting `data $x`. Probe `tentative_def_probe.c`. **Flips
   py/objdict.c.**
2. **Const-expr file-scope scalar initializers + deeper parser stack** (`e890cc9`) — replaced
   the `= NUM`/`= -NUM`/`= (NUM)`/`= (-NUM)` rules with a single `= expr ;` rule that folds
   via `const_eval` (byte-identical output for the old cases; `= STR` and `= gaggr` keep
   their own rules, bare NUM/STR still reduce via shift-resolution). Unblocks py/parse.c's
   `static const size_t X = PAD1 >= 0x100 ? RULE_1 : … : 0;` (~160-level nested ternary over
   enum constants). The deep right-associative ternary also overflowed the generated parser's
   `StackSize=500` at ~120 levels → raised to **4000** in `yacc.c` (host-side stack, cheap;
   regenerated y.tab.c picks it up). +2 s/r (NUM-vs-expr, STR-vs-expr; shift wins), 0 r/r.
   Probe `const_init_probe.c`. **Advances py/parse.c** (next blocker is the double-def scope
   issue below).
3. **Comma expression in C99 for-init increment** (`0e2d9db`) — the C99 for-init rules used
   `exp0` (no comma) for test/increment while the plain `for` used `comma_exp0`, so
   `for (size_t i = n; i > 0; i--, ptrs++)` parse-errored. Both C99 for rules (single + the
   two-pointer-declarator form) now use `comma_exp0`. Probe `for_comma_inc_probe.c`. **Flips
   py/bc.c; advances py/gc.c.**

## Scope for next session — remaining 12 failures

Per-file actual MESSAGES (read the real one; the summary.tsv source line lags the lookahead):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  ./minic/minic -m medium < build/mp-spike/pp/$base.pp.c >/dev/null 2>/tmp/e.err
  printf "%-12s %s\n" "$base" "$(grep -m1 error: /tmp/e.err)"
done
```
Current: **6 double definition** (compile, mpprint, parse, runtime, sequence, vm) +
**6 parse error** (binary, gc, modbuiltins, objlist, objtype, stream).

### HIGHEST-VALUE target — inner-block scope (6 files) — READ THIS, the model is subtle

**compile, mpprint, parse, runtime, sequence, vm** all hit `double definition`: a local name
reused across **sibling braced blocks** with *different* types. Confirmed all are pure
sibling blocks (no live outer shadow), e.g. mpprint's switch cases
`case 'c': { char str; }` … `case 's': { const char *str; }`, sequence's swap idiom
`{ const byte *t=…; } { size_t t=…; }`, vm's two `case` blocks with `cause` as
`mp_obj_t`/`mp_int_t`. compile is **for-init** scope: `for (size_t i …)` vs `for (int i …)`
in the same function.

**CRITICAL — why a scoped symbol table alone is NOT enough (the §1g note understated this):**
minic emits the function body **lazily** — `stmt($4, …)` runs in the function rule's action,
*after* the whole body is parsed (see `typed_decl_rest: ansi_func_proto '{' dcls stmts '}'`).
Variable USES are `'V'` AST nodes that store only the source **name**; their type is resolved
at **emit time** by `varget(name)` (minic.y `lval()` case `'V'`, ~line 3120). So at emit time
only ONE `varh` entry per name survives, and a push/pop scope stack that drops the inner
binding would make the *earlier* sibling block's uses resolve to the *wrong* surviving type.

**The fix that actually works: alpha-renaming at parse time.** When an inner-block decl
(parser block-depth ≥ 1) collides with an existing `varh` entry, give the inner decl a unique
mangled name (e.g. `t$3`), `varadd` it under the mangled name, emit `%t$3 =alloc…`, and push
a **rename binding** `t → t$3` onto a scope-rename stack. The lexer stamps the mangled name
into each `'V'` use node it creates (consult the rename map in `yylex_inner` right where it
builds the `'V'` node, ~line 7812) so the AST carries already-resolved names and lazy
`varget(t$3)` is correct. Because uses are lexed *after* the decl's reduce (which registers
the rename), the rename is active when the use node is built — works for sibling blocks.

**Shadow case (outer use after inner redecl) — handle it, don't miscompile.** Use a
**deferred rename-pop**, mirroring the existing function-level `pending_varclr` defer
(minic.y ~7407): when the lexer's `}` brings block-depth from D to D-1, schedule the pop of
all rename entries tagged depth ≥ D to run at the *next* `yylex` call — i.e. *before* the
lexer builds the next (outer) use node. Traced: `size_t t; { const byte *t; use(t);} use(t);`
→ inner `use(t)`→`t$1`; deferred pop restores `t`→canonical before the outer `use(t)` is
lexed → outer resolves to `size_t`. So the lexer-lag that breaks a naive scheme is exactly
absorbed by the same defer the function-level clear already uses.

**Depth tagging must use a PARSER counter, not the lexer's `brace_depth`.** A decl that is the
last statement before `}` reduces with lookahead `}` (lexer already decremented), so tag each
rename with a depth bumped by parse actions: add a `blockstart: '{' { pdepth++; }` nonterminal
and replace `stmt: '{' stmts '}'` with `stmt: blockstart stmts '}' { pdepth--; … }` (the
`structstart`/`enumstart` idiom — keeps it off the function-body `'{' dcls stmts '}'` rule).
For-init gets its own scope likewise. Reset `pdepth`/rename-stack in the `init*` markers
(where `varclr()` is called).

**DO NOT** just overwrite `varh[].ctyp` (tried+reverted §1g). Decl rules to hook for renaming:
the stmt-level `type IDENT …` family (~6640–6760) and the two C99 for-init rules (~6910, 6928).
Probe heavily: sibling-different-type, sibling-same-type (**stevie for-bodies regression
check** — they currently share one `%pos` via the same-type fold at minic.y:413; renaming
changes the emitted names but not runtime — verify the gate + a stevie smoke), nested blocks,
for-init siblings, and a genuine shadow (outer-after-inner) for the deferred-pop path. This is
the single biggest remaining win (6 files) but the most regression-prone — verify 106/106 +
`make check` + spike stays ≥120 the whole way.

### Smaller, file-specific blockers (each confirmed in isolation)

- **gc** — multi-scalar-declarator C99 for-init: `for (size_t block=0, len=0, len_free=0; …)`.
  Needs a declarator-list generalization of the for-init rules. Tangles with the existing
  single + two-pointer for rules (both start `type IDENT '=' expr ','`); the two-pointer rule
  folds the first `*` into the type and ignores the second, so a clean unified `forinitlist`
  must special-case the pointer form. Justify any new s/r; **no new r/r**. (gc's *first*
  blocker, the comma increment, is already fixed §1j.)
- **binary** — anonymous struct in a cast (inline `offsetof`):
  `((size_t)&((struct { char c; short t; } *)0)->t)`. Needs `STRUCT '{' smembers '}'` usable
  as a type in a cast/sizeof context. Risky in miniyacc — `STRUCT '{'` already appears in
  `typedefstructstart`/`nested_s_begin`; justify any new s/r.
- **objlist** — anonymous struct-typed local variable: `struct { mp_arg_val_t key, reverse; }
  args;`. Same `STRUCT '{'` conflict surface as binary; a `type: STRUCT '{' smembers '}'`
  production would cover both.
- **modbuiltins** — local enum declaration (`enum { ARG_sep, ARG_end, ARG_file };` inside a
  function) **and** an anonymous union-typed local
  (`union { mp_arg_val_t args[N]; size_t len[2]; } u;`). Needs both.
- **objtype** — local aggregate init with an `offsetof` value and an array-decay member:
  `struct class_lookup_data lookup = { .obj=self, .slot_offset=offsetof(T,m), .dest=member, … };`
  where `member` is a local `mp_obj_t member[2]`. The simple local-designated-struct case
  already works; isolate which item (the offsetof value or the `.dest=member` array name) the
  local compound-literal-desugar path chokes on.
- **stream** — function-local typedef of a function-pointer type:
  `typedef mp_uint_t (*io_func_t)(mp_obj_t obj, void *buf, mp_uint_t size, int *errcode);`
  inside a function body. minic typedefs are file-scope only; add a statement/dcls-level
  typedef alternative.

### Incidental limitations found this session (not currently gating, but real)
- **Multi-declarator with an initializer on the FIRST declarator inside an inner block**:
  `{ int j = 0, k = 100; }` parse-errors. The stmt-level multi-decl rule is
  `type IDENT ',' ext_decllist` — it requires the first declarator to be a bare `IDENT`
  (no init) before the comma, so `int a = 0, b = …;` fails as a statement (works at
  function-top via `dcls`). Easy-ish fix; no in-tree consumer among the 12 yet.

### How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct.
Forward-bisect on column-0 `}`/`;` boundaries; cuts mid-construct error at the cut line, so
the FIRST clean boundary whose prefix errors NOT-at-the-cut brackets the construct. For tight
brackets bisect every `;`. Extract into a standalone snippet (stub the few typedefs) and
confirm under `minic -m medium`.

## Guardrails (unchanged)
- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **111 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. miniyacc is picky: no
  `/* … */` between a production head and its `:`, none trailing a rule's action, no
  comment-only action body; **comments inside an action body must avoid `/`, `[`, `]`**.
- Run `tools/test-dos.sh` (must stay **106/106**) and `make check` (SSA, "All is fine!") at
  the **repo root** (not minic/). Add or extend a probe per runtime-bearing feature; the gate
  runs ~5 min in DOSBox — run it in the background and wait.
- Spike harness uses **`clang -E`**. Read the real message by running minic directly on
  `build/mp-spike/pp/<file>.pp.c`, not the lagged summary.tsv line.
- DOSBox capture is occasionally flaky. If a `--model=large` probe diff fails once, re-run.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `&global` data items are
  near-only, so probes that take a static address are medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Out-of-order designated array init** — `{ [3]=…, [0]=… }` dies (agg_emit_array is in-order).
