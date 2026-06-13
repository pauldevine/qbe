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

# Next session (§6v — continue Phase 6.  §6u [2026-06-13, this session] gated the UNMODIFIED upstream `driver_test` **BARE-METAL through bm_testhost + the bm_stdio/timer/PIC stack, with ZERO compiler/toolchain/build-script changes — battery 33/33 → 34/34** (test-dos UNCHANGED at 296/296 — this is a bare-metal-only gate, like §6q).  This is the **first DRIVER-layer upstream test** gated through bm_testhost: the §6j–§6t family ran upstream tests over the *portable* surface (stdio/vfs/fat/block, implemented by newlibc's own portable TUs that bm_stdio links), and the drivers were covered only by the hand-mirrored `bm_*` ports (memory_bm, crtc_bm, pic_bm, timer_bm, …); `driver_test` is the upstream test ITSELF exercising the driver surface (the §6p philosophy applied to the hardware drivers).  It validates the Phase-1 hardware-fix story against the **LIVE** bm_timer/8259: Test 1 measures a real 100 ms delay via `timer_delay_ms` and asserts ~10 ticks — **deterministic 10 in MAME** (both the driver's `timer_get_ticks()` and `delay_ms()`'s internal start read the same ISR-driven `tick_counter`, taken on consecutive instructions, so no tick falls between them; verified stable across the 30/60/90 s runs); Test 2 asserts `timer_get_frequency()==100`; Test 3 prints the serial-counter-assignment text (PASS = output arrived); Test 4 reads the **live 8259 IMR** through the `PIC_GET_MASK()` MMIO macro (a direct `volatile uint8_t __far` read of E000:0001 → **0xBB**, IR2 bit clear → timer unmasked, also stable across runs); Test 5 prints the IR2-vector-0x42 text.  All five tests print fixed text + PASS and `main` returns 0 (`bm_testhost: test returned 0`).  The whole driver surface resolves through **`bm_shim.c`** (`timer_get_ticks`/`timer_delay_ms` → `bm_timer_*`; `timer_get_frequency` → literal `100UL`) and through the upstream `v9k_hw.h` PIC/timer macros (`PIC_GET_MASK()` → `HW_READ_BYTE` direct MMIO, `TIMER_8253_OFFSET` → constant) — **nothing new to link**, so like §6q the only changes are **one battery entry (`driver_test:90:::`) + one bare-metal-captured golden** (`minic/dos/tests/driver_test.golden.txt`, 71 lines, testhost `pic+timer`/`tty+sti`/`vfs` preamble + `test returned 0` trailer).  **Bare-metal ONLY** — the DOS host has no live 8253/8259, so the measured-delay and live-IMR lines have no DOS golden to diff against (the §6q SASI-probe pattern, not the §6r/§6s RAM-disk both-hosts pattern).  Builds **SMALL** (59,485 B code, under the 64 KB single-`_TEXT` ceiling — portable stdio + the lean timer/PIC surface, no fat_write.c/dirent.c/SASI).  The 71 output lines at the §6f display-scroll rate (each printf mirrors to display+serial through bm_tty) need a **90-emulated-second** budget (60 s truncated mid-Test-5, 30 s mid-Test-3 — slowness, not a hang).  **FIRST-RUN PASS** on MAME, verified `tools/test-newlibc.sh driver_test` → **[ok]** end-to-end (the harness diffs the live serial output against the golden, so [ok] IS the gate).  The other 33 battery entries are byte-unaffected (the change is one independent array entry) and with no compiler/qbe/emit/build-script source touched there is **no emit audit and no MP byte-compare**.  Next: the remaining ungated phase-3 driver tests split into (a) clean printf+return shapes that could follow this §6p-style path but need input/interrupt determinism worked out — `interrupt_test`/`simple_interrupt_test` (count timer interrupts; check the printed count is stable in MAME first), `keyboard_raw_test`/`keyboard_nonblock_test` (need a V9K_KEYPOST burst + possibly nonblock-timing care); and (b) display-only/`hlt`-loop tests that are NOT bm_testhost-shaped (no serial output, no clean return) — `memory_test`/`segment_test`/`simple_screen_test`/`font_test`/`font_ram_test`/`font_layout_test`/`minimal_irq_test` — already covered by the hand-mirrored `bm_*` ports; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6u session notes (2026-06-13)

