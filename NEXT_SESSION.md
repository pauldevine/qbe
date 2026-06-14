# Next session (§7a — continue Phase 6 / open compiler tracks.  §6z [2026-06-13, this session] fixed a minic **front-end parse bug: `const`/`volatile`-qualified FLOATING-point declarations did not parse** — `const float`, `const double`, `volatile float`, `volatile double`, and `const volatile double` were hard `parse error`s at any scope, while bare `float`/`double` and `const int` always worked.  This was the carried open track listed as "minic static-init FLOAT const-expr folding (`static float x = 2.0f*3.14f;`) — also unlocks MICROPY_PY_MATH_CONSTANTS", but the diagnosis in that note was WRONG: **const-expr folding was never broken** — `2.0f*3.14f`, `3.14159/2.0`, `6.0f/2.0f`, etc. already fold to a single-precision `data` constant.  The real defect was purely in the `type` grammar (minic/minic.y ~line 8484): it enumerates `CONST TINT`/`CONST TCHAR`/`CONST TLNG`/… and the parallel `vol_qual T…` integer cases, but **omitted the floating forms** — there was no `CONST TFLOAT`/`CONST TDOUBLE` nor `vol_qual TFLOAT`/`vol_qual TDOUBLE` production, so the parser had no action for `const`/`volatile` followed by `float`/`double` and died.  **The fix is four new grammar productions**, each mapping (exactly like the bare `TFLOAT`/`TDOUBLE` rules at lines 8460–8461) to `INT | FLOAT` — double aliases to single-precision (Ks) on i8086 — with the `vol_qual` pair additionally OR-ing `QVOLATILE` and setting `g_decl_volatile = 1`, mirroring every other `vol_qual T…` rule (`const` adds nothing in minic; `volatile` drives the QVOLATILE machinery exactly as the integer cases do).  **No semantic/codegen surface changed** — these productions only fire on token sequences that previously had NO valid parse, so every input that already parsed produces an identical AST.  **Grammar conflict count UNCHANGED**: 115 shift/reduce, 0 reduce/reduce, and "10 rules never reduced" is the pre-existing baseline (verified by stashing the change and rebuilding).  **Gated bug-loud** with a new `minic/dos/examples/const_float_init_probe.c` (+ `minic/dos/tests/const_float_init_probe.golden.txt`), wired into `tools/test-dos.sh` at MEDIUM + COMPACT with `--softfloat` (added to both the runtime-case list and the `sfflag` basename `case`, alongside the sibling float probes): it declares the previously-unparseable forms at file scope — `static const float pi`, `static const double e`, const-expr folds behind a const qualifier (`static const float twopi = 2.0f * 3.14159…f`, `static const float half = 3.14159… / 2.0`), a `static const float tbl[3]` array, a non-static `const float gquarter` (external linkage → `export data`), a `static volatile float vf`, and a `static const volatile double cvd` — printing each value's exact IEEE-754 single bit pattern through a `union { float; unsigned long; }` (the `float_literal_probe` idiom, so the golden is exact with no float printf).  Verified bug-loud: the UNFIXED minic (git stash) errors `error:41: parse error` on the very first `static const float` line.  **test-dos 296/296 → 298/298** (the two new MEDIUM+COMPACT entries `[ok]`, every prior entry unchanged).  Since this is a minic.y grammar change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed.  **Bonus**: this removes the front-end blocker for `MICROPY_PY_MATH_CONSTANTS` (its `const float` definitions of M_PI/M_E now parse) — but MP is PARKED as a byte-compare corpus (the math-constants memory note still says keep it 0), so that feature was NOT turned on; the relevant carried open track is now CLOSED/CORRECTED.  Next: pick another carried compiler track (small setjmp/longjmp — newlibc may want it; huge `_qbe_huge_add` ≥0x8000 §4i; multi-decl block_scope_decl; far static-DATA-ptr reloc §1g; param/static-local shadowing a global; Kw spill-slot sharing) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback device, which collides with the rs232a `null_modem` capture so the gate's serial capture must move to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED (§6v `[90,110]` FAIL-window + raw iteration count).)

## §6z session notes (2026-06-13)

### The bug: missing CONST/vol_qual TFLOAT|TDOUBLE grammar productions
- minic's `type` nonterminal (minic/minic.y ~8484) enumerates `CONST T…`
  and `vol_qual T…` for every INTEGER base type but had NO floating forms.
- So `const float`/`const double`/`volatile float`/`volatile double`/
  `const volatile double` were hard `parse error`s — at file scope, local
  scope, anywhere.  Bare `float`/`double` and `const int` always parsed,
  which masked it.
