# Next session — MicroPython port: next grammar blockers (post §1g)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 100/132 OK** (was 97 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **87/87**.

## What changed last session (§1g — so you don't redo it)

Five independent minic frontend wins (spike 97 → 100, three files flipped: objfun,
objboundmeth, + one obj* via sizeof). All in `minic/minic.y`; no i8086/QBE backend changes.

1. **Bitfield static initializers** — `agg_emit_struct` packs a run of bitfield members
   sharing one storage unit (same `m->offset`) into a single scalar data item. Handles
   both sequential and `.field =` designated items. Was `die("bitfield initializer
   unsupported")`. (mp_obj_exception_t / mp_map_t static instances.)
2. **`alloca` as a builtin** — undeclared-call path special-cases `alloca` /
   `__builtin_alloca` to return `void *` (was defaulting to `int`, breaking `T *p =
   alloca(...)`). NB: **parse/typecheck only** — minic emits `call $alloca`; there is no
   runtime alloca implementation yet. A real far/near DOS build will need one.
3. **Widening assignment `long <- char`** — the assignment converter extends an `INT` *or*
   `CHR` RHS to `LNG` via `sext` (was only `INT`). `uint32_t = uint8_t` now type-checks.
4. **`sizeof(T[expr])`** — new grammar rule folds to `SIZE(T)*dim`; when `dim` isn't a
   foldable constant (MP_STATIC_ASSERT's address-comparison dimension, voided anyway) it
   uses a dummy of 1. New non-dying `constfoldable(Node*)` predicate decides foldability.
5. **Static address-of in aggregate initializers** — `cival_lval` resolves `&global`,
   `&agg.member` (member-offset chains), and `&arr[i]` to `$sym+off`; the `sai_*`
   struct-array/pointer-array machinery grew an `'A'` (address-symbol) item kind +
   `sai_emit_sym`. Unblocks `mp_rom_map_elem_t table[] = { { key, &glob }, … }`.

Pinned by `minic/dos/examples/mp_aggregate_probe.c` (medium; golden in
`minic/dos/tests/`). 0 new conflicts (still **108 s/r, 0 r/r**). `make check` green.

**Reverted mid-session (do not retry as-is):** relaxing the `varadd` double-definition
check to allow *different-typed* re-declaration of a local across sibling blocks. minic has
no inner-block scoping, so updating `varh[].ctyp` to the latest decl miscompiles the
earlier block's uses of the name (turns a loud error into a silent miscompile). The
same-typed relaxation stays. The 3 `double definition` files genuinely need real
inner-block scope (see below).

**Known orthogonal gap surfaced (do not chase unless a far MP build needs it):** static
address-of items emit the canonical `data $P = { l $target, … }`. Under **near-data
(medium)** the linker relocates `$sym` fine. Under **far-data (large/huge)** a 4-byte
`$sym` data item must become a far seg:off pointer, and the OMF toolchain
(`asm_to_omf.py` / `omf_link.py`) does not yet emit that segment relocation for static
data symbols — so static-pointer values are wrong under large/huge. The minic output is
correct; this is a backend/linker enhancement. `mp_aggregate_probe` is therefore
medium-only.

## Scope for next session — remaining 32 failures

Run the spike, then per-file actual error MESSAGES (read `build/mp-spike/err/<file>.minic.err`,
NOT the lagged source line in `summary.tsv`):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  msg=$(grep -m1 'error:' build/mp-spike/err/$base.minic.err | sed -E 's/.*error:[0-9]*:?//')
  printf "%-16s %s\n" "$base" "$msg"
done | sort -k2
```

Current clusters: **27 parse error**, 3 double definition, 1 void-has-no-size (`malloc`),
1 unknown-struct-type (`modsys`).

### The 3 `double definition` files (compile, sequence, vm) — need inner-block scope
MicroPython reuses a local name across **sibling blocks with different types**, e.g.
`{ const byte *t = …; … } { size_t t = …; … }`, and across distinct `for (size_t i …)`
inits. minic has a single flat per-function var table (`varh`), so the two `t`s collide.
The §1e typedef-shadow work added `var_islocal()` + lexer `brace_depth`/`pending_varclr`
(locals dropped at the function body's closing `}`). Extend that to **push/pop a scope at
every `{`/`}`**: on block exit, drop (or restore the shadowed binding of) names declared in
that block. Then re-declaration in a new block is a fresh binding with its own type/slot,
and codegen stays correct because each decl already emits its own `%name = alloc`. This is
the highest-value remaining structural change (unblocks ≥3 files and likely several parse
errors that are really shadowing).

### Known deeper constructs behind the parse errors (each distinct; isolate a snippet)
- **Anonymous struct type inside a cast** — `binary`: `(size_t)&((struct { char c; short t;
  } *)0)->t` (inline `offsetof` with an unnamed struct). minic can't parse a struct
  definition inside a cast.
- **Designated array initializers** — `scope`: `static const uint8_t t[] = { [SCOPE_MODULE]
  = …, };`. The `[index] = value` form; extend the `sai_*` machinery to place by index.
- **switch label edge cases** — `objtuple` `case 1: default: {`, `objlist`
  `case MP_BINARY_OP_ADD: {`.
- **`} else {` / `(void)kind;` reports** (`obj`, `objexcept`, `objint`) — lookahead-lagged;
  bisect to the true site first.
- **`malloc` void-has-no-size** and **`modsys` unknown-struct-type** — isolate; likely a
  `sizeof`/`void *` or an undefined struct tag used before definition.

### How to find the true site (the lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct by
many lines. Test prefixes (`head -n N file.pp.c | minic -m medium`) that end at a
**top-level (column-0) `}` or `;`**; a prefix is a real failure only when its reported error
line is well *before* the truncation point. Walk those boundaries upward; the first whose
reported error stays fixed and small is just past the true construct. Extract it into a
standalone snippet (stub the few typedefs) and confirm it reproduces under `minic -m medium`.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108
  s/r, 0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is
  picky: no `/* … */` between a production head and its `:`, none trailing a rule's action,
  no comment-only action body, no comment block immediately preceding a production head;
  per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **87/87**) and `make check` (SSA, "All is fine!"). Add
  or extend a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`). Read the real message from
  `build/mp-spike/err/<file>.minic.err`, not the lagged source line in `summary.tsv`.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

- **Far-data static pointer relocation** (`l $sym` → far seg:off) — see §1g note above.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors (the
  address-of work landed in the aggregate/struct-array path, not bare-scalar globals).
- **`long` struct member from an opaque source** — `[[qbe-loadc-wordsize-i8086]]`; MP
  structs are word/`size_t`-sized so it doesn't gate.
- **Inline `100000L` literal** — lexer drops the `L` bit; build large longs from
  small-literal arithmetic or stage through a long local.
- **`sizeof(expr)`** (general expression, e.g. `sizeof((arr)[0])`) — `sizeof` takes
  `( type | type[expr] | ident )` only.
- **Inline tagged-aggregate definition + var + initializer** parse-errors — define the type
  first.
