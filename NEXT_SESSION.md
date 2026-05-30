# Next session — MicroPython port: remaining grammar blockers (post §1i)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 118/132 OK** (was 111 at the start of §1i). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **100/100**. 109 s/r, 0 r/r. `make check` green.

## What changed last session (§1i — so you don't redo it)

Six independent minic frontend wins (spike 111 → 118), all in `minic/minic.y`; no
i8086/QBE backend changes. Six commits, each with its own probe (medium + large unless
noted), gate 89 → 100:

1. **Designated struct/union array initializers** (`b229efb`) — `T arr[]={ {.f=v}, … }` at
   file scope and function-local `static`, routed through the generic aggregate machinery
   (`emit_global_array`/`emit_static_array`/`build_array_init` → `agg_emit_array`). Byte-
   identical to the old sai_* round-robin for plain cases (minic never pads structs); adds
   per-element `.field=` designators incl. a union member set by a designator (the
   `mp_arg_t allowed_args[]` idiom). Probe `mp_designated_array_probe.c`.
2. **Pointer comparisons don't scale** (`759ab6f`) — `prom()` fell through to the
   pointer-arith Scale path for `== != < <=`, computing `SIZE(DREF(void*))` → fatal "void
   has no size" (blocked `if (ptr == 0)`). Now returns the pointer operand's type directly
   for comparison ops. Also fixed a latent spurious-`mul` on ptr<->ptr compares. Probe
   `void_ptr_cmp_probe.c`. **Flips py/malloc.c.**
3. **extern struct fwd-decl + empty top-level `;`** (`3929eca`) — `extern struct TAG name;`
   (+ `[]`/`*` forms) forward-declares an unseen tag instead of dying; a bare `;` at file
   scope is an empty declaration (`prog ';'`). Probe `extern_struct_probe.c`. **Flips
   py/modsys.c.**
4. **static decls inside nested blocks** (`5b6c709`) — statement-scope `static T v=init;`,
   `static T a[]={…};`, `static T a[N]={…};`, `static T a[N];` (were only in function-top
   `dcls`). Lowered to mangled file-scope globals. Probe `block_static_probe.c` (medium-only;
   `&global` items need far reloc). **Flips py/obj.c.**
5. **Two-pointer-declarator C99 for-init** (`590c6ed`) — `for (T *a=e1, *b=e2; …)` (both vars
   share the pointer type; inits via a comma node). Probe `for_multidecl_probe.c`. **Flips
   py/qstr.c, py/map.c.**
6. **char-array init from string literal** (`f9f3692`) — `char NAME[]="…";` at file scope and
   static-local (was only the `char *p="…"` pointer init; even file-scope array form
   failed). Emits NUL-terminated bytes; `sizeof` uses escape-aware `strlit_bytelen`. Probe
   `string_array_probe.c`. **Flips py/objstr.c, py/repl.c.**

## Scope for next session — remaining 14 failures

Per-file actual MESSAGES (read `build/mp-spike/err/<file>.minic.err`, or rerun minic directly
on `build/mp-spike/pp/<file>.pp.c`; the summary.tsv source line lags the lookahead):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  ./minic/minic -m medium < build/mp-spike/pp/$base.pp.c >/dev/null 2>/tmp/e.err
  printf "%-12s %s\n" "$base" "$(grep -m1 error: /tmp/e.err)"