- The carried-track note "minic static-init FLOAT const-expr folding" was a
  MISDIAGNOSIS: folding works (`2.0f*3.14f` → a single-precision `data`
  constant already).  The defect was purely the missing qualifier+float
  grammar rules.

### The fix: four additive productions, semantics-neutral
- Added `CONST TFLOAT`/`CONST TDOUBLE` → `INT | FLOAT` (matching bare
  TFLOAT/TDOUBLE at lines 8460–8461; double aliases to single Ks on i8086).
- Added `vol_qual TFLOAT`/`vol_qual TDOUBLE` → `INT | FLOAT | QVOLATILE`
  with `g_decl_volatile = 1`, mirroring the integer `vol_qual T…` rules.
- Purely additive: fires only on token sequences that previously had no
  valid parse, so all previously-parsing input yields an identical AST.
- Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10-rules-never-reduced is
  the pre-existing baseline — confirmed by stash + rebuild).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/const_float_init_probe.c` + golden, wired into
  `tools/test-dos.sh` MEDIUM + COMPACT with `--softfloat` (runtime-case
  list AND the `sfflag` basename `case`).  Prints exact IEEE single bit
  patterns via a float/ulong union (float_literal_probe idiom).
- Bug-loud verified: unfixed minic (stash) → `error:41: parse error` on the
  first `static const float` line.
- **test-dos 296 → 298** (both new entries [ok], all prior unchanged).
- minic.y change (NOT emit.c) → NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  documented golden → codegen unchanged, NO Victor run.

### Bonus / closed track
- Removes the front-end blocker for MICROPY_PY_MATH_CONSTANTS (const-float
  M_PI/M_E now parse), but MP is PARKED (byte-compare corpus; math-constants
  stays 0) so the feature was NOT enabled.  The "static-init FLOAT
  const-expr folding" open track is now CLOSED/CORRECTED.

### Open tracks (carried)
- Compiler: small setjmp/longjmp (newlibc may want it); huge `_qbe_huge_add`
  ≥0x8000 (§4i); multi-decl items after the first skip block_scope_decl; far
  static-DATA-ptr reloc (§1g); param/static-local shadowing a global; Kw
  spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp (§4v, unreduced).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console +
  rs232a TXD→RXD MAME loopback device colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.

---

# Next session (§6z — continue Phase 6.  §6y [2026-06-13, this session] gated the UNMODIFIED upstream `pic_test` **BARE-METAL through bm_testhost + the bm_stdio/bm_pic stack — battery 37/37 → 38/38** (test-dos UNCHANGED at 296/296 — a bare-metal-only gate, like §6q/§6u/§6v/§6w).  This is the **first gate of the `pic_enable_irq()`/`pic_disable_irq()` IRQ-mask API** — the §6f hand-mirrored `pic_bm` exercised only `bm_pic_get_mask`/`set_mask`, and §6u's `driver_test` read the live IMR through the `PIC_GET_MASK()` MMIO macro, but the per-IRQ enable/disable mask primitives had never been driven by an upstream test.  `pic_test` has three parts: **Test 1 (`test_pic_mask`)** reads the runtime IMR (0xBB = IR2 timer + IR6 keyboard enabled, the deterministic `bm_pic_init` state), then `pic_disable_irq(IRQ_EXPANSION_5)` / `pic_enable_irq(IRQ_EXPANSION_5)` — IR5 is an UNUSED expansion bit, deliberately chosen so toggling it never disturbs the live timer/keyboard IRQs — asserting EXACT 0xBB→0xBB (IR5 already masked) → 0x9B (IR5 cleared) transitions, then `pic_set_mask(saved)` restore; **Test 2 (`test_pic_with_timer`)** reads a start tick, waits for ~100 ticks under the live timer ISR (bounded loop), and asserts the count advanced; **the EOI test is implicit** (continuous ticks ⇒ EOI is working).  **Two changes, both build-glue only (NOT compiler/qbe/emit/minic):** (1) `bm_shim.c` gained four PIC aliases — `pic_get_mask`/`pic_set_mask` → `bm_pic_get_mask`/`bm_pic_set_mask`, and `pic_enable_irq`/`pic_disable_irq` → `bm_pic_unmask`/`bm_pic_mask` (note the inversion: enable=unmask=CLEAR bit, disable=mask=SET bit, matching upstream `drivers/pic.c`) — mirroring the file's existing `timer_*`/`display_*`/`keyboard_*` (§6w) surfaces; bm_pic.c is ALREADY linked into every bm_stdio build (the testhost preamble calls `bm_pic_init`), so nothing NEW links, only wrapper symbols.  (2) A NEW support header `minic/dos/newlibc/interrupts.h` — a minic-dialect port of upstream `drivers/interrupts.h`.  `pic_test` `#include "interrupts.h"` **gratuitously** (it uses NO symbol from it), but the upstream header carries a `static inline get_interrupt_vector()` built on `SAVE_ES`/`RESTORE_ES` — ia16-elf-gcc extended `__asm__` with `"=m"`/`"m"` operand constraints.  minic does NOT drop unreferenced static functions the way gcc does (it emits one per including TU) AND it passes inline asm through verbatim, so the upstream body emitted AT&T `movw %es,...` that nasm (Intel syntax) **rejected** (`expression syntax error`).  The port mirrors the upstream declaration surface name-for-name (`ivt_entry_t`, `set_interrupt_vector`, the three `ISR_HANDLER` ISR prototypes, the `interrupts_init/enable/disable` trio, the `ISR_HANDLER` macro) but reimplements `get_interrupt_vector` as a plain far-pointer IVT read and makes `SAVE_ES`/`RESTORE_ES` no-ops — the SAME §6e/§6i ES-drop reasoning that dropped those asm sites from the `bm_*.c` driver ports (on this toolchain the §6d ISR ABI owns ES and a volatile far access carries its own segment).  It lives in `$NLC_DIR` (searched BEFORE `$NL/drivers` in the bare-metal include path), exactly the established `bm_interrupts.h`/`bm_sasi.h` header-port pattern, and is picked up ONLY by TUs that include the bare name `"interrupts.h"` — all the linked `bm_*.c` support TUs include `"bm_interrupts.h"` (the existing clean shim), so the only includer in any build is the upstream test itself; no existing DOS/MP/battery build is disturbed (MP never includes it).  **Golden character:** the Test-1 mask values are FULLY DETERMINISTIC (the IMR is the fixed bm_pic_init state, IR5 toggles are exact); only Test 2's **3 tick lines** (`Starting tick count: 6188` / `Final tick count: 6612` / `Ticks elapsed: 424`) are TIMING-DERIVED — RUN-STABLE (cycle-deterministic in MAME, verified byte-identical across two repeated runs before capture, per [[victor-harness-deterministic]]) but they WILL SHIFT on a bm_tty/printf codegen change → re-capture then (the PASS verdicts are robust: Test 1 is exact equality on deterministic values, Test 2 is `current > start`, so a tick shift is a LOUD diff, never a silent wrong pass — the §6v lesson).  Builds **SMALL** (60,121 B code, under the 64 KB single-`_TEXT` ceiling — portable stdio + timer/keyboard/pic surface, no fat_write.c/dirent.c/SASI bulk); ~60 output lines + a ~1 s timer wait reach `return 0` within a **90-emulated-second** budget (matching §6u's driver_test).  **Bare-metal ONLY** — the DOS host has no live 8253 timer nor 8259 PIC.  **FIRST-RUN PASS** on MAME, verified `tools/test-newlibc.sh pic_test` → **[ok]** end-to-end (the harness rebuilds and diffs live serial against the golden, so [ok] IS the gate); the additive aliases + header were confirmed non-disturbing by re-running `driver_test` (live timer+PIC sibling), `keyboard_nonblock_test` (shares the bm_shim.c alias file), and `stdin_test` (cooked stdio path) → all **[ok]**.  Since this is newlibc bare-metal support glue, NOT compiler/qbe/emit/minic, and MP links neither bm_shim.c nor interrupts.h → **no emit audit, no MP byte-compare** (house rules).  Next: with the keyboard family (raw-event §6w, nonblock-cooked §6x, cooked line/char §6n/§6o/§6t) AND the PIC mask API (§6y) now all gated, the upstream phase-3 tests that remain are `serial_loopback_test` (needs NEW harness plumbing — an rs232a TXD→RXD loopback attach, distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem — plus its own RX-timing determinism story; the only remaining tractable bm_testhost candidate, but it is real harness work, not the alias surface) and `interrupt_test` (stays SKIPPED — §6v's `[90,110]` FAIL-window + a raw busy-loop iteration count make it wrong to gate); the display-only/`hlt`-loop tests (`minimal_irq_test`/`segment_test`/`simple_screen_test`/`memory_test`/`font*_test`) are NOT bm_testhost-shaped and already covered by the hand-mirrored `bm_*` ports; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6y session notes (2026-06-13)

