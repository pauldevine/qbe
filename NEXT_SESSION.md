# Next session — MicroPython port: remaining grammar blockers (post §1l)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 126/132 OK** (was 125 at the start of §1l). Re-run
> `bash build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **115/115**. 111 s/r, 0 r/r. `make check` green.

## What changed §1l (so you don't redo it)

**for-init inner-block scope** — closed compile.c's sibling for-loop double
definition (`for (int i …)` then `for (size_t i …)` in `c_del_stmt`). The three
C99 for-init rules were refactored to share a new `forinit_var: type IDENT '='`
nonterminal. Because all three for-init rules share that prefix, the state after
`type IDENT =` is a single-action state miniyacc **default-reduces without lexing
lookahead** — so `forinit_var`'s action (rename via `block_scope_decl` + varadd +
alloc, base type stashed in new global `forinit_basetyp`) runs *before* the
test/increment/body uses are lexed, letting the existing lexer-stamp rewrite them
to the renamed slot. No new conflicts (still 111 s/r, 0 r/r). Probe
`for_init_scope_probe.c` (medium + large; 5 cases incl. two-pointer/multi-scalar
regression guards). All in `minic/minic.y` + gate wiring; no i8086/QBE backend
changes. **The apostrophe-in-action-comment footgun this surfaced is now fixed**
(commit `a4a1fe7`): `cpycode` in `minic/yacc.c` is comment-aware, so action
comments can use `'`/`"`/braces freely — the old "avoid `'` `/` `[` `]`" rule no
longer applies (and `/` `[` `]` were never real). Generated `y.tab.c` differs
only in `#line` numbers (a latent off-by-one the old phantom-literal mode caused
is also corrected); minic's emitted SSA is unaffected.

## What changed last session (§1k — so you don't redo it)

Three commits, all in `minic/minic.y` (+ gate wiring), no i8086/QBE backend changes,
no new grammar conflicts (still 111 s/r, 0 r/r):

1. **Inner-block lexical scope via alpha-renaming** (`f51894e`) — a local name reused
   across distinct blocks with *different* types used to die "double definition" (minic
   has one flat local symtab and emits bodies lazily, resolving a USE by name at emit
   time). Now `block_scope_decl()` (called from the stmt-level `type IDENT ;` and
   `type IDENT = expr ;` rules) renames the colliding declarator to `name$N`, pushes a
   rename binding, and the lexer stamps that mangled name into every subsequent *use*
   of the source name; the binding pops at block exit (`rename_pop_closed()` at the top
   of `yylex`, keyed on `brace_depth`). **Renaming fires ONLY on a different-type
   collision**, so same-type re-decl still folds in `varadd` (stevie for-bodies; stevie
   size unchanged) and the 120 already-passing files are byte-identical. Verified
   empirically that miniyacc default-reduces those decl rules *before* lexing the next
   token (so the rename is active when following uses are lexed). **Flips
   mpprint/runtime/sequence/vm**; advances parse past its `pn` collision. Probe
   `block_scope_probe.c` (medium + large). See `[[minic-inner-block-scope]]`.
2. **Multi-scalar-declarator C99 for-init** (`6dc14b2`) — new rule
   `FOR '(' type IDENT '=' expr ',' init_decllist ';' …` allocates each declarator and
   chains the inits into one comma-expr mkfor runs at loop entry; distinguished from the
   two-pointer for by the token after the first comma (IDENT vs `*`). **Flips gc.**
   Probe `for_multiscalar_probe.c`.
3. **Function-local fnptr typedef** (`6dc14b2`) — new `dcls` production for
   `typedef T (*name)(…);` in block scope (name enters the single global typedef table).
   **Advances stream** past its grammar blocker. Probe `local_typedef_probe.c`.
4. **Out-of-order designated array init + trailing comma** (`dcd6742`) — `agg_emit_array`
   now buffers values into index-addressed slots (positional → running cursor; `[k]=v`
   sets the cursor per C99; gaps coalesce into one zero-fill so in-order arrays are
   byte-identical), and `initlist` accepts a trailing comma. **Advances objtype** past
   two blockers. Probe `array_designate_probe.c` (medium; file-scope address-of is
   near-only).

## Scope for next session — remaining 6 failures

Per-file actual MESSAGES (the summary.tsv source line lags the lookahead — read the real one):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  ./minic/minic -m medium < build/mp-spike/pp/$base.pp.c >/dev/null 2>/tmp/e.err
  printf "%-12s %s\n" "$base" "$(grep -m1 error: /tmp/e.err)"
