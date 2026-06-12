# Next session (§6g — continue Phase 6.  §6f [2026-06-12, this session] completed **step 4c: the bare-metal battery grew 5/5 → 9/9** — the four "near-free" newlibc phase-3 tests are ported and standing (`tools/test-newlibc.sh`): `memory_bm` (font-RAM 0000:0C00 + screen-RAM F000:0000 byte write/readback, distinct patterns to catch segment aliasing), `crtc_bm` (display bring-up verified through raw far-pointer VRAM word writes read back via the driver hook, plus RAW R14/R15 cursor readback — (12,34)→0x03/0xE2 — the only 6845 registers with readback), `pic_bm` (IMR get/set/restore on unused IR5 while the timer is LIVE on IR2, deterministic post-init mask 0xFB, plus the gating proof the original only implied: masking IR2 freezes ticks, unmasking resumes them; continuous ticks = the EOI check), and `interrupt_bm` (the battery's first CROSS-DRIVER stress: display init's 8 KB far font copy + 60 scrolling lines all under the live timer ISR — far MMIO/ES traffic racing the compiler-emitted ISR ABI — then delay-in-range and screen-intact checks).  Every port PASSED ON THE FIRST RUN — zero compiler changes again; the only driver delta is `bm_pic_get_mask`/`bm_pic_set_mask` (IMR readback was already there as a static).  One harness lesson: `interrupt_bm`'s scroll stress needs a 120-emulated-second budget (60 scrolls ≈ 90+ s on the 5 MHz 8088; the 40 s first try truncated mid-phase — slowness, not a hang, per the determinism rule).  `build-newlibc-baremetal.sh` gained a `bm_pic.h` include probe.  Gates: test-newlibc **9/9**, test-dos 289/289; NO toolchain change so no emit audit / MP byte-compare triggered.  Next: **step 4d** — the cooked console (bm_display putc + bm_keyboard getc behind a console layer) toward retiring libstub's DOS-only stdio; block/SASI driver port (MAME -scsi:0 harddisk) for bare-metal FAT; serial TX ISR if a consumer appears; run-dos-exe.sh stdin redirect for the 3 remaining DOS-hosted tests.)

## §6f session notes (2026-06-12)

### The four ports (minic/dos/newlibc/, all first-run PASS)
- memory_bm.c: byte patterns i / 0xFF-i to font RAM (0000:0C00) and
  screen RAM (F000:0000) — distinct patterns double as an aliasing
  check.  No display init needed; VRAM is plain RAM for byte access.
  Results over serial (the original reported on the display it had
  just scribbled over).
- crtc_bm.c: writes screen words through its OWN far pointer (not the
  driver putc path), reads back through bm_display_read_cell; asserts
  raw R14/R15 bytes for cursor (12,34) = 994 = 0x03E2 and for home.
  R0..R13 are write-only on a real 6845 — the original's register dump
  is unverifiable and was dropped, per the §6e display_bm precedent.
- pic_bm.c: new bm_pic_get_mask/bm_pic_set_mask (the read was already
  a static; OCW1 at E000:0001).  Deterministic IMR values printed:
  post-init+timer = 0xFB.  IR5 set/clear/restore with the timer live;
  then mask-IR2-freezes / unmask-resumes — proving the IMR gates
  delivery, which the original never tested.  wait_tick_change spin
  idiom copied from timer_bm (~6 ms/outer iteration vs ~8 ms tick).
- interrupt_bm.c: display init INSIDE live interrupts (8 KB far font
  copy + 16 CRTC writes with the ISR firing), 60 puts+newline lines
  (35+ full-VRAM scroll moves) racing the ISR, ticks-advanced checks
  bracketing the stress, delay(500ms) in [50..80], final screen-intact
  readback.  Every display op is far MMIO (ES loads), so this is the
  standing ES-safety/ISR-ABI stress the original interrupt_test was
  written to be.

### Harness facts
- interrupt_bm needs `120` emulated seconds in NEWLIBC_BM_TESTS: the
  scroll stress alone is ~90 s of 5 MHz 8088 time.  A truncated serial
  log mid-phase = budget too small (slowness), NOT a hang — rerun
  longer before debugging (determinism rule).
- build-newlibc-baremetal.sh now probes `bm_pic.h` directly (pic_bm
  includes it without bm_interrupts.h being the only pull).
- Battery totals: 9 tests, ~5 min wall (interrupt_bm dominates).

### Open tracks (new + carried)
- newlibc step 4d: cooked console (bm_display + bm_keyboard behind a
  getc/putc pair) → stdio toward retiring libstub; block/SASI driver
  port (MAME -scsi:0 harddisk) for bare-metal FAT; serial TX ISR when
  a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