### Nothing new needed — one battery entry + one bare-metal golden (the §6q pattern)
- `driver_test` includes only `<stdio.h>`/`<stdint.h>` + the upstream
  `timer.h`/`console.h`/`v9k_hw.h` headers; its driver calls resolve through
  `bm_shim.c` (`timer_*` → `bm_timer_*`, `timer_get_frequency` → `100UL`) and
  the `v9k_hw.h` MMIO macros (`PIC_GET_MASK()` direct far read of E000:0001,
  `TIMER_8253_OFFSET` constant).  `build-newlibc-baremetal.sh` test-host mode
  resolves it unchanged — no probe widen (contrast §6p's `sasi.h` widen), no
  build-script or compiler change.
- Bare-metal golden captured from a clean 90 s MAME run; entry
  `driver_test:90:::` added to `NEWLIBC_BM_TESTS` in `test-newlibc.sh`.

### First DRIVER-layer upstream test (vs portable surface / hand-mirror ports)
- §6j–§6t ran upstream tests over the portable stdio/vfs/fat/block surface
  (newlibc's own TUs, linked by bm_stdio).  The drivers were covered only by
  the hand-written `bm_*` minic ports (memory_bm/crtc_bm/pic_bm/timer_bm/…).
  `driver_test` is the upstream test ITSELF over the driver surface — it
  asserts the Phase-1 fixes (timer Ch2 @ 100 Hz, IR2 unmasked, vector 0x42)
  against the LIVE bm_timer/8259, not a hand-mirror's re-statement of them.

### Determinism of the two hardware-read lines (verified, not assumed)
- "Measured 100ms delay: 10 ticks" — `timer_delay_ms(100)` waits `target =
  (100*100)/1000 = 10` ticks on the same ISR-driven `tick_counter` the
  driver reads before/after; the two enclosing `timer_get_ticks()` calls are
  consecutive instructions so no tick falls between → measured exactly 10.
  Stable across the 30/60/90 s runs.
- "Current PIC mask: 0xBB" — a direct `volatile __far` read of the live 8259
  IMR at E000:0001 after `bm_pic_init`+`bm_timer_init`; bit 2 (IR2) clear →
  "unmasked".  Stable across runs.  MAME emulation is cycle-deterministic
  (the [[victor-harness-deterministic]] rule), so both lines are golden-safe.

### Bare-metal ONLY (no DOS half), SMALL model
- The DOS host has no live 8253/8259, so the measured-delay and live-IMR
  lines have no DOS golden — bare-metal-only, like §6q `sasi_sector_test`
  (not the §6r/§6s RAM-disk both-hosts pattern).
- SMALL: 59,485 B code, under the 64 KB `_TEXT` ceiling (portable stdio +
  lean timer/PIC surface; no fat_write.c/dirent.c/SASI bulk).
- 71 output lines at the §6f display-scroll rate → 90 s budget (60 s cut
  mid-Test-5, 30 s mid-Test-3; slowness, not a hang).

### Open tracks (carried)
- Remaining ungated phase-3 driver tests: (a) printf+return shapes that
  could follow this §6p-style path but need input/interrupt determinism
  settled first — `interrupt_test`/`simple_interrupt_test` (printed timer-
  interrupt count — confirm it is stable in MAME before trusting a golden),
  `keyboard_raw_test`/`keyboard_nonblock_test` (need a V9K_KEYPOST burst, and
  nonblock has timing care); (b) display-only/`hlt`-loop tests that are NOT
  bm_testhost-shaped (no serial, no clean return) — memory/segment/
  simple_screen/font/font_ram/font_layout/minimal_irq — already covered by
  the hand-mirrored `bm_*` ports.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.


---

Older session headers (§6t and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
