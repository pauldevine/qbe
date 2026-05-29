# Next session — MicroPython port: struct return-by-value (codegen/ABI)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Spike now at 77/132 OK** (was 69 at the start of the last session). Re-run
> `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` to confirm before peeling more.
> Gate **80/80**.

## What changed last session (so you don't redo it)

§1b — **file-scope aggregate / designated initializers** — is **done** (spike 69 → 77).
`T NAME = { ... };` for struct/union/global variables now parses and emits a correct QBE
`data` block: designated `.field =`, nested braces (`.base = { ... }`), partially-initialized
array members (trailing zero-fill), `&global`/function/string-literal pointer fields, casts,
and constant-folded values. New `gaggr`/`gilist`/`gitem`/`gival` grammar (0 new conflicts) +
`cival_eval` constant folder + `emit_global_aggregate` layout-aware emitter, all in
`minic/minic.y`. Pinned by `aggregate_init_probe.c` (medium, DOSBox-verified). Out-of-order
designators and bitfield-member initializers `die()` (not used by MicroPython).

## Scope for next session — struct return-by-value (the dominant remaining blocker, ~20 files)

This is the `return source_line;` failure cluster (minic's error line is the parser's
*lookahead*, which lags the real construct by many lines — the true site is a struct-returning
function). `py/bc.h`:
```c
mp_code_lineinfo_t mp_bytecode_decode_lineinfo(const byte **ip);   // returns a struct BY VALUE
...
mp_code_lineinfo_t decoded = mp_bytecode_decode_lineinfo(&ip);     // struct-typed init from call
```
minic emits a word-typed return and chokes on the struct-typed initializer ("invalid lvalue"
/ parse error). This is a **codegen/ABI feature, not grammar**:

- **Return path:** a function whose return type is a struct/union. Two standard lowerings:
  (a) **hidden-pointer return arg** — caller allocates space, passes its address as an implicit
  first arg, callee `memcpy`s the result there (works for any size); or (b) **small-struct in
  registers** (≤4 bytes in DX:AX) like the Kl return path. Mirror the call/selret ABI work
  already done for Kl returns (see `[[i8086-selret-kl-rtmp-lowdup]]`, `[[kl-slot-resident-invariant]]`,
  and `minic.y`'s `selret`/`selcall`). The hidden-pointer approach is the safer general fix;
  consider it first.
- **Call path:** `selcall` must allocate the return temp, pass its address, and yield the
  struct value (really its address) so `struct X y = f();` becomes a struct copy from the
  return slot (reuse the existing `*X = *Y` struct-copy machinery in `minic.y`).
- **Far-data models:** the hidden return pointer is a far pointer under compact/large/huge
  (4-byte), near (2-byte) under medium — gate it like the other `uses_far_code()`/`NEAR_DATA()`
  sites. The probe should cross-check at least medium + one far-data model.
- **Probe:** a function returning a multi-field struct by value; caller does both
  `S y = f();` and `f().field` (member-of-call-result) and passes the result to another fn.
  Verify each field round-trips in DOSBox.

After it lands, re-run the spike and re-scope from the fresh tally.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts (now **108 s/r,
  0 r/r**). Justify any new shift/reduce; **no new reduce/reduce**. Local miniyacc is picky:
  **no `/* … */` between a production head and its `:`, none trailing a rule's action, and no
  comment block immediately preceding a production head** (burned last session — "colon expected
  after production's head"); per-rule RHS length limit (~5 symbols).
- Run `tools/test-dos.sh` (must stay **80/80**). Add/extend a probe per runtime-bearing feature.
- Spike harness uses **`clang -E`** (not `cpp`) and reports CPP_FAIL honestly. **minic's error
  line is the parser's *lookahead* line, which lags the real construct by many lines.**
  Binary-search-by-`head` is **unreliable** (non-monotonic; truncation gives spurious failures).
  Bisect only at top-level statement boundaries, and confirm any suspected construct by feeding a
  **minimal isolated snippet** to `minic -m medium` directly. The `NORMALIZE` sed is a no-op
  under macOS BSD sed (`\b`) — minic must accept canonical names.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)

- **Scalar array initializers** (`static const uint8_t rule_act_table[] = { ... };`) — the
  `T NAME[] = {...}` rule only dispatches struct / pointer element types; a scalar element type
  `die`s ("array initializer requires struct or pointer type"). Surfaced as a spike blocker
  (`compile.c`, the grammar/op tables). Small extension to `emit_struct_array_data`'s sibling.
- **Inline tagged-aggregate definition + var + initializer** (`union ival { int i; long l; } U = {…};`)
  parse-errors — define the type first, then declare the variable (the probe does this).
- `sizeof` of a *local* array returns pointer size; `void *` pointer comparison hits "void has
  no size"; `sizeof` only takes `( type | ident )`, not a general expression; tagged nested
  aggregate definitions used as members (`struct Foo { … } x;`) are not handled (only untagged);
  out-of-order / bitfield-member designated initializers `die()` (see §1b notes).