# Next session (§6f — continue Phase 6.  §6e [2026-06-12, previous session] completed **step 4b: the display, keyboard, and serial-RX drivers run bare-metal on the Victor 9000 through compiler-emitted ISRs, and the newlibc bare-metal battery is a standing gate (`tools/test-newlibc.sh`, 5/5).**  Three new minic-dialect driver ports (`bm_display.c`+`bm_font_data.c`, `bm_keyboard.c`, `bm_serial.c`) and three new MAME-verified tests, each PASS on the FIRST run — the §6d ISR ABI carried them with zero compiler changes.  `display_bm`: font→0000:0C00, 6845 bring-up, glyph-pointer VRAM writes, scroll/tab/backspace — all self-verified from the machine (VRAM/font readback over serial; the 6845 only reads back R14/R15, so cursor is the only CRTC readback).  `keyboard_bm`: MAME natural-keyboard types "v9k" (harness `V9K_KEYPOST`), every keystroke travels VIA-CS2 shift register → KBINT/IR6 → compiler-emitted ISR → ring buffer → cooked ASCII — the port is interrupt-driven ONLY (MAME wires IR6 to the VIA2 IRQ, so each state-machine step re-edges IR6), with the timer ISR live concurrently.  `serial_bm`: a REAL 7201 channel-B RX ISR (newlibc's own was a stub) — IR1, WR1=0x18 int-on-all-RX, and a drain-until-RR0-empty loop because the edge-triggered 8259A never re-edges while the 7201 holds INT for an unread byte; harness `V9K_SERIAL_IN` attaches a byte file to a second null_modem on rs232b MID-RUN from Lua (attach-at-boot would stream the bytes before the program initializes; two bitbangers renumber the capture option to `-bitbanger1`).  **KEY step-4 question ANSWERED: extended-asm is NOT needed for the driver suite** — newlibc's display/keyboard inline asm was entirely ia16-gcc ES-workarounds (the interrupt ABI owns ES now) + a pushf/cli flags-save the single-producer/single-consumer ring design makes unnecessary; the CRTC asm is just volatile-far MMIO.  `bm_install_isr()` exported from bm_interrupts.c for driver TUs.  Gates: test-dos 289/289, test_omf_link all pass, victor pipeline 4/4, test-newlibc 5/5; NO toolchain change (new sources + harness scripts only) so no emit audit / MP byte-compare triggered.  Next: **step 4c** — sweep more newlibc phase-3 tests onto the bare-metal battery (interrupt_test/pic_test/crtc_test/memory_test should be near-free now); a cooked console (display output + keyboard input) toward retiring libstub's DOS-only stdio; block/SASI driver port for bare-metal FAT; serial TX ISR if a consumer appears.)

## §6e session notes (2026-06-12)

### Driver ports (minic/dos/newlibc/)
- bm_display.c + bm_font_data.c (8 KB font table, verbatim from newlibc):
  font MUST load to 0000:0C00 before the CRTC shows anything (no char ROM);
  VRAM words at F000:0000 are (attr<<8)|glyph_ptr with glyph_ptr=char+0x60;
  VIA brightness (E800:40=0x54, E800:42=0xFF) or the screen stays dark;
  the original's CRTC push-es/mov-es asm is exactly a minic volatile-far
  store.  Self-check hooks (bm_display_read_cell/read_crtc/screen_word)
  let display_bm verify everything over serial — no host screen dump.
- bm_keyboard.c: BIOS state machine (SHIFTING→STOP_LOW→STOP_HIGH),
  MAME-validated ASCII map, S88-Return compat, Shift/RPT/Alt — all kept.
  Interrupt-driven ONLY: the ISR is the sole ring-buffer producer and the
  consumer pops via one-byte indexes, so the original's pushf/cli
  flags-save (the last inline-asm holdout) is structurally unnecessary.
- bm_serial.c: 7201 channel B mirrors the channel-A console bring-up
  (VIA2 PA bit1 internal clock, 8253 counter 1 — counter 1 is Serial B —
  mode 2 divisor 8, reset+WR4/WR3/WR5) plus WR1=0x18.  ISR drains all
  pending bytes before the specific EOI (0x61) — edge-triggered PIC +
  level-holding 7201 INT means a left-behind byte kills all future IRQs.
- bm_interrupts.c: install() → exported bm_install_isr(int_num, fn)
  (model-agnostic qbe_get_cs idiom) so driver TUs install their own ISRs.

### Harness (tools/)
- run-victor-baremetal.sh: V9K_KEYPOST + V9K_KEYPOST_DELAY (MAME
  natkeyboard:post — the newlibc-validated pattern; works headless),
  V9K_SERIAL_IN + V9K_SERIAL_IN_DELAY (second null_modem on rs232b,
  byte file attached mid-run via Lua manager.machine.images img:load —
  null_modem streams an attached file as RX data, so attaching at boot
  would lose everything during program init).  With rs232b present the
  bitbanger media options renumber: capture binds to -bitbanger1.
- build-newlibc-baremetal.sh: per-header driver TU selection
  (bm_display/bm_keyboard/bm_serial) + dedup (keyboard and serial both
  pull bm_interrupts+bm_pic).
- tools/test-newlibc.sh: the standing battery — hello_bm, timer_bm,
  display_bm, keyboard_bm, serial_bm, each golden-diffed
  (minic/dos/tests/<name>.golden.txt), skip-77 when MAME/newlibc absent.

### Victor/MAME facts worth keeping
- 6845 CRTC registers are write-only except R14/R15 (cursor) — readback
  tests must not assert config registers.
- MAME victor9k drives IR6 from the VIA2 IRQ line (victor9k.cpp:561), so
  the keyboard handshake is fully interrupt-driven — every SR/CB1 step
  re-raises KBINT.
- natkeyboard:post() is deterministic under -nothrottle and types
  shifted chars itself; keyboard_bm's "v9k" arrives byte-exact.
- 7201 INT stays asserted while an RX byte is pending; with the 8259A in
  edge mode the ISR MUST drain to RR0-empty or IR1 never fires again.

### Open tracks (new + carried)
- newlibc step 4c: port more phase-3 tests bare-metal (interrupt_test,
  pic_test, crtc_test, memory_test look near-free); cooked console
  (bm_display + bm_keyboard behind a getc/putc pair) → stdio toward
  retiring libstub; block/SASI driver port (MAME -scsi:0 harddisk) for
  bare-metal FAT; serial TX ISR when a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