### Two changes, build-glue only — no compiler/qbe/emit/minic touched
- `bm_shim.c`: four PIC aliases so the UNMODIFIED upstream test links its
  unprefixed names (the §6w keyboard-alias pattern, applied to PIC):
    - `pic_get_mask`     → `bm_pic_get_mask`
    - `pic_set_mask`     → `bm_pic_set_mask`
    - `pic_enable_irq`   → `bm_pic_unmask`   (enable = CLEAR mask bit)
    - `pic_disable_irq`  → `bm_pic_mask`     (disable = SET mask bit)
  The enable/disable→unmask/mask inversion matches upstream `drivers/pic.c`
  semantics exactly.  bm_pic.c is already linked into every bm_stdio build
  (the testhost preamble calls `bm_pic_init`), so nothing NEW links.
- `minic/dos/newlibc/interrupts.h` (NEW): minic-dialect port of upstream
  `drivers/interrupts.h`.  See below.

### Why the interrupts.h port was needed (the one real friction)
- `pic_test` `#include "interrupts.h"` GRATUITOUSLY — it references no symbol
  from it.  `driver_test` (the only prior testhost test touching PIC) does
  NOT include it, so this is the first testhost test to pull it in.
- The upstream header is mostly pure declarations, but carries a
  `static inline get_interrupt_vector()` built on `SAVE_ES`/`RESTORE_ES`
  macros = ia16-elf-gcc extended `__asm__` with `"=m"`/`"m"` constraints.
