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

# Next session (§6w — continue Phase 6.  §6v [2026-06-13, this session] gated the UNMODIFIED upstream `simple_interrupt_test` **BARE-METAL through bm_testhost + the bm_stdio/bm_timer stack, with ZERO compiler/toolchain/build-script changes — battery 34/34 → 35/35** (test-dos UNCHANGED at 296/296 — a bare-metal-only gate, like §6q `sasi_sector_test` and §6u `driver_test`).  This is the minimal **continuous-timer-interrupt** test (the 33-line upstream TU: read a start tick count, then 5× `timer_delay_ms(1000)` each printing the elapsed ticks, then an UNCONDITIONAL `PASS: Interrupts working!` and `return 0`), and it COMPLEMENTS §6u's `driver_test` — where §6u measured a SINGLE 100 ms `timer_delay_ms` and asserted ~10 ticks, this proves **5 seconds of CONTINUOUS timer interrupts** keep the ISR-driven `tick_counter` incrementing monotonically under the full bm_stdio stack, the longest sustained-interrupt run in the battery.  Its driver calls resolve entirely through **`bm_shim.c`** (`timer_get_ticks`/`timer_delay_ms` → `bm_timer_get_ticks`/`bm_timer_delay_ms`) — **nothing new to link**, so like §6q/§6u the only changes are **one battery entry (`simple_interrupt_test:30:::`) + one bare-metal-captured golden** (`minic/dos/tests/simple_interrupt_test.golden.txt`, 17 lines: testhost `pic+timer`/`tty+sti`/`vfs` preamble + the test body + `test returned 0` trailer).  **`simple_interrupt_test` was chosen over the larger `interrupt_test`, which is UNSUITABLE for a golden**: `interrupt_test`'s Test 1 reads `start_ticks` BEFORE four slow display-mirrored `printf`s, so the accumulated display-scroll ticks push `elapsed` past its `[90,110]` PASS window → it would print **`FAIL: Timer tick count incorrect!`** (semantically wrong to gate), and its Test 3 embeds a raw busy-loop iteration count; `simple_interrupt_test` has no pass/fail threshold and no iteration count, only monotonic elapsed ticks + an unconditional PASS.  **IMPORTANT note on the golden's tick values** (`Start: 111`, then elapsed `155 / 316 / 476 / 637 / 797`): MAME models the Victor channel-2 input clock **FASTER than the nominal 100 Hz** (the upstream `interrupt_test.c` comment explicitly warns of this), and the slow display-mirrored `printf` between each `timer_delay_ms(1000)` (target = 100 ticks) accumulates ~61 extra ISR ticks, so elapsed grows ~161/iteration rather than the nominal 100 — these numbers are **DISPLAY-SCROLL-TIMING-derived, not wall-clock**.  They are **perfectly RUN-STABLE** (MAME is cycle-deterministic per [[victor-harness-deterministic]] — verified byte-identical across three repeated runs before capture), so the gate passes repeatedly; but unlike §6u's threshold-robust `10 ticks`/`0xBB`, they WILL SHIFT if a future toolchain change alters the bm_tty/`printf` codegen timing → **re-capture the golden then** (the PASS verdict itself is unconditional and toolchain-independent, so the test never falsely passes — a shift produces a loud diff, not a silent wrong result).  Bare-metal ONLY (the DOS host has no live 8253 to drive the ISR).  Builds **SMALL** (58,723 B code, under the 64 KB single-`_TEXT` ceiling — portable stdio + lean timer surface, no fat_write.c/dirent.c/SASI); the 17 output lines + 5 emulated seconds of timer delays fit a **30-emulated-second** budget (the test reaches `return 0` well within it).  **FIRST-RUN PASS** on MAME, verified `tools/test-newlibc.sh simple_interrupt_test` → **[ok]** end-to-end (the harness rebuilds and diffs the live serial output against the golden, so [ok] IS the gate).  The other 34 battery entries are byte-unaffected (the change is one independent array entry), and with no compiler/qbe/emit/build-script source touched there is **no emit audit and no MP byte-compare**.  Next: the remaining ungated phase-3 driver tests are getting thinner — `interrupt_test` is deliberately SKIPPED (the FAIL-window + iteration-count brittleness above); the keyboard pair (`keyboard_raw_test`/`keyboard_nonblock_test`) could follow this §6p-style path with a `V9K_KEYPOST` burst but both have idle-timeout branches whose taken-path depends on keypost-vs-poll timing (settle that determinism first); `serial_loopback_test` needs new harness plumbing (an rs232a TX→RX loopback attach, distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem); `minimal_irq_test`/`segment_test`/`simple_screen_test`/`memory_test`/`font*_test` are display-only/`hlt`-loop shapes NOT bm_testhost-shaped (no serial, no clean return) and already covered by the hand-mirrored `bm_*` ports; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6v session notes (2026-06-13)

