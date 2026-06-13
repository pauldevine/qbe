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

# Next session (§6x — continue Phase 6.  §6w [2026-06-13, this session] gated the UNMODIFIED upstream `keyboard_raw_test` **BARE-METAL through bm_testhost + the bm_stdio/bm_keyboard stack — battery 35/35 → 36/36** (test-dos UNCHANGED at 296/296 — a bare-metal-only gate, like §6q/§6u/§6v).  This is the **first gate of the RAW (uncooked) keyboard event API + its nonblock semantics**: the §6e `keyboard_bm` and the §6n/§6o/§6t keyboard-input family (`stdin_test`/`scanf_test`/`read_test`) all exercise the COOKED `bm_tty` path (rubout, CR→LF, echo, blocking line reads); `keyboard_raw_test` calls `keyboard_get_raw_event_nonblock()` DIRECTLY against the interrupt-driven IR6 event ring, the layer the cooked reader sits on top of.  The test loops polling that nonblock API, bounded by a 500-tick (5-emulated-second) idle window driven off `timer_get_ticks()`; it is run **with NO keypost**, so the raw IR6 ring stays empty (every poll returns `< 0`), the idle window elapses, and it takes its `count == 0` branch — printing `PASS: no raw keyboard events arrived during idle check.` and `return 0`.  **Driving it with no keypost is the key design choice**: it sidesteps the keypost-vs-poll timing race the keyboard tests were parked on since §6u/§6v (with no keys posted there is simply no race — the idle branch is taken deterministically), AND the idle branch prints **ONLY fixed text — no tick values at all** — so its golden (`minic/dos/tests/keyboard_raw_test.golden.txt`, 17 lines: bm_testhost `pic+timer`/`tty+sti`/`vfs` preamble + the 11-line test body + `test returned 0` trailer) is **fully toolchain-stable**, strictly better than §6v `simple_interrupt_test`'s timing-derived-tick golden that needs re-capture on any bm_tty/printf codegen change.  **One change, build-glue only**: `bm_shim.c` gained keyboard surface aliases (`keyboard_get_raw_event_nonblock` / `keyboard_hit` / `keyboard_getc` / `keyboard_getc_nonblock` → the corresponding `bm_keyboard_*`, mirroring the existing `timer_*`/`display_*` alias surfaces in the same file) so the UNMODIFIED upstream test links its unprefixed names — bm_keyboard.c is already linked into every bm_stdio build (bm_tty's cooked reader drains the same ring) and `bm_tty_init()` (in the testhost preamble) already inits the IR6 ISR, so nothing new is *linked*, only the alias wrappers added.  This is a newlibc bare-metal support TU (NOT compiler/qbe/emit/minic, and MP does not link bm_shim.c), so per the house rules there is **no emit audit and no MP byte-compare**.  Builds **SMALL** (59,223 B code, under the 64 KB single-`_TEXT` ceiling — portable stdio + lean timer/keyboard surface, no fat_write.c/dirent.c/SASI); the 5 s idle window + 11 preamble lines reach `return 0` well within a **30-emulated-second** budget (verified at both 40 s and 30 s).  **Bare-metal ONLY** — the DOS host has no live IR6 keyboard ring nor live 8253 for the idle countdown.  **FIRST-RUN PASS** on MAME, verified `tools/test-newlibc.sh keyboard_raw_test` → **[ok]** end-to-end (the harness rebuilds and diffs the live serial output against the golden, so [ok] IS the gate); and the additive aliases were confirmed non-disturbing by re-running `stdin_test` (cooked-keyboard path), `stdio_bm`, and `snprintf_test` → all **[ok]**.  Next: `keyboard_nonblock_test` is the natural follow-up — the four keyboard aliases added this session ALREADY stage its `keyboard_hit`/`keyboard_getc_nonblock` symbols, and it has the same `wait_for_key` 500-tick idle-timeout structure, so a NO-keypost run should deterministically take its "no key" branch (confirm the timeout branch prints fixed text, no tick values, before trusting a golden); `serial_loopback_test` still needs new harness plumbing (an rs232a TX→RX loopback attach, distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem); `interrupt_test` stays SKIPPED (the §6v FAIL-window + iteration-count brittleness); `pic_test` is a candidate but would need NEW bm_shim aliases AND `pic_enable_irq`/`pic_disable_irq` (which bm_pic.c does not yet expose — only `bm_pic_get_mask`/`set_mask`); the display-only/`hlt`-loop tests (`minimal_irq_test`/`segment_test`/`simple_screen_test`/`memory_test`/`font*_test`) are NOT bm_testhost-shaped and already covered by the hand-mirrored `bm_*` ports; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6w session notes (2026-06-13)

