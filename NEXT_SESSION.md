# Next session — MicroPython port: next grammar blockers (post §1f)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 97/132 OK** (was 87 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then
> `cut -f2 build/mp-spike/summary.tsv | sort | uniq -c` to confirm before peeling more.
> Gate **86/86**.

## What changed last session (so you don't redo it)

§1f — five independent wins, all landed (spike 87 → 97). See the MICROPYTHON_PORT.md running log
for the full detail. Summary:

1. **Scalar array initializers** — `emit_scalar_array_data` in `minic/minic.y`; `T NAME[] = {…}`
   with a scalar element type now emits a real data block.
2. **Const-expression brace items** — `sai_item: expr` + `sai_add_expr` fold each item via
   `const_eval` (string literals still route to `sai_add_str`).
3. **`offsetof`** — added to `minic/include/stddef.h` (`((size_t)&((type*)0)->member)`).
4. **`__attribute__` struct-member prefix** (`smembers: smembers attrspec`) + **member cap 64→256**
   (`members[NMember]` + three `>= NMember` checks). Flipped 5 of the 6 `mp_fun_table` files.
5. **`const_eval` operators** — added `'K'` cast, `!` `==` `!=` `<` `<=` `&&` `||`, and `?:` ternary.

Pinned by `scalar_array_probe.c` (medium + large). 0 new conflicts (still 108 s/r, 0 r/r).

## Scope for next session — remaining 35 failures

Run the spike, then per-file actual error MESSAGES (not the lagged source line — read
`build/mp-spike/err/<file>.minic.err`):
```sh
for base in $(awk -F'\t' '$2=="MINIC_FAIL"{print $1}' build/mp-spike/summary.tsv); do
  msg=$(grep -m1 'error:' build/mp-spike/err/$base.minic.err | sed -E 's/.*error:[0-9]*:?//')
  printf "%-16s %s\n" "$base" "$msg"
done | sort -k2
```

Current clusters (after §1f): **22 parse error**, 3 double definition, 3 bitfield-initializer-unsupported,
3 invalid-assignment, 1 void-has-no-size, 1 unsupported-operation-in-constant-expression (now
`objmodule`, needs static address-of), 1 unsupported-address-of-in-static-initializer, 1
unknown-struct-type.

### Known deeper constructs behind the parse errors (each distinct; confirm by isolating a snippet)
- **`sizeof(char[expr])` MP_STATIC_ASSERT idiom** — `((void)sizeof(char[1 - 2 * !(&a != &b)]))`,
  appears across many `obj*` files (`objlist`, `objtuple`, …) inside `mp_obj_is_type` checks. The
  dimension contains an address comparison so it can't be const-folded; but the whole `sizeof(...)`
  is always cast to `(void)` and discarded. Pragmatic option: parse `sizeof(type[expr])` and, when the
  dimension isn't const-foldable, return a dummy (the value is voided here). Risky in general — gate
  it carefully. Likely the single biggest remaining cluster.
- **Anonymous struct type inside a cast** — `binary`: `(size_t)&((struct { char c; short t; } *)0)->t`
  (an inline `offsetof` with an unnamed struct). minic can't parse a struct definition inside a cast.
- **Designated array initializers** — `scope`: `static const uint8_t t[] = { [SCOPE_MODULE] = …, };`.
  The `[index] = value` form. Bounded feature; extend the `sai_*` machinery to place by index.
- **Static address-of pointer values** — `objmodule`: `mp_rom_map_elem_t table[] = { { key, &glob }, … }`
  (`'A'` node in `const_eval`). Needs relocatable `&global` in a struct-array data block.
- **switch label edge cases** — `objtuple` `case 1: default: {`, `objlist` `case MP_BINARY_OP_ADD: {`
  (the body starts with the `sizeof(char[…])` idiom — overlaps the first bullet). Bisect to separate.
- **`} else {` / `(void)kind;` reports** (`obj`, `objexcept`, `objint`) — lookahead-lagged; bisect.

### How to find the true site (the lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real construct by many
lines. Only test prefixes that end at a **top-level (column-0) `}` or `;`** boundary; a prefix is a
real failure only when its reported error line is well *before* the truncation point. Walk those
boundaries upward; the first one whose reported error line stays fixed and small is just past the
true construct. Extract it into a standalone snippet (stub the few typedefs it references) and confirm
it reproduces under `minic -m medium` directly.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is picky:
  **no `/* … */` between a production head and its `:`, none trailing a rule's action, no
  comment-only action body, and no comment block immediately preceding a production head**;
  per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **86/86**) and `make check` (SSA, "All is fine!"). Add/extend
  a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`). Read the real message from
  `build/mp-spike/err/<file>.minic.err`, not the lagged source line in `summary.tsv`.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

- **`long` struct member from an opaque source** — `[[qbe-loadc-wordsize-i8086]]`; MicroPython
  structs are word/`size_t`-sized so it doesn't gate.
- **Inline `100000L` literal** — lexer drops the `L` bit (`[[minic-long-literal-int-vararg]]`); build
  large longs from small-literal arithmetic or stage through a long local.
- **`sizeof(expr)`** (general expression, e.g. `sizeof((arr)[0])`) — `sizeof` only takes
  `( type | ident )`. Blocks `map`. `void *` pointer comparison hits "void has no size".
- **Inline tagged-aggregate definition + var + initializer** parse-errors — define the type first.
