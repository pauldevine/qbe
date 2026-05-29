# Next session — MicroPython port: stand up a working preprocessing environment (Phase 2/3 config)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **The frontend declaration grammar is cleared as far as the spike can measure.** Nested
> named members, flexible-array decay, and void-returning function pointers all landed
> 2026-05-29 (gate **76/76**). This document is the re-scoped plan for the *next* layer.

## The pivot: the spike is now cpp-bound, not grammar-bound

Re-running the spike after the 2026-05-29 grammar work, the headline number barely moved
(16 → 23/132 OK) — but the *reason* changed completely. **All 132 `py/*.c` files now fail
`cpp` itself** (exit 1), on shared-config errors in two headers:

- `py/mpconfig.h`: `invalid token at start of a preprocessor expression`, `Unexpected
  MP_INT_MAX value`, `Unexpected SIZE_MAX value`, `endianness not defined and cannot detect it`.
- `py/misc.h`: `__has_builtin` unsupported by host `cpp` in this mode, `#elif after #else`.

The spike harness pipeline is `cpp … | tr … | sed … > pp`, so the pipeline's exit status is
**`sed`'s (0), not `cpp`'s (1)** — cpp failures are silently mislabeled `MINIC_FAIL "parse
error"` (94 of them), because minic then chokes on the raw `#if`/`//` lines cpp left behind
when it bailed. The "23 OK" are merely files whose *garbled* cpp output stayed parseable.

**Conclusion:** there is no point doing more minic grammar work (including the §1b
aggregate/designated-initializer emitter) until a clean translation unit can actually be
preprocessed. **Build the config/header layer first**, then re-measure.

## Scope for next session

### Step 0 — fix the harness exit-code masking (5 min, do this first)

In `build/mp-spike/run-spike.sh`, the `if ! cpp … | tr … | sed … > "$pp"` line hides cpp
failures. Use `set -o pipefail` (bash) for that pipeline, or capture cpp to a temp file and
check its status, so CPP_FAIL is reported honestly. Without this you cannot tell config
progress from grammar progress.

### Step 1 — a real `mpconfigport.h` + stub headers so cpp succeeds

This is Phase 2 (headers) + Phase 3 (port glue) bleeding together — the config header is what
makes the limits/endianness/`__has_builtin` errors go away. Concretely, the cpp errors trace to:

- **endianness** (`mpconfig.h:2320`): define `MP_ENDIANNESS_LITTLE`/`MP_ENDIANNESS_BIG`
  (8086 is little-endian) so the auto-detect `#error` path isn't taken.
- **`MP_INT_MAX` / `SIZE_MAX`** (`mpconfig.h:225,240`): the spike's stub `limits.h`/`stdint.h`
  must define `INT_MAX`/`UINT_MAX`/`LONG_MAX`/`SIZE_MAX` with the *target's* widths
  (`int`=16-bit, `long`=32-bit on this 8086 target — NOT the host's). Note the shipped
  `minic/include/stdint.h` still lacks `intptr_t`/`uintptr_t` (model-dependent width — wants a
  `FAR_DATA` `#ifdef`); the spike's `stubinc/stdint.h` shadow papered over this.
- **`__has_builtin`** (`misc.h:469`): host `cpp -nostdinc` may not provide it; define
  `__has_builtin(x) 0` (and check `__has_feature`/`__has_attribute` similarly) in the config
  header or a forced-include, OR pass it on the cpp line.
- **`#elif after #else`** (`misc.h:457`): usually a *downstream* symptom of an earlier `#if`
  whose macro was undefined — re-check once the above are defined; it may evaporate.

The cleanest home for this is the actual port: start `ports/dos8086/mpconfigport.h` (the
locked target config is in `MICROPYTHON_PORT.md` §"Target configuration") and point the spike
at it (`-I…/ports/dos8086`) instead of the ad-hoc `build/mp-spike/stubinc/` shims. Re-run the
spike after each header lands and watch CPP_FAIL → 0.

### Step 2 — re-measure minic grammar; THEN re-scope §1b

Once files preprocess cleanly, the spike's MINIC_FAIL tally will *finally* reflect real
grammar gaps. Two are already visible through the noise:

- **`NVar` "too many variables"** (15 files, Phase 1c): raise minic's local-variable cap.
- **§1b aggregate/designated initializers** (`{ … }`, `.field =`, `[i] =`) — the approved
  plan's `dataitem()` emitter. This is still expected to be the dominant *grammar* blocker,
  but **re-scope it from the post-config tally**, not from today's cpp-polluted one.

## Guardrails (unchanged)

- Rebuild with `cd minic && make minic`; the local `yacc` prints conflict counts (currently
  **126 s/r, 0 r/r** — no new reduce/reduce, justify any new shift/reduce). The local miniyacc
  is picky: **no `/* … */` comment between a production's head and its `:`, and none trailing a
  rule's action**; per-rule RHS length limit (~5 symbols, like the existing 5-symbol rules).
- Run `tools/test-dos.sh` (must stay **76/76**). Add/extend a probe per runtime-bearing feature.
- Spike: `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c`, then read `summary.tsv` /
  `err/<file>.minic.err`. **minic's error line points just *past* the failing construct.** The
  harness `NORMALIZE` sed is a **no-op under macOS BSD sed** (`\b` unsupported) — pp files carry
  canonical un-normalized type names, so minic must accept them natively.
- Orthogonal pre-existing limits (don't chase unless a real consumer needs them): `void *`
  pointer comparison hits "void has no size"; `sizeof` only takes `( type | ident )`, not a
  general expression; tagged nested aggregate definitions used as members (`struct Foo { … } x;`)
  are not handled (only untagged `struct { … } x;`); only the struct-member array-dim sites take
  const-exprs.