### One change: bm_shim.c keyboard aliases (build glue, not the compiler)
- `keyboard_raw_test` includes only `<stdio.h>`/`<stdint.h>` + the upstream
  `keyboard.h`/`timer.h`; `timer_get_ticks`/`timer_delay_ms` already resolve
  through `bm_shim.c` → `bm_timer_*` (the §6u/§6v surface), but
  `keyboard_get_raw_event_nonblock` did not.  Added FOUR keyboard aliases to
  `bm_shim.c` (`keyboard_get_raw_event_nonblock`/`keyboard_hit`/
  `keyboard_getc`/`keyboard_getc_nonblock` → `bm_keyboard_*`), mirroring the
  `timer_*`/`display_*` surfaces already in that file.  bm_keyboard.c is
  already linked into every bm_stdio build (bm_tty's cooked reader drains the
  same IR6 ring) and `bm_tty_init()` inits the ISR in the testhost preamble —
  so nothing NEW links, only the wrapper symbols.
- This is a newlibc bare-metal support TU, not compiler/qbe/emit/minic, and
  MP does not link bm_shim.c → no emit audit, no MP byte-compare.

### NO keypost → idle branch → fixed-text golden (better than §6v)
- The test polls `keyboard_get_raw_event_nonblock()` in a loop bounded by a
  500-tick idle window.  Run with NO keypost, the raw ring stays empty
  (returns `< 0` every poll); after the idle window it takes the `count == 0`
  branch and prints `PASS: no raw keyboard events arrived during idle check.`
- Driving with no keypost SIDESTEPS the keypost-vs-poll race the keyboard
  tests were parked on (no keys → no race; idle branch deterministic), and the
  idle branch prints ONLY fixed text — no tick values — so the golden is
  fully toolchain-stable (contrast §6v's timing-derived ticks that need
  re-capture on a bm_tty/printf codegen change).

### First RAW (uncooked) keyboard-event coverage
- §6e `keyboard_bm` + §6n/§6o/§6t (`stdin_test`/`scanf_test`/`read_test`) all
  test the COOKED `bm_tty` path.  `keyboard_raw_test` calls the nonblock raw
  IR6 event API directly — the layer beneath the cooking — and proves it
  returns cleanly ("no event") with no input.

### Model: SMALL, bare-metal only
- 59,223 B code, under the 64 KB `_TEXT` ceiling (portable stdio + lean
  timer/keyboard surface; no fat_write.c/dirent.c/SASI bulk).
- 5 s idle window + 11 preamble lines → 30 s budget (verified at 40 s and
  30 s; reaches `return 0` well within it).  Bare-metal ONLY: DOS has no live
  IR6 ring nor live 8253.

### Verification
- `tools/test-newlibc.sh keyboard_raw_test` → [ok] (FIRST-RUN PASS).
- Additive aliases confirmed non-disturbing: re-ran `stdin_test` (cooked
  keyboard), `stdio_bm`, `snprintf_test` → all [ok].

### Open tracks (carried)
- `keyboard_nonblock_test`: natural follow-up — the four aliases added this
  session ALREADY stage its `keyboard_hit`/`keyboard_getc_nonblock`; same
  `wait_for_key` 500-tick idle-timeout shape, so a NO-keypost run should
  deterministically take its "no key" branch (confirm that branch prints
  fixed text, no tick values, before trusting a golden).
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

Older session headers (§6v and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
