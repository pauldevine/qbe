# Next session (§4d — TRY THIS: reduce the pre-existing churn(~80) GC-pressure corruption — the real compiler-bug candidate)

## 2026-06-08 §4c notes (§4b DONE: stackless-strict is the dos8086 port default — clean win, no compiler bug)
- **§4b landed.**  `MICROPY_STACKLESS (1)` + `MICROPY_STACKLESS_STRICT (1)` are
  now the dos8086 port default (external `~/projects/micropython/ports/dos8086/
  mpconfigport.h`), with `mp_raise_recursion_depth()` provided as a real port
  symbol in `ports/dos8086/main.c` (py/runtime.c only defines it under
  MICROPY_STACK_CHECK, which we keep off).  The qbe-repo artifact is the harness
  default **`MP_STACK_SIZE` 24576 → 16384** in `tools/build-micropython.sh`
  (committed).  Build: 107/107 TUs, **image 843344 / body 820096** (under the
  ~824416 "Program too big" point; loads with margin).  `make check` green;
  `tools/test-dos.sh` was **219/219 ok** at session start and is unchanged (NO
  minic/qbe/i8086/runtime/probe source changed this session — the only qbe edit
  is the shell-script stack default + this doc).
- **Why 16384, not the plan's 8192:** 8192 corrupts.  Deep PLAIN recursion is
  now heap-framed (stackless), so the C stack stays shallow — but generator
  RESUME still C-recurses (`mp_execute_bytecode`, objgenerator.c:210; STACKLESS
  does NOT cover generator resume), so deep generator nesting overflows the C
  stack into DGROUP data.  At 8192 that corruption is catastrophic (garbage
  output + `Divide overflow` INT 0); 16384 degrades it gracefully (wrong value,
  clean exit).  16384 is the largest stack that still fits the load ceiling
  (body 820096 < ~824416; 24576 → body 828224 → won't load).
- **On-Victor verification (real Victor via `tools/run-victor-sasi.sh`):**
  - `build/mp-recsum-probe.py` → `recsum(6/12/20/30)` = 21/78/210/465, clean
    `D4`/`C5`.  **The documented HARD frontier (recursive image corrupted at
    recsum(20) with `DE`+`(nil)`) is GONE.**
  - `build/mp-frontier2.py` → reaches **`OK recsum`** (the old recsum(30) wall),
    then hits the pre-existing churn(80) frontier (see §4d below).
  - `build/mp-feature-probe.py` → ALL 23 checks OK (mul…enum), clean `D4`/`C5`.
- **Stackless is strictly ≥ the committed recursive image on every axis** (all
  measured on real Victor this session):
  | workload | recursive 24 KB (was committed) | stackless 16 KB (now) |
  |---|---|---|
  | deep plain recursion recsum(30) | ✗ corrupt (`DE`+`(nil)` @ 20) | ✓ clean 465 |
  | deep generator recursion `sum(gc(15))` | ✗ **machine REBOOT** | ~ wrong 99, clean exit |
  | GC pressure churn(80) | ✗ corrupt (NameError) | ✗ corrupt (hang) — TIE, pre-existing |
- **No compiler bug surfaced** — the §4b "stress for a codegen bug" prize did NOT
  materialise (the honest-caveat outcome).  The deep-generator-recursion limit is
  target-fundamental (finite DOS C stack vs. generator C-recursion, no fit-able
  MICROPY_STACK_CHECK), not a minic/qbe/i8086 bug and not a stackless regression.
- **Probes written this session (untracked `build/*.py` scratch):**
  `mp-stackless-stress.py` (mutual/raise-catch/generator/GC recursion),
  `mp-gen-probe.py` (generator-recursion bisection), `mp-churn-scale2.py`
  (churn 20→120 GC-pressure scale).

## §4d — THE GOAL FOR NEXT SESSION: reduce the churn(~80) GC-pressure corruption
**`churn(n)` corrupts between n=60 (ok) and n=80 (fails) on BOTH the stackless
and the recursive images** — a pre-existing, VM-mode-independent bug, and the
most promising remaining compiler/runtime-bug candidate.  `churn` is a FLAT loop
(no recursion), so it is NOT a C-stack issue — it is **GC pressure**: each
iteration allocates `[i+j for j in range(8)]` (a list + a comprehension
frame) + `{str(i):row,"last":row[-1]}` (a dict + a str).  At ~churn(80) a live
object is lost: symptoms are nondeterministic (`NameError: local variable
referenced before assignment` in `<listcomp>`, `TypeError: object isn't
subscriptable` on `table["last"]`, or a hang), all consistent with a GC
root-scan miss or heap corruption under pressure.
- **Repro:** `VICTOR_SRC=build/mp-churn-scale2.py tools/run-victor-sasi.sh
  build/mp-link/mpython.exe 240` → prints `20 330`, `40 1060`, `60 2190`, then
  fails at 80.
- **Likely loci** (reduce to a `minic/dos/examples/*_probe.c` FIRST, per the
  discipline): the conservative C-stack root scan in `ports/dos8086/main.c`
  `gc_collect()` (does it miss a live far pointer at some alignment under deep
  allocation?), gc.c block/ATB math under near-full heap, or a codegen bug in
  the list-comprehension / dict-store path that only bites once the heap is
  churned.  Instrument MicroPython gc at the C level (mark/sweep of the listcomp
  frame + the per-iteration dict) for `churn(80)` to find which object is freed
  while live, then reduce that shape to a DOS probe and fix QBE/minic/runtime.
- **HARNESS GOTCHA (cost me a wasted run this session — see
  [[feedback-victor-harness-pipe-buffer]]):** do NOT pipe `run-victor-sasi.sh`
  through `tail`/`head`.  Its watchdog subshell `( sleep WALL_SECS; kill )&`
  inherits the pipe write-fd (~1080 s for a 240 s run), so `tail` blocks for an
  EOF that never comes and the run looks empty/hung.  Redirect straight to a
  file (`... > /tmp/run.out 2>&1`), background it, and poll the file.  macOS has
  **no `setsid`**.

# (ARCHIVED) §4b plan — land stackless-strict as the port default

## THE GOAL FOR NEXT SESSION
**Enable `MICROPY_STACKLESS=1` + `MICROPY_STACKLESS_STRICT=1` as the dos8086 port
default, rebuild compact far-data, and re-verify on Victor — then stress the
recursion paths to see if the different VM code paths shake a compiler/backend
bug loose.**  This is the most concrete remaining frontier with a known payoff:
it eliminates the one documented HARD frontier (deep Python recursion) while
staying under the Victor image ceiling.

### Why this is the pick
- The committed port today uses MicroPython's **recursive** VM: each Python call
  is a C-level recursive call into `mp_execute_bytecode`, so deep recursion
  burns the hard-capped DOS stack.  `build/mp-recsum-probe.py` reaches
  `recsum(12)` but fails by `recsum(20)` with an **uncaught** `DE` + `(nil)`
  (corruption, not a clean exception).  `build/mp-frontier2.py` dies at the
  `recsum(30)` case.
- You **cannot fix it by growing the stack**: 28 KiB → body 824416 → Victor
  "Program too big to fit in memory"; 32 KiB → won't link (DGROUP+stack > 64 KB).
  And `MICROPY_STACK_CHECK=1` is both too big AND consumes more transient C stack
  per frame, so it trips during *shallow* recursion (see the 2026-06-07 Codex
  stack-check experiment notes below).  Both are dead ends.
- **Stackless-strict is already proven to work via build knobs** (2026-06-07
  Codex notes, lines ~441-445): with `MP_STACK_SIZE=8192` it links at total
  **835088 (well under ceiling)** and `build/mp-recsum-probe.py` completes
  `recsum(6/12/20/30)` with clean `D4`/`C5`; `mp-test.py`/`mp-feature-probe.py`/
  `mp-frontier.py` all still pass; `mp-frontier2.py` reaches `OK recsum`.  The
  deep-recursion frontier disappears.

### Concrete steps (promote experiment → committed port default)
1. **External MicroPython checkout** (`~/projects/micropython`, NOT this repo):
   - `ports/dos8086/mpconfigport.h`: add `#define MICROPY_STACKLESS (1)` and
     `#define MICROPY_STACKLESS_STRICT (1)` (both default to `0` in
     `py/mpconfig.h:386,393`; the port does not currently override them).
   - Provide `mp_raise_recursion_depth` **properly** as a real port source symbol
     (e.g. in `ports/dos8086/main.c` or a small port .c), NOT via the generated
     `runtime.pp.c` sed-patch.  The existing build knob
     `MP_DOS_STACKLESS_RECURSION_RAISE=1` (tools/build-micropython.sh:67,136)
     proves the one-liner body; just make it a committed symbol so the build is
     reproducible without the env knob.
   - Set the DOS port stack to the value that fit: `MP_STACK_SIZE=8192` worked
     (vs the current 24576 default).  Decide whether to bake 8192 into the
     harness default or keep it an env override — but RECORD the chosen value.
2. **Build:** `tools/build-micropython.sh --model=compact` (with the stackless
   config above).  Confirm 106/107 TUs → objects and a clean link.  **MEASURE
   the image** and compare to the 844256 NONE baseline / the 835088 stackless
   experiment number.
3. **Re-verify on Victor** (`VICTOR_SRC=... tools/run-victor-sasi.sh
   build/mp-link/mpython.exe 240`), in this order:
   - `build/mp-recsum-probe.py` — must reach `recsum(30)` + clean `D4`/`C5`
     (the whole point).
   - `build/mp-test.py`, `build/mp-feature-probe.py`, `build/mp-frontier.py`,
     `build/mp-frontier2.py`, `build/mp-frontier3.py` (NEW this session — see
     §4a-followup below) — full feature surface must still pass.
4. **Stress the new VM paths for a compiler bug** (the REAL prize): stackless
   uses heap frame-chaining + a different nlr/exception interaction.  Push
   deep+wide recursion, mutual recursion, recursion-through-generators,
   recursion-raising-and-catching-exceptions, and recursion under GC pressure.
   If anything mis-behaves, **reduce it to a `minic/dos/examples/*_probe.c`
   FIRST**, fix the QBE/minic/i8086/runtime bug, then gate it in
   `tools/test-dos.sh` — same discipline as every prior §.

### The honest caveat (decide if it's worth it)
This is a **MicroPython port-config improvement, NOT inherently a compiler
change.**  It makes the *port* more capable (clean deep recursion within the
ceiling).  Its value as a *compiler exercise* is indirect: the different VM code
paths MIGHT flush out a latent minic/codegen bug (that reduction would be the
real win), or it might just work — in which case you've improved the port, not
the compiler.  If the session goal is strictly "find compiler bugs," a fresh
untested feature surface may be a better net than port tuning.  But stackless is
the one frontier with a mapped path AND a known payoff, so it's the default pick
unless the user redirects.

## §4a-followup (2026-06-08): frontier3 sweep — CLEAN, no compiler bug
- Re-verified baseline gates green BEFORE any work: `make check` ✅,
  `tools/test-dos.sh` **219/219 ok**.  No tracked changes made this session.
- Wrote `build/mp-frontier3.py` (untracked scratch, alongside the other
  `build/mp-*.py`) to push past the fixed `str(int)` frontier on real Victor.
  **Every minimal-ROM-supported feature passed**, including the codegen-sensitive
  cross-word 32-bit (DX:AX) integer arithmetic that's most likely to expose an
  i8086 bug:
  - int: `100000*5`, `1<<20`, `divmod(100000,7)`, `-7//2`, `-7%2`, big XOR,
    `~0`, `1000000>>3`, `7**6` — all correct.
  - dict: `update`/`get`/`get(default)`/`keys`/`values`.
  - list: `insert`/`extend`/`pop`/`index`.
  - `zip`, `map`, `sorted(key=lambda)`, nested `repr` (list-of-dicts-of-lists),
    `str.format` (positional + reordered), and a 200-iteration GC churn loop
    (list+dict+str per pass, some retained as live roots) → correct checksum,
    clean collection.  `DONE` → `D4` → `C5`.
- The ONLY "failures" were `filter` / `reversed` raising `NameError` — these are
  **deliberate config omissions** (`MICROPY_PY_BUILTINS_FILTER`/`_REVERSED`
  require `AT_LEAST_CORE_FEATURES`; the port is `MINIMUM` ROM level,
  `py/mpconfig.h:1531,1536`).  Same category as the documented `str.count` /
  `%`-format gaps — NOT a compiler bug.
- Net: the port's integer-feature surface is robust wherever the minimal config
  enables it.  This frontier found nothing to fix — hence §4b redirects to the
  stackless-strict recursion direction, which has a mapped path and a real
  payoff.

# Next session (§4a — float flip: all per-TU gaps cleared; FLOAT LINKS but overflows Victor ceiling)

## 2026-06-08 §4a notes (MICROPY_FLOAT_IMPL_FLOAT now LINKS; size wall is the blocker)
- **Goal: clear the 4 remaining per-TU gaps from §3z and actually flip
  `MICROPY_FLOAT_IMPL` → FLOAT.**  All 4 gaps cleared; the flip now produces a
  **clean link (107/107 TUs, compact far-data)**.  BUT the float image is too
  big for the Victor load ceiling, so the flip is REVERTED to NONE (the §3z
  discipline).  `make check` green; `tools/test-dos.sh` **219/219 ok**; the
  NONE image is **844256 — byte-identical** to §3z (all groundwork is
  gc-stripped under NONE).
- **The 4 gaps were NOT 4 distinct compiler bugs — they collapsed to 3 root
  causes, only ONE of which touched compiler-adjacent code:**
  1. **objfloat / objtype / modbuiltins → ONE qstr gap.**  All three referenced
     `MP_QSTR_float` / `MP_QSTR___float__`, absent from the pre-generated
     `ports/minimal/build/genhdr/qstrdefs.generated.h` (built integer-only).
     modbuiltins's "non-constant in case label" was a LAGGED line number —
     instrumenting `const_eval`'s die printed the real culprit `MP_QSTR_float`
     (an undefined identifier in the `mp_module_builtins_globals_table` rom-map
     entry, NOT a real `case`).  **Fix = append two QDEF0 (static-pool) lines**
     to the genhdr (hashes via the verified djb2 `hash*33^b & 0xFFFF` — matched
     known entries __dir__=36730/__call__=63911):
       `QDEF0(MP_QSTR_float,    17461, 5, "float")`
       `QDEF0(MP_QSTR___float__, 28725, 9, "__float__")`
     Safe because pool 0 is **unsorted** (linear search) and
     `MP_QSTRnumber_of_static` is **positional** (auto-counted) — both the enum
     (qstr.h) and the data arrays (qstr.c) scan QDEF0 in file order, so an
     appended line stays index-consistent.  Static pool now 185 (< 256, so
     bytecode short-qstr encoding is unaffected).
  2. **parsenum.c "undefined variable" → missing `INFINITY` macro (header
     gap, NOT the §3z-guessed float-local scope bug).**  `(mp_float_t)INFINITY`
     left `INFINITY` unexpanded — `minic/include/math.h` never defined it.
     **Fix:** new public `float sf_inff(void)` in `minic/dos/softfloat.c`
     (`sf_frombits(sf_inf(0))`) + `#define INFINITY/HUGE_VALF/HUGE_VAL
     (sf_inff())` and `#define NAN (sf_nan(""))` in math.h.  (Reduced the
     suspected scope shape first — it compiled clean — which pointed at the
     macro.)
  3. **binary.c "parse error" → `_Float16` (config decision, NOT a minic
     parse bug).**  host clang defines `__FLT16_MAX__`, so mpconfig.h
     auto-selected the native `_Float16` union path; minic/i8086 has no
     `_Float16` and MicroPython ships a portable `uint32_t`-bit fallback for
     exactly that.  **Fix = `#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)`** in
     the port config.
- **Probe:** extended `minic/dos/examples/softlibm_probe.c` (+golden, medium
  `--softfloat`) with the new `INFINITY`/`NAN`/`HUGE_VALF` macros
  (`inf_bits=7f800000`, `isinf(INFINITY)=1`, `signbit(-INFINITY)=1`,
  `huge_bits=7f800000`, `isnan(NAN)=1`).  This is the only compiler-surface
  artifact of the session (the qstr + FLT16 fixes are external/config).
- **THE WALL — why FLOAT is reverted:** the FLOAT compact far-data image is
  **908944 total / body 882944** (code 742882, far data 77904).  The Victor
  load ceiling is **footprint = body + heap + stack ≤ ~896 KB**; the prior
  data points: NONE body 821152 loads, a 28 KiB-stack body 824416 already
  reported "Program too big".  Float body 882944 is **~59 KB past a
  known-failing point** — and that 59 KB is intrinsic float CODE (objfloat +
  formatfloat + parsenum-float path + the exp2/log2/powf soft-libm + every
  `_sf_*` call the VM now emits).  Heap is BSS, so trimming it cannot shrink
  the body; `--gc-sections` already ran (stripped 201 segments, keeping only
  the reachable powf, not exp2f/log2f/expf/logf).  **Enabling float on Victor
  requires a code-size campaign first** (feature trim won't help — float IS the
  feature; the candidates are the §2-style i8086 backend size levers, or a
  larger-RAM target).  Did NOT run Victor — 882944 vs the recorded
  824416-fails point makes the result certain; no need to burn the long run.
- **To re-attempt the flip** (recorded in `ports/dos8086/mpconfigport.h`'s
  float comment too): flip the 3 mpconfigport defines (FLOAT + COMPLEX 0 +
  FLT16 0) and append the 2 QDEF0 genhdr lines above.  The minic/softfloat/
  math.h groundwork is all in-tree and inert under NONE.
- **DECISION (2026-06-08, user): do NOT pursue MICROPY_FLOAT_IMPL_FLOAT on
  Victor.**  The effort to recover ~60 KB of code is not worth the payback in
  this RAM-limited environment.  Soft-float stays a fully-gated MEDIUM-MODEL
  DOS capability (the `--softfloat` probes), and the float groundwork in tree
  (softfloat.c, math.h, double→single, static float init) is inert under
  MicroPython's `MICROPY_FLOAT_IMPL_NONE`.  The MicroPython port stays
  integer-only; the FLOAT path is *available* (recipe above) but not a target.
- **Next:** drive the MicroPython port on integer-feature frontiers again
  (slicing/strings/GC pressure/recursion) and reduce any new failure to a
  `minic/dos/examples/*_probe.c` before fixing, as always.  The float flip is
  closed as "won't-fit, not worth it".

# Next session (§3z — MicroPython float flip groundwork: double→single, static float init)

## 2026-06-08 §3z notes (toward MICROPY_FLOAT_IMPL_FLOAT: compiler gaps cleared; flip surfaces per-TU gaps)
- **Goal: flip `MICROPY_FLOAT_IMPL` → FLOAT** (the §3y next step).  §3y's
  soft-libm made the *math* LINK-complete; this session did the build wiring,
  flipped the flag, and cleared the COMPILER gaps the flip exposed.  The flip
  is NOT yet complete — it surfaces 5 further per-TU gaps (below), 2 of which
  are build-infra (qstr regen), not compiler bugs.  **Landed the compiler work
  as a green-gate milestone; the external `mpconfigport.h` flip was REVERTED to
  NONE to keep that checkout clean.**  `make check` green; `tools/test-dos.sh`
  **218→219 ok**.
- **Build wiring (verified inert under NONE):**
  - `tools/build-micropython.sh` always links `minic/dos/softfloat.c` (the
    `_sf_*` arithmetic + algebraic/transcendental libm).  Under
    `MICROPY_FLOAT_IMPL_NONE` `--gc-sections` strips it ENTIRELY → image
    **byte-identical** (844256, 0 `sf_` symbols in the map).
  - `build/mp-spike/stubinc/math.h` was an EMPTY stub that SHADOWED the real
    `minic/include/math.h` (stubinc is `-I`'d first); now it `#include`s the
    real header.  Inert under NONE.
- **`double` aliases to single-precision (Ks)** — the decision (FPU-less i8086,
  no 8087, no 64-bit int to build a soft-double; standard tiny-target
  convention).  `minic/minic.y`: `TDOUBLE` → `INT|FLOAT` (was `LNG|FLOAT`);
  every float literal — suffixed or not — types single; `irtyp`/`irtyp_ret`
  always return `'s'` for a float (backstop so no stray `Kd` reaches the
  backend).  The existing `exts`/`truncd` conversion sites are guarded on a
  float-precision *difference* which can no longer occur, so they go dead (no
  bogus conversion).  This unblocked **93 of 107 MP TUs** (obj.h's
  `mp_obj_get_float_to_d`/`_from_d` inline helpers, emitted into every TU, no
  longer carry a `Kd`).
- **Pre-existing `SIZE(float)`=2 bug FIXED** — the `SIZE` macro never checked
  `FLOAT`, so `float` (`INT|FLOAT`) sized as the 2-byte `int` (masked before
  because `double` was `LNG|FLOAT`→4).  Added `ISFLOAT(x) ? 4` early.  Without
  this, `sizeof(float)`==2 and float struct members overlapped (probe `pb` read
  the wrong 2 bytes).  float LOCALS were unaffected (backend Ks slots are 4B).
- **`Ostosi`/`Ostoui` with a `Kl` result** (`i8086/emit.c`) — float→`long`
  (the `mp_float_hash` `(mp_int_t)val` shape) hit the `i->cls == Kl` switch and
  died.  Excluded them from that switch so they reach the soft-float conversion
  handler, which now stores the full `_sf_to_int` DX:AX into the Kl slot (Kw
  result still takes the low word only).
- **Static float initializers** (`minic/minic.y`) — a file-scope `float g=1.5f;`
  or a const struct float member used to die "unsupported operation in constant
  expression" (integer-only `const_eval`).  New `const_eval_double()` (host
  double; handles literals/casts/`+-*/`/unary-minus, incl. the `0 - x` form
  `mkneg` emits for a negative float) + `cival_float_text()` (`%.17g`) +
  `emit_global_float_init()` + an `ISFLOAT` branch in `agg_emit_scalar`.
  Emits QBE `s s_<value>`.
- **Float DATA truncation FIXED** (`parse.c`) — QBE maps `s` (float) data →
  `DW`, which on i8086 (`wordsz==2`, where `int`/`Kw` is 2 bytes) emits the
  2-byte `int` width → a 4-byte float was truncated.  `case Ts:` now picks `DL`
  (the §ll `.long` = 4-byte directive) when `T.wordsz==2`.  Target-general
  (gated on word size), `make check` green.
- **Probe `minic/dos/examples/double_float_probe.c` (+golden), gated medium
  `--softfloat`** (`tools/test-dos.sh` **219/219 ok**): sizeof(double/float)==4,
  static float globals (incl. negative) + struct float members, double
  single-precision arithmetic, float↔double identity conversion, float→long
  (Ostosi Kl), float→int (Ostosi Kw), int→float (swtof).  Bug-loud: a `Kd`
  double would die() in the backend, a stale static-float init would die in
  minic, and a 2-byte float would mis-read.
- **REMAINING to actually enable `MICROPY_FLOAT_IMPL_FLOAT`** (after re-flipping
  `ports/dos8086/mpconfigport.h` to FLOAT and adding back
  `#define MICROPY_PY_BUILTINS_COMPLEX (0)` — complex defaults on with float,
  mpconfig.h:983, and is niche/costly here so keep it off):
  1. **qstr/genhdr regeneration (build infra, NOT a minic bug)** — objfloat.c
     and objtype.c reference NEW qstrs `MP_QSTR_float` / `MP_QSTR___float__`
     that are ABSENT from the pre-generated `ports/minimal/build/genhdr/
     qstrdefs.generated.h` (built for the integer-only config).  Regenerate the
     qstr/genhdr set with the float-enabled dos8086 config (MicroPython's
     `makeqstrdefs.py`/`makeqstrdata.py`).  This is how the build harness
     borrows genhdr from `ports/minimal/build`; it needs a float-config genhdr.
  2. **parsenum.c** — `dec_val` (a float local in the float-parsing path)
     reported "undefined variable".  Reduce to a minic probe (likely a
     float-local-in-a-conditional-block scope gap).
  3. **modbuiltins.c** — "non-constant in case label" (lookahead-lagged; find
     the real `case` — likely a float-related `round`/builtin switch).
  4. **binary.c** — "parse error" at `mp_decode_half_float`'s
     `union { uint16_t i; ... }` (float16 decode); reduce + fix the minic parse
     gap.
  Then build compact far-data `--keep-going`, **MEASURE the image** (§3y/§3x
  flagged ~3 KB body headroom; objfloat + formatfloat + parsenum-float + the
  soft-libm will likely overflow the ~896 KB Victor ceiling — levers are heap
  trim / feature trim; `--gc-sections` strips unused exp2f/log2f/expf/logf,
  keeping only powf), then run a float feature probe on Victor.
- **Reduction discipline reminder:** the dominant 93-TU blocker reduced cleanly;
  the remaining 4 (objfloat/objtype = qstr; parsenum/modbuiltins/binary =
  compiler) each need their own reduced `minic/dos/examples/*_probe.c` + gate
  before relying on the MP behavior, same as every prior §.

# Next session (§3y — transcendental soft-libm: exp2/log2/exp/log + powf)

## 2026-06-08 §3y notes (powf landed — the last soft-libm LINK blocker for MICROPY_FLOAT_IMPL_FLOAT)
- **Goal: implement the transcendental soft-libm `powf` (and the exp/log it
  needs)** — §3x's audit found `powf` is the one transcendental the curated
  MicroPython core references at LINK time under `MICROPY_FLOAT_IMPL_FLOAT`
  (objfloat `**`, parsenum `1eN`, modbuiltins `round(x,n)`); the algebraic
  surface (floor/ceil/round/fmod/fabs/copysign/isnan/isinf/signbit) was done
  in §3x.  This session closes the `powf` gap.
- **`minic/dos/softfloat.c` — added the transcendental core** (after `sf_fmod`):
  - `ieee_exp2(U32)` — 2^x: split x = n + r (n = nearest int via `sf_round`,
    r in [-0.5,0.5]), `sf_exp2_frac(r)` is a degree-7 Taylor in r with
    coefficients (ln2)^k/k!, then `sf_scalbn(g, n)` adds n to the exponent
    field (clamps to signed inf / signed zero).  Clamps |x| extremes first.
  - `ieee_log2(U32)` — log2(x): decompose x = 2^e·m, recentre m to
    [√½,√2), atanh series `s=(m-1)/(m+1)`, `log(m)=2s·(1+s²/3+s⁴/5+…)`
    (degree-9, 5 bracket terms), `log2(x)=e+log(m)·(1/ln2)`.
  - `sf_expf`/`sf_logf` are derived: `e^x = exp2(x·log2(e))`,
    `ln(x)=log2(x)·ln2`.  `sf_exp2f`/`sf_log2f` are thin wrappers.
  - `sf_powf(x,y) = 2^(y·log2(x))` with an **exact integer-exponent fast
    path** (binary exponentiation, `|y|≤64`) so `2**10`/`10**5`/`round(x,n)`
    are exact (the exp2/log2 round-trip alone gives `10**5 = 99999.977`); the
    squaring loop carries the sign of a negative base for free.  Full edge
    handling: `x^0=1`, `1^y=1`, nan, `0^±`, negative base (`nan` for
    non-integer exponent, signed for odd integer).  `sf_int_parity()` returns
    -1/0/1 (not-integer / even / odd).
  - All built on the exact `sf_add/sub/mul/div`, no float operators inside
    (consistent with the §3x algebraic helpers).
- **`minic/include/math.h`** — declared the 5 helpers and mapped
  `exp2f/exp2/log2f/log2/expf/exp/logf/log/powf/pow` to them.
- **Host validation FIRST** (the fast loop): compiled softfloat.c with
  `-DSF_HOST` + a libm-comparison harness — every case within ~2 ulps of
  glibc (rel ≤ 2.3e-7); integer powers exact.  Two bugs caught on the host
  before DOSBox: (1) initial pass forgot the integer fast path → `pow 10,5`
  off by 2 ulps (added it); (2) the fast path passed `sf_frombits(...)` (a
  float) to `sf_to_int` (which wants a BIT PATTERN) → exponent read as a
  denormal → `ye=0` → every integer power returned 1.  Fixed to
  `sf_to_int(ay & ABS_MASK)`.
- **Probe `minic/dos/examples/softtrig_probe.c` (+golden), gated medium with
  `--softfloat`** (`tools/test-dos.sh` **217→218 ok**).  19 lines: exp2/log2,
  exp/log, integer-pow fast path (`2**10`,`10**5`,`10**-2`,`(-2)**3`,`(-2)**2`),
  fractional pow (`2**0.5`,`9**0.5`,`3**3.3`), and edges (`x^0`,`0^3`,
  `(-2)**2.5`→nan).  Bit patterns round-trip exactly in DOSBox (golden
  generated from the SF_HOST build, 32-bit union to match the target's
  32-bit `unsigned long`).  Hit the known minic limit `{ U32 a=.., b=..; }`
  (multi-declarator-with-init in an inner block) → split into two decls.
- **Gates:** `make check` green; `tools/test-dos.sh` **218/218 ok**.  No
  MicroPython rebuild (float still `NONE` — flip is the next step).
- **Soft-libm is now LINK-complete for `MICROPY_FLOAT_IMPL_FLOAT`.**  Next
  steps (the remaining items 2-3 from §3x, now unblocked on the math side):
  1. **Wire softfloat.c into `tools/build-micropython.sh`** (always link it
     under float) and point the MP build's `<math.h>` at the real
     `minic/include/math.h` — `build/mp-spike/stubinc/math.h` is an EMPTY stub
     that SHADOWS the real one (`stubinc` is `-I`'d first); replace/redirect
     it for the MP build.
  2. **Flip `ports/dos8086/mpconfigport.h`** `MICROPY_FLOAT_IMPL` →
     `MICROPY_FLOAT_IMPL_FLOAT`, build compact far-data `--keep-going`, and
     **MEASURE the image**.  §3x flagged only ~3 KB body headroom; objfloat +
     formatfloat + parsenum-float + the soft-libm will likely overflow the
     ~896 KB Victor ceiling.  If so the levers are heap trim
     (`MICROPY_HEAP_SIZE`) or a feature trim — `--gc-sections`/`--pack-code`
     won't help (the float type is reachable once enabled, and gc-sections
     WILL strip the unused exp2f/log2f/expf/logf, keeping only powf).  Then
     run a float feature probe on Victor.

## 2026-06-07 §3x notes (toward MICROPY_FLOAT_IMPL_FLOAT: soft-libm groundwork)

## 2026-06-07 §3x notes (toward MICROPY_FLOAT_IMPL_FLOAT: soft-libm groundwork)
- **Goal was to enable `MICROPY_FLOAT_IMPL_FLOAT`.**  Audit first: under that
  config the curated MicroPython core references a soft-libm at LINK time
  (`parsenum.c`/`objfloat.c`/`modbuiltins.c` reference `powf`/`floorf`/`fmodf`/
  `copysignf`/`nearbyintf`/`nanf`; `formatfloat.c` references
  `isnan`/`isinf`/`signbit`/`fabsf`).  The existing soft-float surface was only
  `sf_add/sub/mul/div/from_int/to_int/cmp`.  Plus the image has only ~3 KB of
  body headroom (body 821168 loads; 824416 reports "Program too big").  So the
  flip is a multi-front effort, not a flag change — this session built the
  prerequisite **algebraic** soft-libm and fixed a backend bug it surfaced.
- **`minic/dos/softfloat.c` — added the EXACT/algebraic helpers** (no
  transcendentals): `sf_isnan/sf_isinf/sf_signbit`, `sf_fabs`, `sf_copysign`,
  `sf_nan`, `sf_trunc`, `sf_floor`, `sf_ceil`, `sf_round`, `sf_nearbyint`,
  `sf_fmod`.  These take/return honest `float`/`int` (called from C source, not
  emitted by the backend), reinterpreting to bits via a `union sf_cvt`.  All
  work on the 32-bit bit pattern and reuse the existing `sf_add/sub/cmp/to_int`
  (no float operators inside, so no `_sf_` lowering of the helpers themselves).
  `sf_fmod` is exact (exponent-aligned shift-subtract).  **`powf` is
  deliberately ABSENT** — it needs a soft `expf`/`logf` and is the next piece.
- **`minic/include/math.h` (NEW)** — declares the `sf_*` helpers and maps the
  libm names to them (`floorf`→`sf_floor`, `isnan`→`sf_isnan`, `fabsf`/`fabs`,
  `copysignf`, `nanf`/`nan`, `truncf`, `ceilf`, `roundf`, `nearbyintf`,
  `fmodf`, ...).  No `powf` yet.
- **Backend bug found + fixed (`copy.c`):** the soft-libm `floor/ceil/round/
  nearbyint/fmod` came out with INVERTED sign decisions (`fmodf(7,3)`→-1.0,
  `fmodf(-7,3)`→+1.0) while `sf_signbit` standalone was fine.  Reduced to
  `(int)(a >> 31) && (t != a)`: the `(int)` cast emits `%w =w copy %l` (a real
  16-bit truncation on i8086, where `l`=4-byte pair, `w`=2-byte reg), and
  `copy.c`'s `copyref()` folded EVERY `Ocopy` to its source — sound on
  word-uniform targets (registers alias) but on i8086 it let the `jnz` (a `w`
  use) reference the wider `l` temp; spill then parked it in a 4-byte slot and
  rega never reloaded the low word into the branch register, so the branch
  tested garbage.  **Fix:** `copyref()` no longer folds a class-narrowing copy
  (`i->cls==Kw` of a non-`Kw` temp) when `T.wordsz==2`; the explicit low-word
  `mov` is kept.  Generic-pass change gated on the i8086 word size, same shape
  as the `load.c` `T.wordsz` precedent.  `make check` green (no SSA regression).
- **Probes (both NEW, gated medium):**
  - `kl_narrow_copy_branch_probe.c` — pins the copy.c fix directly
    (`(int)(a>>31) && ...`, bug-loud: inverted sign without the fix).  Pure
    integer, no softfloat link needed.
  - `softlibm_probe.c` (`--softfloat`) — exercises every algebraic helper
    against known bit patterns via a union (`fabs/copysign/trunc/floor/ceil/
    round/nearbyint/fmod` + `isnan/isinf/signbit/nan`).
- **Gates:** `make check` green; `tools/test-dos.sh` **215→217 ok**.  No
  MicroPython rebuild this session (float not yet flipped).
- **Next on the float path (to actually enable `MICROPY_FLOAT_IMPL_FLOAT`):**
  1. **Implement `powf`** (and the soft `expf`/`logf`/`exp2f`/`log2f` it needs)
     in softfloat.c + math.h.  Integer-exponent fast path covers parsenum
     (`1e5`), `round(x,n)`, and integer `**`; the general fractional path needs
     exp/log.  This is the remaining hard blocker before MP float can LINK.
  2. **Wire softfloat.c into `tools/build-micropython.sh`** (add a `--softfloat`
     equivalent / always link it under float) and point the MP build's math.h
     at the real one — note `build/mp-spike/stubinc/math.h` is an EMPTY stub
     that currently SHADOWS `minic/include/math.h` (stubinc is `-I`'d first);
     replace/redirect it for the MP build.
  3. **Flip `ports/dos8086/mpconfigport.h`** `MICROPY_FLOAT_IMPL` →
     `MICROPY_FLOAT_IMPL_FLOAT`, build compact far-data with `--keep-going`,
     and MEASURE the image.  Expect the ~3 KB body headroom to be the wall:
     objfloat+formatfloat+parsenum-float+soft-libm will likely overflow.  If so,
     the levers are heap trim (`MICROPY_HEAP_SIZE`), a feature trim, or
     `--gc-sections`/`--pack-code` already in place won't help (float type is
     reachable once enabled).  Then run a float feature probe on Victor.

# Next session (§3w — far-data single-precision float load/store)

## 2026-06-07 §3w notes (float through a far pointer: loadfs/storefs)
- **Closed the last deferred far-data float gap.** Under compact/large/huge a
  `float` global/array/struct-member lives in a far segment, so reading/writing
  it goes through the i8086 far load/store path.  minic routed a far float
  through `loadfw`/`storefw` (16-bit) and silently truncated the 32-bit Ks
  value to its low half.  Now there are dedicated far single-float ops.
- **New QBE ops `loadfs` / `storefs`** (ops.h), mirroring `loadfl`/`storefl`
  but with a Ks value: `loadfs` result Ks ← far ptr (`l`); `storefs` value Ks,
  far addr (`l`).  Wired into `all.h` (`isloadfar`→`Oloadfs`, `isstore` 2nd
  range→`Ostorefs`) and `load.c` `storesz` (Ostorefs = 4 bytes).  The QBE IL
  lexer perfect-hash (parse.c `K`) did NOT collide — no regen needed.
- **i8086 backend** (`i8086/emit.c`): `Oloadfs` falls into the `Oloadfl`
  handler, `Ostorefs` into `Ostorefl` — the far 32-bit DX:AX move is
  class-agnostic.  Oloadfs (cls Ks) is excluded from the soft-float `(2)` Ks
  guard so it reaches the main op switch with the other far ops.
- **minic** (`minic.y`): `loadfar`/`storefar` + the 3 inline member/array
  `storef*` sites gain an `'s'` branch (loadfs/storefs).
- **Also fixed a float usual-conversion bug exposed under far-data:** the
  float↔double conversion sites compared the FULL ctyp (`a.ctyp != b.ctyp`)
  to decide whether to emit `exts`/`truncd`.  Under far-data a float VALUE
  carries an extra `FAR` bit, so `float = float` (and `float + float`, and
  `return float`) spuriously emitted a `truncd` on an already-Ks operand
  (QBE: "invalid type for first operand in truncd").  Fixed 4 sites (binop l/r
  at minic.y ~1843/1868, assignment ~3921, return ~4688) to compare only the
  floating PRECISION: `(KIND(a)==LNG) != (KIND(b)==LNG)`.  Medium codegen is
  unaffected (the FAR bit is never set there).
- **Probe:** `minic/dos/examples/float_fardata_probe.c` (+golden), gated under
  COMPACT/LARGE/HUGE with `--softfloat`.  Exercises far round-trip, far
  arithmetic (`g_c = g_a OP g_b`), float through an explicit far pointer, far
  float array element, far float struct member, and far compare — all via a
  near `union` so a truncated high word prints a wrong `%08lx`.  Bit patterns
  round-trip exactly on all three far-data models in DOSBox.
- **Gates:** `make check` green; `tools/test-dos.sh` **215/215 ok**.  Soft-float
  is now model-complete (medium arith/compare/convert + far-data load/store).
- **Next on the float path:** `MICROPY_FLOAT_IMPL_FLOAT` — enable single-float
  in the MicroPython compact far-data build and run a float feature probe on
  Victor.  Watch image size (MP is currently `MICROPY_FLOAT_IMPL=NONE`;
  enabling float pulls in objfloat.c + float formatting and will grow the
  image — may bump against the Victor load ceiling).

# Next session (§3v — unary-minus-on-float / float-vs-int usual conversions)

## 2026-06-07 §3v notes (unary minus on a float → single precision)
- **Compiler change — float usual-arithmetic-conversions corrected:** minic's
  binop type-promotion promoted `float OP int` (and `int OP float`) to `double`
  (Kd), which the i8086 soft-float backend `die()`s on.  Per C, a `float`
  combined with an integer stays `float` — the integer converts to float; only
  a `double` operand makes the result double.  `minic/minic.y` now computes the
  common floating type as `double` iff either operand is itself double, else
  `float` (action-body only, no grammar change).
- **Closes unary minus on a float:** `mkneg` desugars `-x` to `0 - x` with an
  integer `0`; that subtraction now stays Ks instead of Kd.  Verified `-x` →
  `=s sub` → `call far _sf_sub`, `x + 1` → `=s add` → `call far _sf_add`.
- **Probe:** extended `minic/dos/examples/softfloat_probe.c` (+golden) with
  direct `-f3`, `-fneg7`, `f3 + 1`, `2 - f1`, `f3 * 2`; removed the stale
  "deliberately avoids unary minus" note.  DOSBox run matches golden.
- **Gates:** `make check` green; `tools/test-dos.sh` **212/212 ok**.  MicroPython
  compact far-data rebuild **106/106 objects, image 844288 — byte-identical** to
  §3u (MP is `MICROPY_FLOAT_IMPL=NONE`, so no float-promotion path is reachable).
- **Next on the float path:** far-data `Ks` load/store (`loadfw`/`storefw`
  truncate Ks through a far ptr — the `[[storefar-lacks-storefl]]` family
  extended to Ks), then `MICROPY_FLOAT_IMPL_FLOAT`.

# Next session (§3u — float-literal Ks typing + feature-surface validation)

## 2026-06-07 §3u continuation notes (float literal `1.5f` → Ks; broad MP validation)
- **Validation sweep first (no compiler bug found — current image is solid):**
  - `make check` green; `tools/test-dos.sh` 211/211 before changes.
  - `build/mp-frontier2.py` on the normal compact image: reaches `OK filtercomp`, then stops at the `recsum(30)` case — the KNOWN deep-recursion frontier (a runtime stack / image-size tradeoff documented in §3o/§3t, not a clean compiler bug; stackless-strict fixes it but doesn't fit the ceiling as the default).
  - GC pressure is CORRECT: `build/mp-churn-scale.py` passes churn3/10/20/40 (clean `D4`/`C5`).  `build/mp-churn120.py` prints `XX churn 7980 7860` — but **7980 is the correct answer** (`sum(i+7 for i in 0..119) = 7140+840 = 7980`); the scratch script's golden `7860` is wrong (so is `mp-churn60.py`'s `2130`, should be `2190`).  These `build/*.py` are untracked scratch, not gated probes.
  - New broad probe `build/mp-strfeat-probe.py`: slicing (`[a:b]`, negative), `split`/`join`/`find`/`replace`/`strip`/`upper`/`startswith`/`endswith`, `hex`/`bin`/`int(base)`, string concat, `str.format` — ALL pass.  Exception tracebacks render correctly throughout.  The only failures were minimal-config feature gates (extended `[::-1]` slices → `NotImplementedError: only slices with step=1`; `str.count` → `AttributeError`), i.e. MicroPython config decisions, NOT compiler bugs.
- **Compiler change this session — `1.5f` → single-precision (Ks):** minic used to type every float literal as `double` (Kd), which the i8086 soft-float backend `die()`s on.  Now an `f`/`F`-suffixed literal types as `float` (Ks) and lowers to the `_sf_*` helpers; un-suffixed literals stay double.
  - `minic/minic.y`: lexer tracks a new `single_float` flag and stamps it on the `'F'` node's `nlong` field (unused for `'F'` until now); `expr()` `case 'F'` branches on `n->nlong` to emit `=s copy s_<v>` (ctyp `INT|FLOAT`) vs `=d copy d_<v>` (ctyp `LNG|FLOAT`).  No grammar change.
  - Verified end-to-end: `x + 1.5f` → `s_1.5` → pattern `0x3FC00000` → `call _sf_add` (`/tmp` SSA smoke + DOSBox).
  - Probe `minic/dos/examples/float_literal_probe.c` (+golden), wired into `tools/test-dos.sh` MEDIUM with `--softfloat`.  Combines literals with runtime floats so QBE can't fold them to constants; a mis-typed double literal would `die()` in the backend, so merely running proves Ks.  Updated the stale "float literals → double" note in `softfloat_probe.c`.
  - Gates: `make check` green; `tools/test-dos.sh` **212/212 ok**.  MicroPython compact far-data rebuild **106/106 objects, image 844288 — byte-identical** to the §3t image (MP's `MICROPY_FLOAT_IMPL=NONE` has no reachable `f`-literals, so no MP behavior change).
- **Next on the float path:** unary minus on a float (`-x` still desugars to `0.0 - x` in double → Kd; see softfloat_probe note), then far-data `Ks` load/store (`loadfw`/`storefw` truncate Ks through a far ptr — the `[[storefar-lacks-storefl]]` family extended to Ks), then `MICROPY_FLOAT_IMPL_FLOAT`.

## 2026-06-07 Codex continuation notes (globals-map corruption fixed)
- Root-caused and fixed the `HAS_CK False` globals-map corruption from the prior frontier.
- **Bug:** `block_scope_decl()` in `minic/minic.y` folded a block-scoped *array* and a sibling-block *pointer* of the same name + element type into one stack slot — it compared only `ctyp`, not array-ness.  MicroPython's list-comprehension codegen emits exactly that shape (an `args2[N]` array in one branch, an `obj_t *args2` pointer in another), so the array's `memcpy` wrote through into adjacent storage and clobbered the globals map (visible as the `ck` key turning into a non-string `<>` object).
- **Fix:** `block_scope_decl` now takes an `isarray` arg and renames on `varh[h].ctyp != ctyp || varh[h].isarray != isarray`.  All 8 call sites updated; the two array-decl stmt rules and the unsized/sized local-array-init rules that previously bypassed renaming (`v = $2->u.v`) now route through `block_scope_decl(..., 1)`.  No grammar change (action-body only), `make check` green.
- **Probe:** `minic/dos/examples/local_array_memcpy_probe.c` (compact, gated).  Bug-loud confirmed: without the fix `victim0/victim1` clobber to `286335522`/`858997828`; with it they read the correct `1431660134`/`2004322440`.
- **Verification:**
  - `tools/test-dos.sh`: **211/211 ok** (was 210; +1 for the new probe).
  - MicroPython compact far-data rebuilt: 106/106 objects, body `821184` (+32 B), total `844288` (under the Victor ceiling).
  - `VICTOR_SRC=build/mp-repeat-comp-globals-direct.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260` now prints `HAS_CK True`, `HAS_BASE True`, `HAS_ARG True`, `DONE`, clean `D4`/`C5`.
- Next: resume the "Candidate next exercises" list below — heavier string formatting / `repr` / GC pressure, and the stackless-strict recursion frontier.  (The float path is CLOSED as of §4a — see top of file.)

## Active focus
Stevie §3r is closed.  Manual MAME testing confirms `dw`/`de` work, matching the scripted Victor/MAME checks and the tracked gates.  Keep Stevie as a regression target, but stop using it as the primary driver unless a new editor regression appears.

Return to the MicroPython port as the main exercise tool for identifying QBE/MiniC/i8086/runtime improvements:
- Run real MicroPython features on the Victor/compact far-data build.
- When MicroPython exposes a failure, reduce it to a tiny MiniC/DOS probe first.
- Fix the compiler/backend/runtime bug underneath it.
- Add the reduced probe to `tools/test-dos.sh` before relying on the MicroPython behavior.
- Keep watching image size; previous MicroPython builds were close to the practical Victor load ceiling.

## Starting point
- Current QBE gates were green after the latest compiler fix: `make check` and `tools/test-dos.sh` (`209/209 ok`).  Re-run them before committing any compiler/backend changes.
- Stevie §3r details are archived in `SESSION_LOG.md`; current status is no `dw`/`de`/`yw` through-EOF reproduction in tracked probes, redirected Victor/MAME edit-loop checks, or manual MAME testing.
- MicroPython compact far-data was rebuilt on 2026-06-06 with current QBE: 106/106 objects, body `821072` after increasing the link stack to 24 KiB.
- Victor/MAME smoke coverage passes on the rebuilt image:
  - `build/mp-test.py`: primes/list-comprehension/fib/dict loop, clean `D4`/`C5`.
  - `build/mp-feature-probe.py`: integer ops, classes/inheritance, string methods, list sort/comprehension, generators, exceptions/finally, min/max/abs/sorted/enumerate, clean `D4`/`C5`.
  - `build/mp-frontier.py`: kwargs/defaults, closures, tuple unpacking, recursion to 6, allocation churn, clean `D4`/`C5`.
- The 24 KiB stack change fixes the old 8 KiB-stack return-path corruption reproduced by `build/mp-recsum8.py` (`recsum(8)` now prints `DONE`, `D4`, `C5`).
- Deep recursion is still bounded by stack/runtime behavior, not yet a compiler probe: `build/mp-frontier2.py` reaches dict/filter comprehensions then fails at `recsum(30)`; `build/mp-recsum-probe.py` reaches `recsum(12)` on the 24 KiB stack and fails by `recsum(20)` with the uncaught path printing `(nil)`.  A 28 KiB link stack produces body `824416` and does not load on Victor (`Program too big to fit in memory`), while 32 KiB cannot link (`DGROUP + stack overflows 64KB`).
- The latest reduced compiler issue was an unsigned int-to-long promotion bug in MiniC: `prom()` sign-extended unsigned `int` operands when comparing/promoting against `long`.  This was fixed with `extuw` for unsigned widening and covered by compact DOS probe `minic/dos/examples/uint_widen_cmp_probe.c`.
- MicroPython stack-check experiments exposed that unsigned-widening bug in `mp_cstack_check()`, but stack checks were not left enabled.  With the normal minimum-ROM no-stack-check config restored, MicroPython still reaches `recsum(12)` and still fails around `recsum(20)` with `(nil)`.
- Software single-precision float is COMPLETE for DOS (medium + far-data, literal/unary typing, double→single, static init, soft-libm) and gated via the `--softfloat` probes.  MicroPython float enablement is CLOSED (§4a, won't-fit on Victor) — float is NOT a target; the port stays integer-only.
- The next session should investigate whether the remaining deep-recursion failure is expected MicroPython stack-limit handling, a bad uncaught-stack-overflow exception path, or excessive i8086 VM C-frame size.  If it becomes a compiler/backend issue, reduce it to a focused `minic/dos/examples/*_probe.c` before fixing.

## 2026-06-06 Codex continuation notes
- Re-verified the normal compact far-data MicroPython image: 106/106 objects, body `821072`, total `844176`.
- `make check` passes and the full DOS gate passes: `tools/test-dos.sh` reports `209/209 ok`, including `uint_widen_cmp_probe`.
- `uint_widen_cmp_probe` also passes standalone under compact DOSBox:
  - `field_hi=1`, `field_lo=0`, `local_hi=1`, `local_lo=0`.
- Victor/MAME MicroPython status with the normal no-stack-check image:
  - `build/mp-test.py` via redirected REPL passes and reaches clean `C5`.
  - `build/mp-frontier.py` via redirected REPL passes and reaches clean `C5`.
  - `build/mp-feature-probe.py` should be run as whole-file `PROG.PY` via `VICTOR_SRC=build/mp-feature-probe.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260`; in that mode it passes all listed feature checks and reaches `D4`/`C5`.  The redirected REPL harness can split nested compound blocks in this script and produce `IndentationError`/`SyntaxError`; treat that as a REPL harness limitation, not a VM/compiler failure.
  - `build/mp-recsum-probe.py` via whole-file `PROG.PY` still reproduces the frontier: `recsum(6)` and `recsum(12)` complete, then `recsum(20)` enters the uncaught path as `DE` + `(nil)`.
  - `build/mp-frontier2.py` via whole-file `PROG.PY` reaches `OK nonlocal1`, `OK nonlocal2`, `OK tuple-loop`, `OK dictcomp`, `OK filtercomp`, then does not reach `C5` at the `recsum(30)` case.
- Added `MP_EXTRA_CPPFLAGS` support to `tools/build-micropython.sh` and `tools/recompile-mp-tu.sh` so MicroPython config experiments can be driven from the QBE repo without editing the external MicroPython checkout.
- Stack-check experiment:
  - Build command: `MP_EXTRA_CPPFLAGS='-DMICROPY_STACK_CHECK=1' tools/build-micropython.sh --model=compact`.
  - The generated `mp_cstack_check()` now uses `extuw` for `stack_limit` before the unsigned compare, confirming the earlier MiniC unsigned-widen fix applies to the real MicroPython path.
  - The stack-check image links but is too large for the Victor load ceiling: body `825648`, total `849104`, and Victor prints `Program too big to fit in memory`.
  - Conclusion: the remaining deep-recursion failure is presently a MicroPython port/runtime stack-guard and image-size tradeoff, not a proven compiler/backend bug.  To ship clean recursion failure handling, either recover roughly 5 KiB of image/body size, trim another feature/memory consumer, or implement a smaller DOS-port-specific recursion/stack guard.

## 2026-06-07 Codex continuation notes
- Added MicroPython build experiment knobs to `tools/build-micropython.sh` and `tools/recompile-mp-tu.sh`:
  - `MP_STACK_SIZE` overrides the linker stack size, default still `24576`.
  - `MP_STACK_LIMIT` rewrites the DOS port's `mp_stack_set_limit(8192)` in generated `main.pp.c`, default still `8192`.
  - `MP_HEAP_SIZE` rewrites generated `static char heap[(49152)]`, default still `49152`.
  - `MP_DOS_TINY_STACK_CHECK=1` rewrites generated `mp_cstack_check()` to a smaller DOS offset-only guard for stack-check experiments.
  - `MP_DOS_STACKLESS_RECURSION_RAISE=1` appends `mp_raise_recursion_depth()` to generated `runtime.pp.c` so `MICROPY_STACKLESS=1` + `MICROPY_STACKLESS_STRICT=1` can link without enabling global `MICROPY_STACK_CHECK`.
  - Full builds now refresh `/tmp/mp_objs.txt`, making `tools/recompile-mp-tu.sh` usable immediately after a full MicroPython build.
- Standard `MICROPY_STACK_CHECK=1` can be made to fit by reducing linker stack size, but it is not useful as-is on i8086:
  - `MP_STACK_SIZE=18432` links at total `842960`, but raises `RuntimeError: maximum recursion depth exceeded` before `recsum(6)` completes.
  - Raising `MP_STACK_LIMIT` through `14336`, `16384`, `20480`, `21504`, `22272`, and even `30000` still trips during shallow recursion.  The generated limit write is correct; the stack-check-enabled VM path simply consumes too much transient C stack for this byte-limit approach.
  - `MP_DOS_TINY_STACK_CHECK=1` removes the `mp_cstack_usage()` call/divide from `mp_cstack_check()`, but does not materially improve the shallow-recursion frontier.
- `MICROPY_STACKLESS=1 MICROPY_STACKLESS_STRICT=1` is the promising recursion direction:
  - Plain stackless strict initially failed to link because `_mp_raise_recursion_depth` was undefined; the generated `runtime.pp.c` patch above fixes that without enabling global stack checks.
  - With `MP_STACK_SIZE=8192`, stackless strict links at total `835088` with the default 48 KiB heap and passes `build/mp-recsum-probe.py`: `recsum(6)`, `recsum(12)`, `recsum(20)`, and `recsum(30)` all complete, followed by clean `D4`/`C5`.
  - Stackless strict also passes whole-file Victor/MAME runs of `build/mp-test.py`, `build/mp-feature-probe.py`, and `build/mp-frontier.py`.
  - `build/mp-frontier2.py` reaches `OK recsum`, so the old `recsum(30)` frontier is fixed; it does not complete the later heavy churn section within the tested run.
- New MicroPython frontier found while testing stackless: integer-to-string conversion hangs/restarts on both normal and stackless images.
  - Minimal reproducer: `build/mp-str-int-probe.py` with `print(str(0))`.
  - Normal no-stackless 24 KiB image prints startup markers and `A`, then never reaches the string output or `C5`.
  - Stackless image shows the same failure/re-entry pattern, so this is pre-existing, not a stackless regression.
  - A heavier churn expression also stalls at `str(i)` after proving list comprehension and `row[-1]` work (`build/mp-churn-progress.py` and `build/mp-dict-expr-probe.py`).

## 2026-06-07 Codex continuation notes (str(int) fixed)
- Reduced the MicroPython `str(int)` stall to `minic/dos/examples/mp_str_int_probe.c`, now gated under compact in `tools/test-dos.sh`.
- Two bugs were found/fixed:
  - `sizeof(s->buf)` for an array member accessed through a struct pointer decayed to pointer size (`4` in compact) instead of the declared array byte size.  MiniC now detects `sizeof(obj.arr)` / `sizeof(ptr->arr)` before expression decay, matching the existing bare-array `sizeof(arr)` behavior.
  - Far-data calls to `memmove()` were not remapped to a far-pointer runtime helper.  MicroPython's `vstr_add_strn()` passed 4-byte far pointers, but libstub `_memmove` consumed near-pointer args.  Added `_far_memmove` and remapped `memmove` in far-data models.
- Current normal compact far-data MicroPython image after the fix:
  - `tools/build-micropython.sh --model=compact`: 106/106 objects, body `821152`, total `844256` (about +80 bytes vs the prior normal image).
  - `VICTOR_SRC=build/mp-str-int-probe.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 260` now prints `A`, `0`, `B`, `123`, `DONE`, `D4`, `C5`.
  - `build/mp-test.py`, `build/mp-feature-probe.py`, and `build/mp-frontier.py` all pass whole-file Victor/MAME runs and reach clean `D4`/`C5`.
- Final gates after the fix:
  - `make check` passes.
  - `tools/test-dos.sh` passes: `210/210 ok`.

## Candidate next exercises
1. Continue pushing MicroPython features past the fixed `str(int)` frontier: heavier string formatting, `repr()`, dict/list rendering, exception tracebacks, and GC pressure.
2. Revisit stackless strict as the recursion direction now that string conversion is fixed; rerun `build/mp-frontier2.py` and the churn scripts to identify the next real frontier.
3. ~~Resume the float path~~ — CLOSED 2026-06-08 (§4a, user decision): float on
   Victor is "won't-fit, not worth it".  Soft-float stays a gated medium-model
   DOS capability only; the MicroPython port stays integer-only.  Do NOT
   re-attempt the flip.
4. For every failure, follow the same discipline as §3r: reproduce as a focused `minic/dos/examples/*_probe.c`, fix QBE/MiniC/runtime, then gate it.

## 2026-06-07 Codex continuation notes (post-str(int) frontier)
- Normal compact far-data MicroPython was rebuilt at `844256` bytes before probing.  A stackless strict image also rebuilt and fit at `835168` bytes.
- Rendering/string follow-up:
  - `build/mp-render-probe.py` passes on Victor: `repr(int)`, `repr(str)`, `repr(list)`, `repr(dict)`, and `str(ValueError("boom"))` all reach clean `D4`/`C5`.
  - Old-style `%` string formatting is not available in this minimal MicroPython config: `build/mp-format-probe.py` fails immediately with `TypeError: unsupported type for operator` on `"x=%s" % "ab"`.  Treat this as config/support, not a compiler bug unless the port intentionally enables string modulo formatting.
- New reproducible MicroPython frontier:
  - `build/mp-repeat-comp-globals-direct.py` is the cleanest current repro.  It defines `ck`, `comp_base`, and `comp_with_arg`; after two simple list-comprehension calls and one argument-capturing list-comprehension call, `"ck" in globals()` becomes `False` while `"comp_base"` and `"comp_with_arg"` remain `True`.
  - `build/mp-repeat-comp-key-list-direct.py` shows the globals keys after the same sequence as `['comp_with_arg', <>, '__name__', 'comp_base']`: the `ck` key slot appears to have been overwritten/corrupted into a non-string object key rendered as `<>`.
  - Adding an extra global before `ck` avoids the symptom (`build/mp-first-global-probe.py`, `build/mp-first-function-probe.py`), so the failure is globals-map-layout sensitive.
  - Repeated scalar calls and repeated list-literal allocation pass (`build/mp-repeat-call-probe.py`), and the simple negative-`unsigned long` pointer-index/store-pop C shape was tested as a temporary DOS probe and passed.  The current evidence does not support `fastn[-unum] = (*sp--)` as the reduced compiler bug.
  - Speculative GC fixes were tried in generated `gc.pp.c` only: scanning `mp_state_ctx` at extra far-pointer alignments, extending roots through `qstr_last_chunk`, and scanning heap payloads every 2 bytes / with interior-pointer marking.  None fixed `HAS_CK False`; do not re-try those unchanged.
- Suggested next reduction path:
  - Instrument MicroPython at the C level around `mp_store_name()` / `mp_obj_dict_store()` / `mp_map_lookup()` for the failing script to dump the raw key/value words in `mp_state_ctx.vm.dict_main.map.table` before and after `comp_with_arg(0)`.
  - Look for a VM/compiler pattern that writes a non-qstr object into a globals map key slot.  The corruption is visible before any explicit key-list iteration; key-list probes that use `for k in globals()` mutate globals via `k`, so prefer `list(globals())` or direct membership probes.

## 2026-06-07 Codex continuation notes (repeat comprehension globals fixed)
- Fixed the `build/mp-repeat-comp-globals-direct.py` frontier.
  - Instrumentation showed top-level globals stores were clean: `ck` was inserted as qstr key `0:1754`.
  - After `comp_with_arg(0)`, the globals table slot for `ck` changed to two heap pointers, while no `mp_obj_dict_store()` ran.  The corruption happened inside the parent `comp_with_arg` before the child list-comprehension bytecode began.
  - Reduction found MiniC emitted duplicate SSA names for same-named block locals when one declaration was an array and a sibling declaration was a pointer of the same stored C type.  MicroPython's `closure_call()` has exactly this shape:
    - `if (...) { mp_obj_t args2[5]; ... }`
    - `else { mp_obj_t *args2 = ...; ... }`
  - The bad SSA was `%args2 =l alloc4 20` followed by `%args2 =l alloc4 4`; the array arm then loaded `%args2` as a pointer and passed an arbitrary destination to `_far_memcpy`.
- Fix:
  - `block_scope_decl()` now considers both stored type and `isarray` when deciding whether to alpha-rename a colliding block local.
  - Block-scoped array declaration rules now call `block_scope_decl(..., isarray=1)`, including fixed-size, initialized fixed-size, and unsized initialized arrays.
  - Added compact DOS regression `minic/dos/examples/local_array_memcpy_probe.c`, gated in `tools/test-dos.sh`, to pin the shadowed local-array/local-pointer case.
- Verification after the fix:
  - `make check` passes.
  - `tools/test-dos.sh` passes: `211/211 ok`.
  - Clean normal compact far-data MicroPython rebuild: 106/106 objects, image `844272` bytes, body `821168`.
  - `build/mp-repeat-comp-globals-direct.py` now prints `HAS_CK True`, `HAS_BASE True`, `HAS_ARG True`, then clean `D4`/`C5`.
  - `build/mp-repeat-comp-key-list-direct.py` prints `KEYS ['comp_with_arg', 'ck', '__name__', 'comp_base']`, with no `<>` key.
  - `build/mp-test.py` and `build/mp-feature-probe.py` passed on the rebuilt image before the final no-op unsized-array scope hook cleanup; direct repro was rerun after the final rebuild.
- Next MicroPython exercise:
  - Continue past the fixed repeated-comprehension/globals frontier.  Good next scripts are `build/mp-frontier.py`, `build/mp-frontier2.py`, and the churn probes (`build/mp-churn-progress.py`, `build/mp-dict-expr-probe.py`, etc.).
