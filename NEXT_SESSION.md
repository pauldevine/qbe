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

# Next session (§6u — continue Phase 6.  §6t [2026-06-13, this session] gated the UNMODIFIED upstream `read_test` BOTH DOS-hosted AND bare-metal — the raw `read(0,…)` keyboard-input layer, the third member of the §6n/§6o keyboard-input family after `stdin_test` (getchar/fgets) and `scanf_test` (scanf), with ZERO compiler/toolchain/build-script changes — **test-dos 295/295 → 296/296, battery 32/32 → 33/33**.  `read_test` exercises `read(STDIN_FILENO, &ch, 1)` (one byte) and `read(STDIN_FILENO, line, sizeof(line)-1)` (a cooked line) DIRECTLY — asserting the returned byte count, that the buffer stops at the newline, and that it contains no `\b` byte — coverage the getchar/fgets/scanf tests reach only transitively through the same `_read(0,…)` path (on DOS that path bottoms out at INT 21h AH=3Fh on handle 0; bare-metal it routes through `console_dev_read` → `bm_tty_read`).  **DOS-hosted** it runs through the §6n stdin-redirect mechanism (`< IN.TXT`, the run-dos-batch.sh 3rd manifest field via `stage_runtime_case`'s 4th arg; AH=3Fh on a redirected file reads RAW — no echo, no rubout — so the run is deterministic): fixture `minic/dos/tests/newlibc_read_test.stdin.txt` = `Ahello\n` (no Backspace byte, because the raw redirect performs no editing), golden `minic/dos/tests/newlibc_read_test.golden.txt` (read1='A' 0x41, read2="hello\n" 6 bytes → "PASS: read stopped at newline"), added to the §6n loop in `tools/test-dos.sh` (`for t in stdin_test scanf_test read_test`) and verified byte-exact through the full run-dos-batch path.  **Bare-metal** it runs through the §6o cooked `bm_tty` console (interrupt-driven keyboard on IR6): battery entry `read_test:35:Avx\b9k\nz::`, where `read(0,&ch,1)` consumes ONE keystroke `A` (count=1, no Enter), `read(0,line,39)` reads the cooked line `vx\b9k` — a REAL Backspace rubs out the `x` → `v9k\n` (4 bytes, no `\b` byte) — and the trailing `z` commits the final Return into the IR6 ring (the §6h/§6o flush rule: a `\n` at the very end of a keypost is not flushed).  Its golden `minic/dos/tests/read_test.golden.txt` ECHOES the typed input (`> A`, `> vx 9k` — the rubout sequence; vs the no-echo DOS golden) and carries the bm_testhost preamble (`pic+timer`/`tty+sti`/`vfs`) + `test returned 0` trailer.  It builds SMALL in BOTH hosts (DOS 51,117 B code; bare-metal 59,811 B, under the 64 KB single-`_TEXT` ceiling — portable stdio, no fat_write.c bulk), like the other two keyboard tests.  **FIRST-RUN PASS** on MAME (`tools/test-newlibc.sh read_test` → **[ok]**) and through the DOS gate (`newlibc small (read_test)` → **[ok]**).  The other 32 battery entries and 295 DOS entries are byte-unaffected (each harness change is one array entry), and with no compiler/qbe/emit/build-script source touched there is **no emit audit and no MP byte-compare**.  Next: the keyboard-input family is now complete (read_test/stdin_test/scanf_test cover read(0)/getchar/fgets/scanf over the cooked console); the remaining ungated phase-3 tests are driver/hardware (font_test/font_layout_test, keyboard_raw_test/keyboard_nonblock_test, serial_loopback_test, simple_interrupt_test/minimal_irq_test, segment_test/simple_screen_test/driver_test — mostly covered by the hand-mirrored bm_* ports, but candidates for §6p-style "run the upstream test ITSELF" runs through bm_testhost if a driver path needs a deterministic golden); or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6t session notes (2026-06-13)

### Nothing new needed — pure battery + DOS-gate plumbing (one entry each + three goldens)
- `read_test` is portable (only `<stdio.h>`/`<string.h>`/`<sys/types.h>`/
  `<unistd.h>`, no driver headers), so it builds small both hosts and
  resolves through `build-newlibc-test.sh` / `build-newlibc-baremetal.sh`
  unchanged.  It is the raw-`read(0,…)` member of the §6n/§6o keyboard
  family; both gate mechanisms already existed.
- DOS: added `read_test` to the §6n stdin-redirect loop in `test-dos.sh`
  (fixture `newlibc_read_test.stdin.txt` = `Ahello\n`, golden
  `newlibc_read_test.golden.txt`).  Golden captured via run-dos-exe with
  `DOS_STDIN=…` (CRLF-stripped) and verified byte-exact through the actual
  run-dos-batch path (full test-dos.sh → 296/296) before trusting it.
- Bare-metal: added `read_test:35:Avx\b9k\nz::` to `NEWLIBC_BM_TESTS` in
  `test-newlibc.sh`; golden `read_test.golden.txt` captured from a clean
  MAME run (echoes input, testhost preamble + `test returned 0`).

### What this test covers that the other two did not
- The raw POSIX `read(0, buf, n)` syscall layer DIRECTLY: `read(0,&ch,1)`
  returns exactly 1 (one keystroke, no Enter), `read(0,line,39)` returns
  the cooked line and stops at the newline.  stdin_test/scanf_test reach
  `_read(0,…)` only through the getchar/fgets/scanf wrappers; this asserts
  the syscall's byte count and edited-buffer (`\b`-free) result directly.

### DOS vs bare-metal goldens diverge (the §6n/§6o pattern, again)
- DOS redirect is RAW (AH=3Fh, no echo, no rubout) → input must omit the
  Backspace (`Ahello\n`), golden shows no echo.
- Cooked `bm_tty` echoes and edits → keypost CAN include a real Backspace
  (`Avx\b9k\nz` → "v9k\n"), golden echoes `> A` / `> vx 9k`.  Same split as
  stdin_test/scanf_test had between their §6n and §6o goldens.

### Model: SMALL both hosts (no medium)
- DOS 51,117 B code; bare-metal 59,811 B — under the 64 KB `_TEXT` ceiling.
  Portable stdio TU set (no fat_write.c, no dirent.c, no SASI), like the
  other two keyboard tests.

### Open tracks (carried)
- The §6n/§6o keyboard-input family is now complete (read(0)/getchar/fgets/
  scanf).  Remaining ungated phase-3 tests are driver/hardware (font,
  keyboard-raw/nonblock, serial-loopback, simple-interrupt/minimal-irq,
  segment/simple-screen/driver) — mostly covered by the bm_* ports, but
  any whose driver path lacks a deterministic golden is a §6p-style
  "run the upstream test ITSELF through bm_testhost" candidate.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.


---

Older session headers (§6s and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
