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

# Next session (§6y — continue Phase 6.  §6x [2026-06-13, this session] gated the UNMODIFIED upstream `keyboard_nonblock_test` **BARE-METAL through bm_testhost + the bm_stdio/bm_keyboard stack, with ZERO compiler/toolchain/build-script changes — battery 36/36 → 37/37** (test-dos UNCHANGED at 296/296 — a bare-metal-only gate, like §6q/§6u/§6v/§6w).  This is the direct follow-up §6w predicted: where §6w's `keyboard_raw_test` exercised the RAW (uncooked) IR6 event API (`keyboard_get_raw_event_nonblock()`), `keyboard_nonblock_test` gates the **nonblock-COOKED-byte pair one layer up** — `keyboard_getc_nonblock()` (Test 1, with no key pending → returns `< 0` → `"OK: no key was pending."`) and `keyboard_hit()` polled inside a `wait_for_key()` loop bounded by a 500-tick (5 s) idle window (Test 2; with no key posted `keyboard_hit()` is always false → the loop times out → `"PASS: no key arrived during bounded idle check."` + `return 0`).  Like §6w it is run **with NO keypost**, which (a) sidesteps the keypost-vs-poll timing race the keyboard tests were parked on since §6u/§6v — no keys posted means no race, both idle branches are taken deterministically — and (b) makes both branches print **ONLY fixed text, no tick values**, so the golden (`minic/dos/tests/keyboard_nonblock_test.golden.txt`, 18 lines: bm_testhost `pic+timer`/`tty+sti`/`vfs` preamble + the 13-line test body + `test returned 0` trailer) is **fully toolchain-stable** (strictly better than §6v `simple_interrupt_test`'s timing-derived-tick golden that needs re-capture on any bm_tty/printf codegen change).  **ZERO new code** — the four keyboard aliases §6w added to `bm_shim.c` (`keyboard_get_raw_event_nonblock`/`keyboard_hit`/`keyboard_getc`/`keyboard_getc_nonblock` → `bm_keyboard_*`) ALREADY staged exactly the `keyboard_hit`/`keyboard_getc_nonblock` symbols this test needs (§6w explicitly noted "the aliases also pre-stage `keyboard_nonblock_test`"), and `timer_get_ticks`/`timer_delay_ms` resolve through the same file's timer surface — so like §6q/§6u/§6v/§6w the only changes are **one battery entry (`keyboard_nonblock_test:30:::`) + one bare-metal-captured golden**, no compiler/qbe/emit/minic/build-script source touched → **no emit audit, no MP byte-compare**.  Builds **SMALL** (59,271 B code, under the 64 KB single-`_TEXT` ceiling — portable stdio + lean timer/keyboard surface, no fat_write.c/dirent.c/SASI; 48 B larger than §6w's 59,223 B, the only-symbol difference being which aliases the unmodified test references); the two 5 s idle windows + ~13 preamble/body lines reach `return 0` well within a **30-emulated-second** budget.  **Bare-metal ONLY** — the DOS host has no live IR6 keyboard ring nor live 8253 for the idle countdown.  **FIRST-RUN PASS** on MAME, run-stable (golden byte-identical across two repeated MAME runs before capture, per [[victor-harness-deterministic]]), verified `tools/test-newlibc.sh keyboard_nonblock_test` → **[ok]** end-to-end (the harness rebuilds and diffs the live serial output against the golden, so [ok] IS the gate); the additive entry was confirmed non-disturbing by re-running `keyboard_raw_test` (raw-event sibling) and `stdin_test` (cooked-keyboard path) → both **[ok]**.  Next: the keyboard family is now broad — raw-event (§6w), nonblock-cooked (§6x), and cooked line/char (§6n/§6o/§6t stdin/scanf/read) are all gated; the natural remaining ungated phase-3 tests are `serial_loopback_test` (still needs NEW harness plumbing — an rs232a TX→RX loopback attach, distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem), `interrupt_test` (stays SKIPPED — §6v's `[90,110]` FAIL-window + raw iteration-count brittleness), `pic_test` (candidate but needs NEW bm_shim aliases AND `pic_enable_irq`/`pic_disable_irq`, which bm_pic.c does not yet expose — only `bm_pic_get_mask`/`set_mask`), and the display-only/`hlt`-loop tests (`minimal_irq_test`/`segment_test`/`simple_screen_test`/`memory_test`/`font*_test`) which are NOT bm_testhost-shaped and already covered by the hand-mirrored `bm_*` ports; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6x session notes (2026-06-13)