done
```
Current: **6 double definition** (compile, mpprint, objdict, runtime, sequence, vm) +
**8 parse error** (bc, binary, gc, modbuiltins, objlist, objtype, parse, stream).

### HIGHEST-VALUE target — inner-block scope (6 files)

**vm, compile, sequence, mpprint, objdict, runtime** all hit `double definition`: a local
name reused across sibling blocks with *different* types (`{const byte *t;} … {size_t t;}`)
or distinct C99 `for (size_t i …)` inits. minic has one flat per-function `varh`
(`varadd` at minic.y:372; `varclr` at :333; the same-type re-decl is folded at :413, but
different-typed dies at :418).

Extend the §1e `var_islocal()` + lexer `brace_depth`/`pending_varclr` machinery to
**push/pop a scope at every `{`/`}`**: tag each local `varh` entry with a scope depth; on
block exit, drop names declared at that depth and **restore any shadowed outer binding**
(needs a save/restore stack — a flat table holds one entry per name, so a shadowed outer
binding must be stashed and reinstated). **DO NOT** just relax the `varadd` double-def
check to overwrite `varh[].ctyp` — tried and reverted in §1g; minic emits in lexical order,
so a later outer-scope use of the name would see the wrong (inner) type → silent
miscompile. This is the single biggest remaining win but the most regression-prone — do it
fresh with care, lots of probes, and verify the full 100/100 gate stays green.

### Smaller, file-specific blockers (each confirmed in isolation)

- **Anonymous struct/union-typed local variable** — `struct { … } v;` / `union { … } v;`
  parse-errors (no `type: STRUCT '{' … '}'` production; only typedef/sdcl/nested contexts
  define inline bodies). Needs `objlist` (`struct { mp_arg_val_t key, reverse; } args;`) and
  `modbuiltins` (`union { mp_arg_val_t args[N]; size_t len[2]; } u;`). Risky in miniyacc —
  `STRUCT '{'` already appears in `typedefstructstart`/`nested_s_begin`; justify any new s/r.
- **Local enum declaration** — `void f(){ enum { A, B, C }; … }` parse-errors. Needed by
  **modbuiltins** (`enum { ARG_sep, ARG_end, ARG_file };`). Add an enum-decl alternative to
  the statement / dcls rules (register the constants like file-scope enums).
- **Local aggregate init with offsetof value + array-decay member** — **objtype**:
  `struct class_lookup_data lookup = { .obj=self, .slot_offset=offsetof(T,m), .dest=member,
  … };` where `member` is a local `mp_obj_t member[2]` (decays to ptr). The simple cases
  (local sized array partial init; local designated struct) already work — isolate which of
  the offsetof value or the array-name `.dest=member` item the local-aggregate (compound-
  literal-desugar) path chokes on.
- **parse** (line ~2337) — a file-scope `static const size_t X = PAD1_x >= 0x100 ? RULE_x :
  …;` very long `?:` ternary chain initializer. Check the const-expr/ternary fold path and
  any initializer-length cap.
- **bc, binary, gc, stream** — true site is past the coarse `^[};]` boundary (the bisect
  bracketed a big region whose head lines parse fine in isolation: bc's multi-name local
  decl, binary/stream's `__attribute__((noreturn))` prototypes with `mp_rom_error_text_t`,
  gc's `for(;;){ T *area = &(mp_state_ctx.mem.area); … }` all parse standalone). Re-bisect at
  finer (per-`;`, not just column-0) granularity, then isolate.

### How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct.
Forward-bisect: for each column-0 `}`/`;` boundary N, test `head -n N file.pp.c | minic -m
medium`; the FIRST boundary whose prefix errors brackets the construct (between the previous
clean boundary and N). For tight brackets use every `;`, not just column-0 ones. Extract into
a standalone snippet (stub the few typedefs) and confirm under `minic -m medium`.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **109
  s/r, 0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. miniyacc is picky:
  no `/* … */` between a production head and its `:`, none trailing a rule's action, no
  comment-only action body; **comments inside an action body must avoid `/`, `[`, `]`
  characters** (burned again in §1i — a `static T arr[]` comment broke the yacc parse; keep
  action comments plain prose). Long RHS is fine (existing for-decl rules run ~17 symbols).
- Run `tools/test-dos.sh` (must stay **100/100**) and `make check` (SSA, "All is fine!").
  Add or extend a probe per runtime-bearing feature; the gate runs ~5 min in DOSBox — run it
  in the background and wait.
- Spike harness uses **`clang -E`**. Read the real message from
  `build/mp-spike/err/<file>.minic.err`, not the lagged summary.tsv line.
- DOSBox capture is occasionally flaky. If a `--model=large` probe diff fails once, re-run.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `&global` data items are
  near-only, so `mp_aggregate_probe` / `block_static_probe` are medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Out-of-order designated array init** — `{ [3]=…, [0]=… }` dies (agg_emit_array is in-order).
- **char-array string init leaves a duplicate `$gloN`** — the lexer's literal slot is still
  emitted alongside the named array (harmless dead bytes; only short MP strings affected).