- minic does NOT drop unreferenced static functions (it emits one per
  including TU) and passes inline asm through VERBATIM → the dead body
  emitted AT&T `movw %es, [pic_test_glo1]`, which nasm (Intel) rejects
  (`pic_test.omf.asm:29: expression syntax error`).  Confirmed by building
  against the real header first.
- The port mirrors the upstream API name-for-name (ivt_entry_t,
  set_interrupt_vector, the 3 ISR_HANDLER prototypes, interrupts_init/
  enable/disable, the ISR_HANDLER macro) but reimplements
  get_interrupt_vector as a plain far-pointer IVT read and no-ops
  SAVE_ES/RESTORE_ES — the §6e/§6i ES-drop reasoning (the §6d ISR ABI owns
  ES; a volatile far access carries its own segment).  Now the dead static
  emits valid i8086 codegen (GC'd at link).
- Scope is contained: it lives in `$NLC_DIR` (searched before `$NL/drivers`)
  and is picked up ONLY by includers of the bare name `"interrupts.h"`.
  Every linked `bm_*.c` support TU includes `"bm_interrupts.h"` (the
  existing clean shim), so the sole includer in any build is the upstream
  test itself — no DOS/MP/battery build disturbed.

### Golden: deterministic mask test + timing-derived ticks (§6v pattern)
- Test 1 mask values are FULLY DETERMINISTIC: IMR 0xBB (IR2+IR6 enabled,
  the fixed bm_pic_init state), IR5 disable→0xBB (already masked), enable→
  0x9B, restore→0xBB.
- Test 2's 3 tick lines (Starting 6188 / Final 6612 / Elapsed 424) are
  TIMING-DERIVED — run-stable (byte-identical across two MAME runs before
  capture) but WILL SHIFT on a bm_tty/printf codegen change → re-capture
  then.  PASS verdicts are robust (Test 1 exact-equality, Test 2
  current>start), so a tick shift is a LOUD diff, never a silent wrong pass.
- Golden `minic/dos/tests/pic_test.golden.txt` (68 lines): bm_testhost
  pic+timer/tty+sti/vfs preamble + the PIC test body + `test returned 0`.

### Model / budget / host
- SMALL (60,121 B code, under the 64 KB `_TEXT` ceiling — no fat_write.c/
  dirent.c/SASI).  90-s budget (~60 lines + ~1 s timer wait), matching §6u.
- Bare-metal ONLY: the DOS host has no live 8253 timer nor 8259 PIC.

### Verification
- `tools/test-newlibc.sh pic_test` → [ok] (FIRST-RUN PASS, golden
  byte-identical across two repeated MAME runs before capture).
- Additive changes confirmed non-disturbing: re-ran `driver_test` (live
  timer+PIC sibling), `keyboard_nonblock_test` (shares bm_shim.c), and
  `stdin_test` (cooked stdio) → all [ok].

### Open tracks (carried)
- `serial_loopback_test`: the only remaining tractable bm_testhost
  candidate, but needs NEW harness plumbing — an rs232a TXD→RXD loopback
  attach (distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem) — plus
  its own RX-timing determinism story.  Real harness work, not the alias
  surface.
- `interrupt_test`: stays SKIPPED (§6v's `[90,110]` FAIL-window + raw
  busy-loop iteration count → wrong to gate).
- display-only/`hlt`-loop tests (`minimal_irq_test`/`segment_test`/
  `simple_screen_test`/`memory_test`/`font*_test`) are NOT bm_testhost-shaped
  and already covered by the hand-mirrored `bm_*` ports.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6x and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