done
```
Current: **binary, modbuiltins, objlist, objtype, parse, stream.** Several are
multi-blocker; ordered roughly by tractability below. (compile — for-init scope —
was closed in §1l; see above.)

### binary / objlist / modbuiltins — anonymous struct/union as a type, RISKY (r/r)
- **binary**: inline offsetof cast `((size_t)&((struct { char c; short t; } *)0)->t)`.
- **objlist**: anon-struct-typed local `struct { mp_arg_val_t key, reverse; } args;`.
- **modbuiltins**: a function-local `enum { ARG_sep, ARG_end, ARG_file };` **and** an
  anon-union-typed local `union { mp_arg_val_t args[N]; size_t len[2]; } u;` (needs both).

§1k **tried** `type: typedefstructstart smembers '}'` (and the union analogue), reusing
the existing typedef markers — this produced **76 reduce/reduce conflicts** (the shared
`STRUCT '{'` / `typedefstructstart` prefix is ambiguous against the typedef-struct rule
once `smembers '}'` can reduce to either `type` or feed `… '}' IDENT ';'`). **Reverted.**
A workable approach needs a grammar that doesn't share the reduce: e.g. a distinct
`anon_aggr_type` path gated so the parser commits early, or restructuring `smembers` to
avoid the late ambiguity. High risk in miniyacc; justify every new conflict, **no new
r/r**. modbuiltins' local-enum half is independently addable (an `enum { enums }` decl in
`dcls`/stmt) but won't flip modbuiltins alone (still needs the anon union).

### objtype — multi-blocker, MEDIUM
§1k cleared its trailing-comma and out-of-order-designator blockers. Next blocker
(line ~1771): a **compound literal with nested braces assigned through a deref**:
`*o = (mp_obj_super_t) {{type_in}, args[0], args[1]};`. Needs the `(T){…}` compound-literal
path to (a) accept a nested `{…}` initializer item and (b) work as the RHS of `*p = …`.
Likely more blockers after.

### parse — downstream blocker in mp_parse, unknown
§1k cleared the `pn` double-def. The file now parses cleanly through `push_result_rule`
but errors at the **start of `mp_parse`** (line ~2717). Bisect inside `mp_parse` with the
lag-proof technique below to find the real construct; it was masked by the earlier
double-def so it's uncharacterised.

### stream — NOT a minic bug (spike-harness artifact)
§1k's local fnptr typedef cleared its grammar blocker; stream now errors at
`int whence = SEEK_SET;` — **SEEK_SET is never defined in the preprocessed TU**, a
spike `clang -E` include-path gap (real MicroPython gets it from `<stdio.h>`). minic
correctly reports an undefined variable. stream cannot flip in the spike without fixing
the harness includes (out of scope); don't chase it as a minic issue.

### How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct.
Forward-bisect on column-0 `}`/`;` boundaries; the FIRST clean boundary whose prefix
errors NOT-at-the-cut brackets the construct. Extract into a standalone snippet (stub the
few typedefs) and confirm under `minic -m medium`. To find a "double definition" name
fast, temporarily add `fprintf(stderr, …)` before the `die` in `varadd` printing
`v`/`ctyp`/`brace_depth` (note: the printed `brace_depth` lags for a last-stmt decl —
capture lex-time depth from the node if you need the true block depth).

## Guardrails (unchanged)
- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **111
  s/r, 0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. miniyacc is
  picky: no `/* … */` between a production head and its `:`, none trailing a rule's
  action, no comment-only action body. (Action-body comments may now contain `'` `"`
  `/` `[` `]` and braces freely — `cpycode` is comment-aware as of `a4a1fe7`; the old
  restriction is lifted.)
- Run `tools/test-dos.sh` (must stay **115/115**) and `make check` (SSA, "All is fine!")
  at the **repo root** (not minic/). Add or extend a probe per runtime-bearing feature;
  the gate runs ~5 min in DOSBox — run it in the background and wait.
- Spike harness uses **`clang -E`**. Read the real message by running minic directly on
  `build/mp-spike/pp/<file>.pp.c`, not the lagged summary.tsv line.
- DOSBox capture is occasionally flaky. If a `--model=large` probe diff fails once, re-run.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `&global` data items are
  near-only, so probes that take a static address are medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Deep block-scope shadow of an already-renamed name** — §1k's alpha-renaming handles
  sibling blocks, single-level shadow (deferred pop), and inner-then-function-scope
  collisions; a *declarator* lexed while an outer rename of the same name is active
  (double shadow) can mis-stamp. Not in any of the 12 MicroPython files. See
  `[[minic-inner-block-scope]]`.