### ZERO new code — §6w's aliases already staged this test
- `keyboard_nonblock_test` includes only `<stdio.h>` + the upstream
  `keyboard.h`/`timer.h`, and calls `keyboard_getc_nonblock()` /
  `keyboard_hit()` + `timer_get_ticks()`/`timer_delay_ms()`.  ALL four
  symbols already resolve through `bm_shim.c`: the keyboard pair via the
  aliases §6w added (which §6w explicitly noted "also pre-stage
  keyboard_nonblock_test"), the timer pair via the §6u/§6v surface.
- So nothing was added but one battery entry + one golden — no
  compiler/qbe/emit/minic/build-script source touched → no emit audit,
  no MP byte-compare.

### NO keypost → both idle branches → fixed-text golden (like §6w)
- Test 1: `keyboard_getc_nonblock()` with no key pending returns `< 0` →
  `"OK: no key was pending."`
- Test 2: `wait_for_key()` polls `keyboard_hit()` in a 500-tick (5 s) idle
  window; with no key posted it times out → `"PASS: no key arrived during
  bounded idle check."` + `return 0`.
- Driving with NO keypost sidesteps the keypost-vs-poll race the keyboard
  tests were parked on (no keys → no race; idle branches deterministic),
  and both branches print ONLY fixed text — no tick values — so the golden
  is fully toolchain-stable (contrast §6v's timing-derived ticks).

### Nonblock-cooked layer, one above §6w's raw-event API
- §6w `keyboard_raw_test` → `keyboard_get_raw_event_nonblock()` (raw IR6
  event bytes).  §6x `keyboard_nonblock_test` → `keyboard_hit()` /
  `keyboard_getc_nonblock()` (cooked-byte nonblock), the layer the cooked
  line reader (§6n/§6o/§6t) sits on.  The keyboard family is now broad:
  raw-event, nonblock-cooked, and cooked line/char all gated.

### Model: SMALL, bare-metal only
- 59,271 B code, under the 64 KB `_TEXT` ceiling (portable stdio + lean
  timer/keyboard surface; no fat_write.c/dirent.c/SASI).  48 B larger than
  §6w's 59,223 B (different alias symbols referenced).
- Two 5 s idle windows + ~13 preamble/body lines → 30 s budget ample.
  Bare-metal ONLY: DOS has no live IR6 ring nor live 8253.

### Verification
- `tools/test-newlibc.sh keyboard_nonblock_test` → [ok] (FIRST-RUN PASS,
  golden byte-identical across two repeated MAME runs before capture).
- Additive entry confirmed non-disturbing: re-ran `keyboard_raw_test`
  (raw-event sibling) and `stdin_test` (cooked keyboard) → both [ok].

### Open tracks (carried)
- `serial_loopback_test`: needs a new rs232a TX→RX loopback attach in the
  harness (distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem).
- `interrupt_test`: stays SKIPPED (§6v FAIL-window + iteration-count
  brittleness).
- `pic_test`: candidate, but needs NEW bm_shim aliases AND
  `pic_enable_irq`/`pic_disable_irq` — which bm_pic.c does NOT expose
  (only `bm_pic_get_mask`/`set_mask`); more work than the alias surface.
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

Older session headers (§6w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