### Nothing new needed — one battery entry + one bare-metal golden (the §6q/§6u pattern)
- `simple_interrupt_test` includes only `<stdio.h>`/`<stdint.h>` + the
  upstream `drivers/timer.h`; its two driver calls (`timer_get_ticks`,
  `timer_delay_ms`) resolve through `bm_shim.c` → `bm_timer_*`.
  `build-newlibc-baremetal.sh` test-host mode resolves it unchanged — no
  probe widen, no build-script or compiler change.
- Bare-metal golden captured from a clean 30 s MAME run; entry
  `simple_interrupt_test:30:::` added to `NEWLIBC_BM_TESTS` in
  `test-newlibc.sh`.  Bare-metal ONLY (no DOS half — no live 8253).

### Complements §6u driver_test (single delay → continuous interrupts)
- §6u `driver_test` measured ONE 100 ms delay (~10 ticks, robustly
  deterministic because the two `timer_get_ticks()` reads are consecutive).
  `simple_interrupt_test` runs 5× `timer_delay_ms(1000)` back-to-back —
  the longest sustained live-interrupt run in the battery — proving the
  ISR `tick_counter` increments monotonically across 5 emulated seconds
  under the full bm_stdio/bm_timer/8259 stack.

### Why interrupt_test was REJECTED (read this before trying to gate it)
- `interrupt_test` reads `start_ticks` BEFORE four slow display-mirrored
  `printf`s, then does ONE `timer_delay_ms(1000)` (target 100 ticks) and
  checks `elapsed` against `[90,110]`.  Because the pre-delay printfs
  accumulate display-scroll ticks, `elapsed` exceeds 110 → the test prints
  `FAIL: Timer tick count incorrect!` (a gate on a FAIL line is wrong).
  Its Test 3 also prints a raw busy-loop iteration count (pure timing).
  `simple_interrupt_test` has neither a threshold nor an iteration count.

### The golden's tick values are timing-derived (run-stable, toolchain-fragile)
- `Start: 111`, elapsed `155 / 316 / 476 / 637 / 797`.  MAME's channel-2
  clock runs faster than the nominal 100 Hz (upstream interrupt_test.c
  warns of this) AND the display-mirrored printf between each delay adds
  ~61 ticks, so elapsed grows ~161/iteration, not the nominal 100.
- RUN-STABLE: byte-identical across three repeated MAME runs (cycle-
  deterministic, [[victor-harness-deterministic]]) — the gate passes
  repeatedly.  But these numbers WILL SHIFT if bm_tty/printf codegen
  timing changes → re-capture the golden after such a toolchain change.
  The PASS verdict is unconditional, so a shift is a LOUD diff, never a
  silent wrong pass.

### Model: SMALL, bare-metal only
- 58,723 B code, under the 64 KB `_TEXT` ceiling (portable stdio + lean
  timer surface; no fat_write.c/dirent.c/SASI bulk).
- 17 output lines + 5 emulated seconds of timer delays → 30 s budget
  (reaches `return 0` well within it).

### Open tracks (carried)
- Remaining ungated phase-3 driver tests: `interrupt_test` SKIPPED (FAIL
  window + iteration count, above); `keyboard_raw_test`/
  `keyboard_nonblock_test` (need a V9K_KEYPOST burst AND idle-timeout
  branch-determinism settled); `serial_loopback_test` (needs a new rs232a
  TX→RX loopback attach in the harness); display-only/`hlt`-loop tests
  (`minimal_irq_test`/`segment_test`/`simple_screen_test`/`memory_test`/
  `font*_test`) are NOT bm_testhost-shaped and already covered by the
  hand-mirrored `bm_*` ports.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6u and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
