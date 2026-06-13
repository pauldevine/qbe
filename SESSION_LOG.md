# SESSION_LOG.md — archived `NEXT_SESSION.md` headers

Rolling archive of prior **"Next session"** headers moved out of
`NEXT_SESSION.md` to keep that file focused on the current plan plus the
immediately-preceding session.  Newest archived header first; each block is the
verbatim header as it was written at the time (point-in-time — file:line and
status claims may be stale, verify against current code).


# Next session (§6j — continue Phase 6.  §6i [2026-06-12, this session] completed **step 4f: bare-metal disk I/O — the minic-built SASI/Xebec driver reads (and writes) real sectors, and the unmodified newlibc FAT/VFS stack mounts a Victor volume from them.**  `minic/dos/newlibc/bm_sasi.c/h` is the minic-dialect port of newlibc's `drivers/sasi.c` (manual polled byte-transfer path, READ(6) + opt-in WRITE(6), full diagnostics struct) registering with the **unmodified** `drivers/block.c`; the TWO upstream inline-asm constructs were dropped, not translated — `SAVE_ES/RESTORE_ES` (ia16-gcc ES damage control: minic far accesses materialize their segment per access and the §6d ISR ABI restores ES on every iret) and the pushf/cli critical sections (the SASI handshake is REQ-driven — the controller holds REQ until serviced — so a live ISR only delays a poll loop, and the timeout budgets are bus-read loop counts orders of magnitude above ISR latency; the §6e "the asm was working around the other toolchain" finding now extends to the disk driver).  New battery test `sasi_bm` (121,904-byte image): full stdio_bm bring-up (timer + tty ISRs LIVE through every transfer — deliberately the honest configuration), then controller init (reset, TEST UNIT READY, Xebec RAM + CTRL tests, REQUEST SENSE), geometry 59058×512, LBA 0 Victor label read **byte-exact vs a host `xxd` of the image** (`02 00 01 00 "tandon_703_mame"`), repeat-uncached-read checksum match (0x8DDD), `vfs_mount_victor_fat("/fat", dev, 0)` — the **first minic exercise of the Victor-label mount path** (the DOS-hosted gate only ever ran `vfs_mount_fat` over ramdisks) — `stat` CONFIG.SYS = 220 bytes, `open`/`read` prefix matches the known image, `fopen`/`fgets` first line `"buffers = 15"` through the FILE layer, **WRITE(6) pattern round-trip @ LBA 59057 verified**, and CONFIG.SYS re-read intact after the write.  **FIRST-RUN PASS, zero compiler changes** (fifth driver/stdio session in a row riding the §6d ISR ABI).  Harness: `run-victor-baremetal.sh` gained `V9K_HARD_DISK` (image copied to a scratch file run-victor-sasi.sh-style — the base image never mutates, so WRITE(6) tests are safe; missing image → skip 77); `test-newlibc.sh` entries grew a fifth `:<disk>` field (`hd` = `$V9K_HARD_DISK_IMAGE`, default `~/projects/mame/victor_30mb.img` — the stable upstream-validated known image; victor_python.img is a moving target, NOT used); `build-newlibc-baremetal.sh` gained the `bm_sasi.h` probe (pulls bm_sasi.c + drivers/block.c).  Gates: test-newlibc **12/12**, test-dos **289/289**, test_omf_link all pass; NO toolchain change so no emit audit / MP byte-compare triggered.  Next: re-run the DOS-hosted newlibc tests bare-metal through bm_stdio — the FAT ones can now run against the REAL disk instead of ramdisks (snprintf/stdio_route are near-free starters); the upstream FAT-WRITE path (`vfs_mount_victor_fat_rw` + `fat_write.c` dispatch install) now has real hardware under it when wanted; run-dos-exe.sh stdin redirect for the 3 remaining DOS-hosted tests; scanf-over-cooked-tty when a consumer appears.)

## §6i session notes (2026-06-12)

### The port (bm_sasi.c — what changed vs upstream and why)
- Function/macro surface is name-for-name upstream (`sasi_register`,
  `SASI_*`, `sasi_device_t`) so upstream SASI tests port unchanged; only
  the file name marks it as the bare-metal TU (no DOS-hosted SASI
  counterpart exists to collide with).
- SAVE_ES/RESTORE_ES: dropped.  minic far MMIO loads its segment per
  access; nothing persists in ES across statements, and the §6d ISR ABI
  saves/restores ES in every compiler-emitted ISR.
- pushf/cli critical sections: dropped.  REQ-driven handshake = the
  controller waits for us, never the reverse; sasi_bm runs every
  transfer with the timer AND keyboard ISRs live as proof.
- `sasi_delay` keeps its `volatile uint16_t` induction variable (loop
  survives optimization without the upstream `asm("" ::: "memory")`).
- block.c, fat.c, vfs.c: compiled UNMODIFIED — the §6b portable-subset
  set plus the never-before-exercised `vfs_mount_victor_fat` Victor
  disk-label path (label at LBA 0, virtual volume regions).

### sasi_bm test facts
- Disk: `~/projects/mame/victor_30mb.img` — the newlibc-validated known
  MAME Victor/Tandon image (label "tandon_703_mame", one 59058-sector
  region, CONFIG.SYS 220 bytes = "buffers = 15\r\nbreak = on\r\n...").
  Deterministic and stable; do NOT swap in victor_python.img (it gets
  rebuilt with new python drops).
- LBA 0 expectation was locked against a host `xxd` of the image before
  the first MAME run — the on-target hex dump matched byte-for-byte.
- WRITE(6) scratch LBA 59057 = last labeled sector, inside both the
  label's region and the image; harness scratch-copy makes it safe.
- 90-second budget is comfortable (run finishes well inside; ~37 output
  lines ≈ 12 scrolled lines is the main 8088 cost, per the §6f scroll
  lesson).
- `block_init(dev)` on a missing/failed controller would print FAIL and
  the golden diff catches it; sasi_refresh_sense + the diagnostics
  struct survive in the port for future bring-up debugging.

### Harness facts
- `V9K_HARD_DISK` attach is at MAME launch (unlike V9K_SERIAL_IN's
  mid-run Lua attach) — the program polls the controller when ready, so
  there is nothing to lose during init.
- Battery entry format is now `<name>:<secs>:<keypost>:<serial>:<disk>`;
  the empty-field padding on existing entries was the whole migration.
- `${DISK_ARGS[@]+"${DISK_ARGS[@]}"}` (not a bare expansion) keeps
  macOS bash 3.2 `set -u` happy when no disk is attached.

### Open tracks (new + carried)
- Re-run DOS-hosted newlibc tests bare-metal through bm_stdio: snprintf,
  stdio_route (near-free); the six FAT tests against the REAL SASI disk
  (replace their ramdisk fixtures or mount /fat alongside).
- Upstream FAT WRITE: `vfs_mount_victor_fat_rw` + fat_write.c's
  runtime-installed dispatch table — bm_sasi WRITE(6) is now proven
  hardware under it; port when a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted tests).
- scanf-over-cooked-tty when a consumer appears; serial TX ISR when a
  consumer appears; newlibc-under-far-models stdio story.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

- For the **current** plan, see [`NEXT_SESSION.md`](./NEXT_SESSION.md).
- For **durable** facts, see the `memory/` files referenced as `[[...]]`.

---

# Next session (§6h — continue Phase 6.  §6g [2026-06-12, this session] completed **step 4d: the cooked console — the Victor is now its own terminal.**  `bm_tty.c/h` (minic/dos/newlibc/) is the minic port of newlibc's `console_dev_read`/`console_dev_write` pair: every output byte mirrors to BOTH the bm_display screen and the serial console (so a headless harness sees what the screen shows), and cooked input comes from the interrupt-driven keyboard with line editing — Backspace/DEL rub out the previous byte (`"\b \b"` echoed to both devices), keyboard Return (CR) is exposed to readers as `'\n'`, the read ends at newline or count — plus a single-byte `bm_tty_getc`.  This is the stdio seam: a future `read(0,…)`/`write(1,…)` routes here instead of libstub's DOS INT 21h.  New battery test `tty_bm`: the harness types **`vx\b9k\nz`** through MAME's natural keyboard, so the rubout is a REAL Backspace keystroke travelling VIA CS2 → IR6 → compiler-emitted ISR → ring buffer → line editor — the cooked read returns exactly `"v9k\n"`, the VRAM readback proves the screen shows the EDITED line (`v9k> v9k`, rubbed-out cell blank), the cursor wraps to row 1, and the queued `z` arrives through `bm_tty_getc`.  FIRST-RUN PASS, **zero compiler changes** (third driver session running on the §6d ISR ABI).  Harness: `run-victor-baremetal.sh` `V9K_KEYPOST` is now BYTE-SAFE — every byte is passed to Lua as a `\ddd` decimal escape, so control characters type real Victor keys (`\b` → Backspace key 26, `\n` → Return via the S88 path), both arriving byte-exact on the first try; `test-newlibc.sh` keypost fields decode through `printf %b` (entries can write `vx\b9k\nz` textually); `build-newlibc-baremetal.sh` gained the `bm_tty.h` probe.  The `tty_bm` golden contains the literal echo bytes (raw 0x08s in `v9k> vx\b \b9k`) — deterministic, diff-clean.  Gates: test-newlibc **10/10**, test-dos **289/289** after refreshing THREE stale DOS-hosted goldens (`newlibc_fat_root/fat_file/ramfs_test`) — the `~/projects/newlibc` tree moved under us (upstream `5727ffb` "POSIX errno audit": invalid-8.3 EINVAL→ENOENT, past-EOF lseek now POSIX-legal, one new O_CREAT check; every changed line still PASS, i.e. source drift, not a toolchain regression — message TEXT changed, which a miscompile can't do), test_omf_link all pass; NO toolchain change so no emit audit / MP byte-compare triggered.  Next: **step 4e** — route stdio through bm_tty: a bare-metal `read(0)`/`write(1)` seam (decide its shape — libstub-level swap vs newlibc's VFS `/dev/console` routing moved bare-metal) toward actually retiring libstub's INT 21h stdio; block/SASI driver port (MAME `-scsi:0 harddisk`) toward bare-metal FAT; serial TX ISR if a consumer appears; run-dos-exe.sh stdin redirect for the 3 remaining DOS-hosted tests.)

## §6g session notes (2026-06-12)

### bm_tty (minic/dos/newlibc/bm_tty.c, bm_tty.h)
- The console_dev_read/console_dev_write contract, preserved exactly:
  echo screen-first then serial; rubout is "\b \b" to both devices
  (bm_display_putc('\b') is already destructive, but the ' '+'\b' pair
  keeps the two devices in lockstep with newlibc's sequence); CR→LF so
  line readers see '\n'; reads block on bm_keyboard_getc.
- bm_tty_init = bm_display_init + bm_keyboard_init, so it inherits the
  keyboard's window: AFTER bm_interrupts_init (PIC re-init), BEFORE
  bm_interrupts_enable.
- bm_tty_write/bm_tty_read are the future fd-1/fd-0 device entries;
  bm_tty_getc/bm_tty_putc are the byte pair.

### tty_bm test
- Input "vx\b9k\nz" exercises: ordinary chars, a real Backspace edit,
  Return (S88 path → CR → cooked '\n'), and a queued post-line byte.
- Checks: line == "v9k\n" (4 bytes); screen row 0 == "v9k> v9k" with
  cell 8 blank (the rubbed-out 'x' is GONE from VRAM); cursor at (1,0)
  after the newline echo; bm_tty_getc → 'z'; ISR count > 0, overruns 0;
  timer alive.  All phases print first (5 MHz 8088 rule).

### Harness facts
- V9K_KEYPOST encoding: `od -An -v -tu1 | awk → \ddd` — any byte
  survives the single-quoted Lua literal.  MAME natkeyboard maps 0x08
  to the Victor Backspace key and 0x0A to Return; both validated here.
- test-newlibc.sh keypost field goes through `printf %b`; existing
  plain-text entries (keyboard_bm "v9k") are unaffected.
- The default 3 s keypost delay is FINE for tty_bm: display init's 8 KB
  font copy is only ~0.1 s of 8088 time (interrupt_bm's slowness was
  the 60-line scroll stress, not init).
- DOS-hosted newlibc goldens track a MOVING upstream: ~/projects/newlibc
  is under active development, so a [FAIL] whose diff shows changed
  message TEXT (not garbage) = upstream source drift — check newlibc
  git log, eyeball the new behavior, refresh the golden via
  build-newlibc-test.sh + run-dos-exe.sh.  This session: 5727ffb errno
  audit changed fat_root/fat_file/ramfs expectations.

### Open tracks (new + carried)
- newlibc step 4e: stdio-over-bm_tty — the bare-metal read(0)/write(1)
  seam that retires libstub's INT 21h stdio (decide: libstub-level swap
  vs newlibc VFS /dev/console routing moved bare-metal); block/SASI
  driver port (MAME -scsi:0 harddisk) for bare-metal FAT; serial TX ISR
  when a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

# Next session (§6g — continue Phase 6.  §6f [2026-06-12, previous session] completed **step 4c: the bare-metal battery grew 5/5 → 9/9** — the four "near-free" newlibc phase-3 tests are ported and standing (`tools/test-newlibc.sh`): `memory_bm` (font-RAM 0000:0C00 + screen-RAM F000:0000 byte write/readback, distinct patterns to catch segment aliasing), `crtc_bm` (display bring-up verified through raw far-pointer VRAM word writes read back via the driver hook, plus RAW R14/R15 cursor readback — (12,34)→0x03/0xE2 — the only 6845 registers with readback), `pic_bm` (IMR get/set/restore on unused IR5 while the timer is LIVE on IR2, deterministic post-init mask 0xFB, plus the gating proof the original only implied: masking IR2 freezes ticks, unmasking resumes them; continuous ticks = the EOI check), and `interrupt_bm` (the battery's first CROSS-DRIVER stress: display init's 8 KB far font copy + 60 scrolling lines all under the live timer ISR — far MMIO/ES traffic racing the compiler-emitted ISR ABI — then delay-in-range and screen-intact checks).  Every port PASSED ON THE FIRST RUN — zero compiler changes again; the only driver delta is `bm_pic_get_mask`/`bm_pic_set_mask` (IMR readback was already there as a static).  One harness lesson: `interrupt_bm`'s scroll stress needs a 120-emulated-second budget (60 scrolls ≈ 90+ s on the 5 MHz 8088; the 40 s first try truncated mid-phase — slowness, not a hang, per the determinism rule).  `build-newlibc-baremetal.sh` gained a `bm_pic.h` include probe.  Gates: test-newlibc **9/9**, test-dos 289/289; NO toolchain change so no emit audit / MP byte-compare triggered.  Next: **step 4d** — the cooked console (bm_display putc + bm_keyboard getc behind a console layer) toward retiring libstub's DOS-only stdio; block/SASI driver port (MAME -scsi:0 harddisk) for bare-metal FAT; serial TX ISR if a consumer appears; run-dos-exe.sh stdin redirect for the 3 remaining DOS-hosted tests.)

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

# Next session (§6e — continue Phase 6.  §6d [2026-06-11, this session] completed **step 4a: a real `__attribute__((interrupt))` ABI, end-to-end to a live timer interrupt on the Victor 9000.**  The old ISR story (an `asm "iret"` appended to the body — frame still up, no register save, block left unterminated) is GONE; the attribute now travels as QBE **`interrupt` function linkage** (`Lnk.isr`, parse.c keyword, lexh K regenerated — tools/lexh.c's stale token list now mirrors the runtime kwmap exactly) and the i8086 backend emits the **ES-safe ISR prologue/epilogue itself**: two CS-local words ahead of the entry label (`_qbe_isr_es_<fn>: dw 0`, `_qbe_isr_dg_<fn>: dw DGROUP` — the proven libstub `_dgroup_para` pattern, correct under both MZ relocs and raw-binary absolute patching), ES saved to static memory FIRST (newlibc's hardware-validated Victor rule — never on the stack), all registers saved, DS=ES=DGROUP established without trusting the interrupted DS, standard frame inside, and every `ret`/`retf` block replaced by the full restore + `iret`.  `libstub _qbe_get_cs` + the model-agnostic install idiom (`seg = (uint32_t)fnptr >> 16, 0 ⇒ qbe_get_cs()`) cover IVT installs in near- AND far-code models.  Probe-gated bug-loud (`isr_probe.c`, small+medium, 1006 software-INT fires; pre-fix toolchain dies at build: "last block misses jump").  Drivers ported to minic dialect (`bm_pic.c`, `bm_interrupts.c`, `bm_timer.c` — the 8253 ch2/IR2/vector-0x42 facts preserved, ALL the original's inline asm gone), and **`timer_bm` PASSES on MAME victor9k**: live IR2 ticks through the compiler-emitted ISR, ~200 ISR entries, delay-in-range, cli-freeze — now a `tools/test-victor.sh` golden entry.  KEY bare-metal lesson: a bare `sti` without a full PIC re-init (ICW1=0x17 Victor value, base 0x40, clear stale in-service bits, mask 0xFF) wild-jumps within milliseconds — the boot ROM leaves IRQs unmasked on stale vectors.  Emit-audit taught the ISR epilogue (`CHKT … live=isr` skip tag): baseline now **369 files / 117,002 regions / 0 violations**.  Gate 287→**289/289**; grammar 115 s/r unchanged.  MP compact: compiler-neutrality PROVEN byte-identical (old toolchain vs new-compiler+old-libstub, whole-image cmp); the only delta is the +5-byte `_qbe_get_cs` libstub insertion, and the shifted image is Victor-validated (math PROG.PY byte-exact vs host python3 via run-victor-sasi.sh); victor pipeline **4/4**.  Next: **step 4b** — extended-asm output constraints + Intel template translation (still unneeded by the timer path — decide whether keyboard/display force it); keyboard ISR (IR6) + display driver ports; serial RX ISR; grow the bare-metal battery → `tools/test-newlibc.sh`.)

## §6d session notes (2026-06-11)

### The ISR ABI (QBE `interrupt` linkage → i8086 backend)
- minic: `__attribute__((interrupt))` → `interrupt function` via fn_export_kw();
  the three asm-"iret" sites now emit a plain `ret` (the backend owns the
  epilogue).  Flag hygiene audited: every definition path resets
  cur_fn_interrupt (attrreset / type_and_ident) before optionally setting it.
- parse.c: `interrupt` linkage keyword (data rejects it); all.h Lnk.isr.
  **lexh trap**: adding ANY keyword needs a new perfect-hash K; tools/lexh.c's
  token list was stale (missing asm/loadfs/storefs/addfo/subfo/vargp) — it now
  mirrors the runtime kwmap exactly.  New K=520135915.  make check green.
- i8086_emitfn (Lnk.isr): CS-local `dw 0` (ES save) + `dw DGROUP` words emitted
  AFTER `.text` and BEFORE the entry label, so asm_to_omf.py's
  function-boundary splitter keeps them glued to the function.  Prologue:
  `mov [cs:es],es` → push ax/cx/dx/ds → `mov ds,[cs:dg]` → ES=DS → standard
  frame (bx/si/di layout unchanged at [bp-2/-4/-6]).  Epilogue (ALL Jret*
  forms): standard unwind → pop ds/dx/cx/ax → `mov es,[cs:es]` LAST → iret.
- Audit: ISR ret regions are tagged `; CHKT n live=isr`; check_emit_brackets.py
  skips them (the epilogue restores the INTERRUPTED context — every register
  including ES/DS legitimately differs from region entry; one fixed template).
  Stale-corpus trap: run-emit-audit.sh CACHES probe asm — rm the probe's
  build/chk-corpus entries after changing its codegen.

### isr_probe (the reduced gate)
- Software INT 0xF1 (user range): near-data store + far MMIO write (40:F0 ICA
  scratch) + callee with 32-bit divide inside the handler; live locals across
  triggers; 1006 fires (stack-balance hammer); vector saved/restored.
- Model-agnostic install: `lin=(uint32_t)fnptr; seg=lin>>16; if(!seg)
  seg=qbe_get_cs();` — far-code models carry seg:off in the pointer (cast
  preserves raw bits), near-code models call the new libstub helper (C name
  `qbe_get_cs`, asm `_qbe_get_cs`, caller's CS — near-code only by design).
- Bug-loud verified: stashed toolchain fails at BUILD ("last block misses
  jump") — the old asm-"iret" left the ret block unterminated.

### Bare-metal timer (minic/dos/newlibc/)
- bm_pic.c: **the load-bearing lesson** — full 8259A re-init before any sti
  (ICW1 0x17 = Victor ROM value NOT IBM 0x11; ICW2 0x40; ICW4 0x01; 8 specific
  EOIs to clear the ROM's in-service bits; mask 0xFF).  Without it the boot
  ROM's unmasked IRQs (IR7 vsync fires every frame) hit stale vectors whose
  RAM workspace the image overwrote — symptom: output RESTARTS from the banner
  (wild jump re-enters the 0x3000 loader stub).  pic_delay = two
  `jmp short $+2` (NASM-safe, no labels).
- bm_timer.c: 8253 ch2 mode 2 divisor 1000 via plain volatile-far byte stores
  (the original's intel_dev_write_byte asm was ia16-gcc store-merging damage
  control); double-read tick getter; delay_ms.  bm_interrupts.c: timer_isr
  (interrupt attr) = tick handler + specific EOI 0x62; IVT install via
  bm_set_vector (far write to 0:int*4).
- timer_bm.c: 7 printed phases (5 MHz rule), deterministic booleans/ranges
  only — MAME clocks ch2 at 125 kHz vs the documented 100 kHz, so tick-vs-wall
  numbers vary but tick accounting is exact.  PASS on MAME; golden +
  test-victor.sh entry "victor bare-metal (timer_bm, live ISR)".
- build-newlibc-baremetal.sh links bm_timer.c/bm_interrupts.c/bm_pic.c only
  when the program #includes their headers — hello_bm image stays stable.

### Open tracks (new + carried)
- newlibc step 4b: keyboard ISR (IR6) + display driver port; serial RX ISR;
  extended-asm output constraints + Intel template translation (NOTHING in
  the timer path needed it — decide whether keyboard/display do);
  tools/test-newlibc.sh once the bare-metal battery has a few more entries.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp; multi-decl items after the first skip block_scope_decl;
  Kw spill-slot sharing.

---

# Next session (§6d — continue Phase 6.  §6c [2026-06-11, this session] completed **step 3: the toolchain's first BARE-METAL program runs on the Victor 9000.**  `omf_link.py --raw-binary --load-addr 0x3000` emits a flat binary (selectors resolved at link time against the load paragraph, no MZ header, a 32-byte synthesized register-setup stub at the image head — the MAME Lua loader enters at 0:0x3000 with CS=DS=SS=0 — and BSS rides as zeros, no clear loop).  A **minic-built crt0** (`bm_crt0.c` `start()` → board init → `main()` → hlt loop) plus a minic-dialect **polled NEC 7201 serial console** (`bm_console.c`, newlibc's validated VIA2/8253-counter-0/WR-register sequence, pure volatile-far MMIO — the original's inline asm was all ia16-gcc workarounds) carry `hello_bm.c` to a PASS over serial under MAME: `tools/build-newlibc-baremetal.sh` + `tools/run-victor-baremetal.sh` (Lua autoboot loader, null_modem bitbanger capture).  Gated: `test_omf_link.sh` test 3 (deterministic raw-image structure asserts) + a `tools/test-victor.sh` golden-diff entry (victor pipeline 3/3).  DOSBox gate stays **287/287**; MP compact **byte-identical** (MZ-path refactor also proven by relink `cmp`).  Next: **step 4** — drivers/ISRs: ISR definition strategy, extended-asm output constraints + Intel template translation; port timer/display; more newlibc tests bare-metal; `tools/test-newlibc.sh` once a battery exists.)

## §6c session notes (2026-06-11)

### omf_link.py raw-binary mode (the step-3 enabler)
- `--raw-binary --load-addr 0x3000` (default 0x3000, paragraph-aligned): all
  loc==2/loc==3 selector fixups get `frame_para + base_para` patched in at
  link time (base_para = load_addr>>4) instead of an MZ reloc record; layout
  starts at byte 32 (`RAW_STUB_SIZE`) so segment paragraph alignment holds.
- The synthesized head stub: `cli; mov ss/sp; mov ds/es=DGROUP; jmp far
  entry` — all constants known at link time (same `_compute_ss_sp()` as the
  MZ header, SS=DGROUP + SP=stack-top-in-DGROUP for the small model).  The
  hlt-padded 32-byte head means entry lands at image para 2.
- Program-RAM ceiling check: image end past 0x9F000 (video RAM at 0xA0000)
  is a link error.
- MZ path refactor (shared `_concat_segments`/`_compute_ss_sp`) verified
  byte-identical: snprintf_test relink `cmp` + MP compact whole-image `cmp`.
- `test_omf_link.sh` test 3 asserts the raw structure (no MZ sig, stub
  opcodes at fixed offsets, entry 0302:0000, far-call selector 0303
  absolute); also fixed test 2's stale-crt0 collision (stevie-orig now
  carries crt0_exe.obj — excluded from the smoke link).

### Bare-metal runtime story (minic/dos/newlibc/)
- `bm_crt0.c`: C `start()` (OMF `_start`) → `bm_board_init()` → `main(0,0)`
  → `while(1) hlt`.  No DOS crt0, no PSP, no HALT2DOS rewrite — bare metal
  WANTS the hlt idle loop.  BSS zero-fill not needed (in-image zeros).
- `bm_console.c` + `.h`: polled 7201 channel-A TX at 9600.  Mirrors
  newlibc drivers/console.c exactly (VIA2 port A bit0 internal clock; 8253
  counter 0 — NOT counter 1 — mode 2 LSB+MSB divisor 8; channel reset +
  WR0/WR4=0x44/WR3=0xC1/WR5=0xEA/WR1=0); all access via v9k_hw.h
  HW_READ/WRITE_BYTE volatile-far MMIO.  The original's inline asm
  (SAVE_ES/RESTORE_ES, forced byte stores) is ia16-gcc damage control this
  backend doesn't need.  Plus bm_puts/bm_putu (32-bit udiv)/bm_puthex.
- `hello_bm.c`: 48 KB raw image; checks volatile 16-bit mul, 32-bit
  unsigned divide, strlen, hex print; __V9BEGIN__/__V9END__ sentinels +
  PASS:/FAIL: verdict (newlibc run_test.sh regex convention).
- libstub on bare metal: `--no-stdio` libstub links fine (its INT 21h
  sites — exit/putc/dos_*/int86 — are functions, nothing runs at startup;
  `_dgroup_para: dw DGROUP` resolves via the raw selector patch).  They are
  LANDMINES if called; the real fix is newlibc replacing libstub (the
  Phase-6 end state).

### Harness
- `tools/build-newlibc-baremetal.sh [--load-addr=] <name|path.c>`: test TU +
  bm_crt0 + bm_console, small model, `--no-stdio` libstub, raw link.  Bare
  name resolves minic/dos/newlibc/ first, then newlibc tests/.
- `tools/run-victor-baremetal.sh <bin> [secs]`: MAME victor9k + Lua
  autoboot loader (newlibc phase-3 pattern: write_u8 loop at 0x3000, zero
  segment regs, IP=0x3000), serial via `-rs232a null_modem -bitbanger`,
  sentinel-trimmed stdout, exit 77 skips, same orphan-killer watchdog as
  run-victor-sasi.sh.
- `tools/test-victor.sh` new entry "victor bare-metal (hello_bm)" diffs
  against `minic/dos/tests/hello_bm.golden.txt`; skips when newlibc tree or
  MAME absent.  Victor pipeline 3/3.

### Open tracks (new + carried)
- newlibc step 4: ISR definition strategy; extended-asm output-constraint
  store marking + Intel-syntax template translation; port timer/display
  drivers to minic dialect (the bm_console port shows most driver asm is
  removable); grow the bare-metal test battery → tools/test-newlibc.sh.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp; multi-decl items after the first skip block_scope_decl;
  Kw spill-slot sharing.

---

---

# Next session (§6c — continue Phase 6.  §6b [2026-06-11, this session] completed **step 2: the newlibc portable subset runs DOS-hosted and is gated.**  ELEVEN phase3 tests (snprintf, fat_bpb/chain/root/dir/file, fat_vfs, ramfs, stdio_route, bss, terminal_meta) now build small-model via `tools/build-newlibc-test.sh` — test TU (`-Dmain=newlibc_test_main`) + libgloss/VFS/FAT/block + `minic/dos/newlibc/dos_shim.c` against crt0 + `libstub_to_exe.py --no-stdio` — and byte-diff against goldens in `tools/test-dos.sh`.  The full newlibc stack (printf wrappers → syscalls → VFS → devices/FAT-over-RAM-block) executes through THIS toolchain in DOSBox.  Two toolchain bugs fixed en route (static file-scope DATA linkage; decimal `UL` literal typing), both probe-gated.  Gate 274→**287/287**.  MP compact byte-identical (rigorous: pre-change toolchain rebuilt from git and whole-image `cmp`'d).  Next: **step 3** — omf_link raw-binary output + minic-built crt0 + MAME bare-metal hello at load addr 0x3000; then step 4 (drivers/ISRs/extended-asm).)

## §6b session notes (2026-06-11)

### The two toolchain fixes (probe-gated, loud-verified pre-fix)
1. **Static file-scope DATA had no internal linkage** — minic NEVER emitted `export`
   on data (`export data` is valid QBE it just didn't use), and asm_to_omf.py
   compensated by auto-promoting EVERY data `_xxx:` label to an OMF public; two TUs
   reusing a static name died "duplicate public symbol" (newlibc: libgloss/dirent.c
   and vfs/vfs.c both define `static … dir_table[]`).  Fix mirrors §1q's function
   story end-to-end: minic marks non-static file-scope data `export data` (new
   `glostatic[]` flag; an `nglo` watermark `glo_decl_start` captured in
   type_and_ident and retro-marked by the STATIC typed_decl variants — the variants
   reduce AFTER typed_decl_rest registers the slots, and the lexer's pending_static
   is already reset by then; `emit_static_local` marks mangled function-locals;
   `STATIC structstart` marks its sai slot) and asm_to_omf.py's data auto-promotion
   is REMOVED — `.globl` is now authoritative for code AND data.
   `static_data_probe` (TWO TUs, small+medium gate entries: same-name file statics,
   same-name block statics behind same-name static fns, plus a cross-TU extern
   global proving export still works).
2. **Decimal `12345UL` was typed int** — the decimal lexer's `u`-suffix branch
   consumed a trailing `L` without setting suffix_l (hex/octal were correct), so a
   `UL` literal ≤0xFFFF was pushed as ONE stack word; newlibc's snprintf_test `%lu`
   read a garbage high word (`12345UL` printed 1093808185 = 0x4132<<16 | 12345).
   longconst_probe gained ulvararg/ulassign cases (golden regenerated, medium+large).

### Step-2 infrastructure (committed, not probe-grade)
- `tools/build-newlibc-test.sh`: small model only (far models would need newlibc's
  printf to answer minic's far_stdlib mangling — later phase).  `NEWLIBC_DIR`
  overrides `~/projects/newlibc/phase3_newlib`.
- `minic/dos/newlibc/shiminc/`: the §6a triage shim headers PROMOTED to the
  committed tree (gate reproducibility); `build/newlibc-triage/sweep.sh` still has
  its own copy (probe-grade, can be repointed later).
- `minic/dos/newlibc/dos_shim.c`: console/tty device ops (INT 21h AH=3F/40 via
  libstub int86), 100Hz wall-clock timer (AH=2Ch), display_puts→console,
  POSIX unprefixed aliases (open/read/write/…→`_`-syscalls), minimal
  FILE-table fopen/fclose/fread/fgetc over VFS fds (libstub's one-word
  `_stdin/_stdout/_stderr` sentinels are layout-compatible with newlib's
  `FILE{int _file;…}` first member — kept), `_impure_ptr`/`__heap_start/_end`
  link satisfaction, and `main()` = `vfs_init()` (board_init()'s job on metal)
  then `newlibc_test_main()`.
- `libstub_to_exe.py --no-stdio` (near-code only): drops FILEIO_EXE+FAR_SPRINTF_EXE
  and skips libstub.asm `_sprintf/_fgets/_putchar/_abort/_stat` so the newlibc
  printf family + dos_shim own those names; keeps malloc/free, str/mem fns,
  int86/intdos, `_qbe_*` helpers, and the stdio sentinels.
- Gate: `run_newlibc_test` in tools/test-dos.sh, skip (77) when the newlibc tree
  is absent; goldens `minic/dos/tests/newlibc_<test>.golden.txt`.

### Excluded / deferred tests (with reasons)
- memory_test: scans the Victor physical memory map — hardware-flavored, fails
  DOS-hosted by design; belongs to step-3+ MAME bare-metal.
- stdin_test / scanf_test / read_test: need stdin feeding — run-dos-exe.sh has no
  input-redirect support yet (DOS `< file` works per the §3n REPL work; small
  harness lever if wanted).
- hello: BUILDS and is shimmed (timer/display/malloc) but prints wall-clock tick
  values — not golden-able as-is.

### Open tracks (new + carried)
- newlibc step 3: omf_link raw-binary output mode + minic-built crt0 + MAME
  bare-metal hello (load addr 0x3000, serial output); then tools/test-newlibc.sh.
- run-dos-exe.sh stdin redirect (unlocks 3 more newlibc tests).
- newlibc-under-far-models stdio story (minic far_stdlib mangling vs newlibc's
  own printf) — decide when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g, static_sym_init_probe reproduces);
  extended-asm output constraints + Intel template translation (step 4); ISR
  definition strategy (step 4); param/static-local shadowing a global; huge
  `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic
  static-init FLOAT const-expr folding; small setjmp/longjmp; multi-decl items
  after the first skip block_scope_decl; Kw spill-slot sharing.

---

# Next session (§6b — continue Phase 6.  §6a [2026-06-11, this session] ran the newlibc TRIAGE SWEEP (Phase-6 step 1) and fixed SEVEN minic dialect gaps it exposed: the per-TU sweep (`build/newlibc-triage/sweep.sh` + `shiminc/` newlib-shaped shim headers) took phase3_newlib from **21/66 → 46/66 TUs compiling** under BOTH small and medium, and the **entire portable subset (libgloss ×7, vfs ×2, every non-asm test) now compiles** — exactly the step-2 target population.  Every remaining failure is inline-asm-flavored (drivers + dos_tests) or the ISR-definition gap — i.e. step-4 work, none of it portable-subset.  Six new probes, gate 262→274.  Next: **step 2 proper** — link the portable subset against libstub, run VFS/FAT/printf tests DOS-hosted in DOSBox, gate them; then step 3 (omf_link raw-binary output + minic crt0).)

## §6a session notes (2026-06-11)

### The seven minic fixes (each probe-gated, all loud-verified pre-fix)
1. **`extern T *f(args);`** — extern + pointer-return + ANSI params had NO production
   (only K&R `*f()`); errno.h's `extern int *__errno(void);` killed ~29 TUs at line 1.
   New ext_decl kind `'H'` (par1 stashed on `->l`, fnproto_record'd in the extern walks).
   `extern_ptrret_probe`.
2. **File-scope prototype param leak** — par1's param() varadds names at file scope and
   nothing removed them after a PROTOTYPE; a later decl reusing the name with a different
   type died "double definition" (`int first(char *buf,…); extern long second(…,const
   void *buf,…)`).  Fixed: `varclr()` at the end of every file-scope prototype-only
   reduction (ansi_proto_register, EXTERN par1, both ext_decllist walks).  Definitions
   were always safe (init_ansi/init_kr varclr first).  `proto_param_leak_probe`.
3. **Array parameter declarators** — par1 had no `'['…']'` forms at all: `uint8_t out[11]`,
   `char buf[]`, `char *const argv[]` all parse-errored (the *const was a red herring —
   `type '*' CONST` existed).  Four new par1 productions, decay to (far-aware) pointer,
   dimension folded and discarded.  `array_param_probe`.
4. **`void __far __attribute__((interrupt)) f(void);`** — the ia16-gcc far-ISR spelling
   (interrupts.h) had no production for the interposed `__far`; new
   `type TFAR attropt IDENT` in type_and_ident accepts-and-drops the __far.  PROTOTYPE
   only: ISR *definitions* remain a designed gap (the vestigial interrupt emission
   produces `asm "iret"` with no block terminator — QBE rejects — and would skip the
   epilogue anyway; Phase-6 step 4 decides the real ISR strategy).  `isr_far_attr_probe`.
5. **`const volatile T`** — qualifier pair missing everywhere; new `vol_qual` nonterminal
   (VOLATILE | CONST VOLATILE | VOLATILE CONST) replaced the bare-VOLATILE heads in all
   type productions (incl. STRUCT/UNION/TNAME).  Covered by the font_test-shaped probe
   cases inside array_param/others; no dedicated probe (parse-only, exercised by sweep).
6. **Scalar global symbol-address init** — `char **environ = __env;` / `int *p = &x;` /
   `int *mid = &arr[2];` died (the `'=' expr ';'` rule folded with const_eval only; the
   aggregate path §1b/§1g could already emit `$sym+off`).  Now routed through cival_eval
   → new emit_global_sym_init.  `static_sym_init_probe` (small+medium).  **The probe
   under compact CONFIRMED the §1g far static-DATA-ptr reloc gap at runtime** (prints raw
   offsets 4194/4192 — segment missing) — now a reproducible open track, NOT gated far.
7. **Locals shadow file-scope bindings** — minic had NO local-shadows-global support
   (`int g; int f(){int g;}` died), only §1k local-vs-local inner-block renames.  newlibc
   vfs_open declares `const fat_mount_t *fat_mount;` next to the file-scope function
   fat_mount() (found by automated delta-reduction of the 785-line failing prefix).
   Fixed: block_scope_decl's rename trigger extended to any global/extern/function/enum
   binding + block_scope_decl wired into the dcls-chain local rules (fn-body depth) —
   the stmt-context rules already had it.  `local_shadow_probe` (global var + function +
   enum constant all shadowed, post-shadow global intact).
   Plus: postfix prototype attribute `void _init(void) __attribute__((weak));` (new
   `ansi_proto_register ATTRIBUTE…';'` production), and `die("undefined variable")` now
   prints the NAME (4 shim-gap diagnoses fell out instantly).

### Sweep infrastructure (build/newlibc-triage/, intentionally untracked probe-grade)
- `sweep.sh [model]`: per-TU clang -E (-nostdinc **-D__ia16__** — keeps `__far` real and
  selects the GCC MK_FP branch in v9k_hw.h, which matches minic semantics) → minic →
  qbe → asm_to_omf → nasm, keep-going, stage-bucketed report.
- `shiminc/`: newlib-shaped shim headers (errno/unistd/fcntl/reent/dirent/stdio with
  struct FILE._file/sys/stat with S_IF*+S_BLKSIZE/sys/types with dev_t/time/limits/io/
  conio→dos.h/i86→dos.h).  These prefigure the real newlibc-port headers.
- minic line numbers in errors are 0-based-ish (error:0 = line 1); statements are emitted
  at the function-close reduce, so stmt-level errors report the `}` line — bisect inside.

### Remaining failures (all step-4 flavored, NONE portable-subset)
- 6 dos_tests: Watcom `_asm { … }` blocks (phase-1-style TUs; park or rewrite later).
- 5 qbe-stage: extended-asm `"=r"` OUTPUT constraints — minic substitutes the slot into
  the template but QBE sees a slot that is read-never-stored and rejects; ALSO
  interrupts.c ISR definition (designed gap, see fix 4).
- 8 nasm-stage: AT&T/ia16 mnemonics leak through the template (`pushfw/popw %0`,
  `movw %es`) — nasm wants Intel.  Driver asm will need per-target porting in step 4
  anyway (it was written for ia16-gcc).
- 46/66 small AND medium, identical fail sets.

### Open tracks (new + carried)
- far static-DATA-ptr reloc (§1g, now runtime-reproduced by static_sym_init_probe under
  compact) — needed before newlibc-style tables-of-pointers work in far-data models.
- minic extended-asm output-constraint store marking + Intel-syntax template translation
  (the qbe/nasm buckets above) — step 4.
- ISR definition strategy (minic save-all+iret codegen vs asm shims) — step 4.
- Param/static-local shadowing a global still dies (only plain locals fixed).
- Carried: huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic
  static-init FLOAT const-expr folding; small setjmp/longjmp; multi-decl items after the
  first skip block_scope_decl; Kw spill-slot sharing.

---

# (§6a designation, kept verbatim) Next session (§6a — DESIGNATED by the user 2026-06-11: **the newlibc frontier (Phase 6).**  Get `~/projects/newlibc` — a much-progressed Victor 9000 C library + driver suite — working when built by THIS toolchain, and adopt its test suite as a standing robustness harness; end state retires libstub.  Full plan + survey in ROADMAP.md → Phase 6.  Docs were consolidated this session (ROADMAP.md/CLAUDE.md rewritten, Phases 0–5 marked complete, the CLAUDE.md Prior: chain pruned, session history §3u–§5c-PLAN archived to SESSION_LOG.md).)

## §6 entry notes (gathered 2026-06-11)
- **newlibc survey**: `phase1_dos_drivers/` (DOS-hosted driver validation, OpenWatcom-built), `phase2_baremetal/`, `phase3_newlib/` (PRODUCTION — bare-metal newlib port built with **ia16-elf-gcc**, small model at load addr 0x3000, ~55–62 KB bins; medium secondary).  Phase 3 complete per its docs: crt0 + v9000.ld, libgloss syscalls, integer-only printf/scanf wrappers, sbrk malloc, VFS + read-only FAT12/16 over a SASI driver, CRTC display + loaded fonts (Victor has no char ROM), VIA 6522 keyboard, 8253-ch2-on-IR2 timer (NOT IBM IRQ0), PIC, serial.  **~40 test programs run under MAME victor9k** — that suite is the harness we want.  ~22 K lines / 82 files in phase 3.
- **Known toolchain gaps** (validate by probe, don't assume): minic has no `__attribute__((interrupt))` (ISRs need a minic feature or asm shims); omf_link has no raw-binary output mode (newlibc is bare-metal at a load address, not MZ .EXE); newlib proper (the ia16-built libc.a/libm.a it links) is OUT of near-term scope — target newlibc's own code (libgloss/drivers/VFS/tests) first; the small model (§5c, brand-new) gets its first real consumer and still lacks setjmp/longjmp.
- **Gated bring-up sequence** (detail in ROADMAP.md Phase 6): (1) triage sweep — compile newlibc sources with minic and bucket the failures (the MP-spike per-TU methodology); (2) portable subset (string/printf wrappers, VFS, FAT) DOS-hosted in DOSBox + gate entries; (3) raw-binary omf_link output + minic-built crt0 + MAME bare-metal hello; (4) drivers + ISR strategy; (5) `tools/test-newlibc.sh` standing gate; (6) libstub retirement.
- Rules of engagement unchanged: probe-first; `tools/run-emit-audit.sh` after any emit.c change; MP compact rebuild + body byte-compare after any toolchain change (731,088 is the current baseline).

## Carried open tracks (from §5c; pick by appetite)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic static-init float const-expr folding (also unlocks MICROPY_PY_MATH_CONSTANTS); small-model setjmp/longjmp (newlibc may force this); multi-decl items after the first skip block_scope_decl (loud); Kw spill-slot sharing.
- ~~ROADMAP.md refresh / CLAUDE.md prune~~ — DONE 2026-06-11.

---

# Next session (§5c is DONE 2026-06-11 — all three primary items landed in one session.  (1) **Upstream sync**: merged `c081897..e786f06` (3 commits; `fa19d3c` strict-aliasing fix in emit.c float-constant comments is output-identical) — make check green, gate 254/254, MP body byte-identical.  (2) **THE SMALL MODEL WORKS** (the only flat-out broken model, commit `4cfb321`): `libstub_to_exe.py` gained `near_code_model`/`unfar_epilogue` — small keeps libstub's native near ABI untouched and the EXE epilogue blocks (authored far-ABI) are reverse-transformed (retf→ret, `call far X`→`call X`, `[bp+N≥6]`−2, the printf engines' computed vararg bases `add si,N`−2, LIBSTUB_TEXT→_TEXT); FAR_DOSIO + SETJMP dropped under near-code (unreachable; SETJMP's jmp_buf is structurally far — a near setjmp is an OPEN track); `asm_to_omf.py` emits tiny/small code into shared `_TEXT` (no budget split); **`omf_link.py` now coalesces CODE segments BY NAME like DATA/BSS** (behavior-identical for far models — names unique; under small the three `_TEXT`s merge into ONE paragraph frame so near calls and 16-bit fn ptrs resolve against the runtime CS — fnptrprobe passes, proving it); crt0 `-DNEAR_CODE` → near `call _main`.  6 gate entries: cprobe/cstrprobe/fnptrprobe/mathprobe/dosapi_probe/fileio-roundtrip, all DOSBox-verified; cstrprobe has a small-specific golden (`%p` prints the C-correct 16-bit near ptr `5678`).  Gate 254→260.  (3) **the `float **` collision is FIXED** (commit `bf4a2e3`): FAR 24→26, QVOLATILE 25→27 — FLOAT (18) two shifts up no longer lands on FAR, so `float **` deref/store/param all decode right (pre-fix medium emitted loadl+loadfw+swtof garbage — probe-proven).  PLUS the probe's f3 case found the DIRECT-call sibling of §5b: `DREF(FUNC(ret))` strips ret bits on the flag positions (a direct fn returning `float **` puts FLOAT on new-QVOLATILE 27), so **fnproto gained `rett`/`has_rett`** (the fpproto.rett mirror; recorded at all 6 fnproto_record sites, used at the direct-call decode — layout-independent).  Residual collisions all moved one level deeper and surveyed UNCONSUMED (MP+stevie+probes): `unsigned T ***` (17+9=26), `float ***` (18+9=27), and far-data nested-far depth is now exactly ONE level (`T **` ok; T***'s innermost FAR needs bit 32 — the probe's short*** case under compact CONFIRMED the documented trade, reduced to short**).  Probe `float_dblptr_probe.c` (medium+compact, bug-loud).  Gate 260→**262/262** with every pre-existing golden unmoved; conflicts unchanged 115 s/r; MP compact rebuild BYTE-IDENTICAL (731,088) after EACH of the three changes → no Victor runs needed all session.  No designated successor — open tracks below.)

## §5c session notes (2026-06-11)

### Small model — what to know before extending it
- The near-code link model: ALL code (crt0 `_TEXT` + every TU's `_TEXT` + libstub `_TEXT`)
  coalesces by NAME in omf_link into one paragraph frame at para 0; entry CS=0.  Near calls
  are self-relative byte-distance fixups (frame-independent); 16-bit code-symbol fixups
  (fn ptrs, loc==1) resolve against the combined segment's frame == runtime CS.  This is
  why fn pointers REQUIRED the coalescing, not just contiguous placement.
- The i8086 backend needed ZERO changes: `sf_farcall()`/`farcall` in emit.c already gate
  helper calls on memmodel (near `call _qbe_div32s` under small), minic's `far_stdlib`
  mangling is NEAR_CODE-off, crt0 was the only far-call site.
- OPEN small-model gaps (extend on demand): no setjmp/longjmp (SETJMP_EXE dropped — needs
  a dedicated near impl with a 2-byte env return slot); softfloat probes not gated under
  small (the `_sf_*` helpers ARE near-callable and softfloat.c compiles — just never
  gated); no stevie-small (DGROUP pressure untested); `unfar_epilogue` is mechanical —
  any NEW epilogue block with computed arg offsets needs the `add si,N ; first vararg`
  idiom or plain `[bp+N]` so the reverse transform sees it.
- tiny .COM is untouched (flat concat via build-com-test.sh, never goes through
  libstub_to_exe/omf_link); `--model=tiny` through build-example.sh now produces the same
  near-code .EXE shape as small (untested, no consumer).

### float**/encoding — the residual map (post-move)
- Encoding law: any flag bit f collides with anything at bit f+3k after k shifts.  Current
  layout: SHORT 16, UNSIGNED 17, FLOAT 18, FAR 26, QVOLATILE 27.  Residuals: UNSIGNED@3
  →FAR, FLOAT@3→QVOLATILE (the fn-ptr/direct-call RETURN paths are immune via
  fpproto.rett / fnproto.rett), SHORT@4→28 (harmless today), far-data T*** loses its
  innermost FAR (bit 32 overflow).  A future flag must not land on 19–25 without checking
  depth collisions against all five.
- fnproto.rett applies to EVERY recorded direct call (not just float) — it also upgrades
  the stale-prototype shape where a function-local prototype's varh entry died at scope
  exit but fnproto persists (previously decoded FUNC(INT)).  MP byte-identical proves no
  shipping consumer changed.

### Open tracks (carried; pick by appetite)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i) — gate has huge entries to extend.
- `jmp_buf bufs[6]` (§4v, UNREDUCED) — reduce, classify, fix or document.
- minic static-initializer FLOAT const-expr folding (`static float x = 2.0f*3.14f;`) —
  also unlocks MICROPY_PY_MATH_CONSTANTS for free.
- Multi-decl items after the first skip block_scope_decl (loud "double definition").
- Small-model setjmp/longjmp (near env) if a consumer appears; softfloat gate entries
  under small (cheap thickening).
- **ROADMAP.md still stale since 2026-05-23** (predates the whole MP campaign + §4/§5
  toolchain work) — one consolidation pass; also prune the CLAUDE.md Prior: chain.
- Kw spill-slot sharing (no consumer pain); newlibc integration (STRATEGIC, needs user
  go/no-go — would retire the libstub ret-rewrite machinery §5c just made model-aware).

---
# (DONE in §5c above) Next session (§5c — DESIGNATED by the user 2026-06-11: **PIVOT OFF MICROPYTHON onto the core QBE/minic/linker 8086 toolchain workstream.**  The MP port is "very capable" and is now a CONSUMER of the toolchain, not the driver: its remaining tracks (math SPECIAL_FUNCTIONS, isclose, MATH_CONSTANTS, third GC area, MP_STACK_LIMIT lever, `-DMP_DBG_*` cleanup) are PARKED unless a toolchain fix unlocks them for free.  MP's role from here: the 108-TU compact rebuild is a regression corpus — after toolchain changes, rebuild and byte-compare the body; Victor re-runs only when bytes move.  Ordered plan below: (1) warm-up = the 3-commit upstream sync; (2) main track = the SMALL-model .EXE fix (the only memory model that is flat-out broken); (3) then the `float **` type-encoding collision (the silent-miscompile class §5b exposed); follow-ons and strategic items after.)

## §5c plan (recommended ordering, gathered 2026-06-11; survey notes inline)

### 1. Warm-up: upstream QBE sync — 3 commits (`c081897..e786f06`)
- `fa19d3c` "emit: fix aliasing-breaking pointer dereference via union" is
  TARGET-GENERAL emit code — the one of the three that can touch us.  `e786f06`
  (rv64 pc-rel globals) and `80d745c` (arm64 extern offset) are other-arch.
- Procedure (the PR #23 rebase recipe, scaled down): merge `upstream/master`,
  `make check`, full gate (254/254 expected), MP compact rebuild + body
  byte-compare (expect byte-identical; if fa19d3c moves bytes, diff the asm and
  re-run the Victor feature-4t probe), commit.  ~an hour including the gate.

### 2. MAIN TRACK: small-model .EXE (the only broken model) — see [[per-model-gate]]
- Symptom: DOSBox hangs.  Root cause (documented since the per-model gate work):
  `tools/libstub_to_exe.py` UNCONDITIONALLY rewrites every libstub `ret` → `retf`
  and shifts `[bp+N]` arg offsets +2 for the 4-byte far return address — correct
  for medium/compact/large/huge (far-code ABI), WRONG for small (near calls,
  2-byte return address, near `_main`).
- Shape of the fix: make the rewrite MODEL-CONDITIONAL (`--model` is already
  plumbed through libstub_to_exe.py since the compact bring-up) — small keeps
  libstub.asm's native near `ret` + unshifted `[bp+N]`; audit the EPILOGUE
  helpers for far-only assumptions (far_stdlib mangling is OFF under small in
  minic.y, so `_far_*` entries are unreachable — verify, don't assume); check
  crt0_exe.asm's `call far _main` vs near under small; omf_link near-code
  grouping (one CS segment — TEXT_SEG_BUDGET irrelevant under small).
- Acceptance: small entries in the gate for the standard probe ladder
  (cprobe, cstrprobe, mathprobe, fileio, fnptrprobe, dosapi_probe), DOSBox
  runtime-verified.  Expect latent near-ABI assumptions to surface — budget a
  full session.
- NOTE: if item 6 (newlibc) is ever greenlit, it subsumes this — do not
  gold-plate; model-conditional rewrite is enough.

### 3. `float **` type-encoding collision (silent-miscompile class, §5b sibling)
- Mechanism (proven in §5b): qualifier flags ride INSIDE the shifted type word —
  FLOAT (bit 18) + two IDIR/FUNC 3-bit shifts = bit 24 == FAR, and DREF strips
  ~FAR.  §5b fixed the fn-ptr RETURN case via the fpproto `rett` side table;
  `float **` (deref of pointer-to-pointer-to-float) still decodes the inner
  type as `int *` → loads `w` instead of `s`.  SHORT (bit 16) has the same
  collision at 3 levels (QVOLATILE, bit 25).
- Candidate fix surveyed in §5b: relocate FAR 24→26 and QVOLATILE 25→27.
  FLOAT then survives 2 levels (float ** and float-ret-fnptr both clean) and
  collides only at 3 (`float ***`).  COST: nested-far headroom shrinks — inner
  FAR bits sit one IDIR up (26→29 ok, 26→32 OVERFLOWS), so far-data models keep
  exactly ONE level of nested far pointer (`T **` ok, the innermost FAR of
  `T ***` is lost).  BEFORE committing: grep the MP corpus + stevie + probes
  for triple-pointer types under far-data models; the DREF comment block in
  minic.y documents the current 27/30 two-level headroom — update it.
- Probe FIRST (bug-loud `float_dblptr_probe.c`: store/load through `float **`,
  fn returning `float *` through a fn ptr, `short ***` if the SHORT fix rides
  along), then the bit move, then: full gate byte-compare (goldens should be
  IDENTICAL — the bits are internal), MP body byte-compare.

### 4. Follow-on correctness tracks (independent, pick by appetite)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i) — the documented hole
  in huge far-ptr arith; gate has huge entries to extend.
- `jmp_buf bufs[6]` (§4v, UNREDUCED) — array-of-jmp_buf cross-frame longjmp
  misbehaved; reduce first (suspect: §2m array_vartyp stride family), classify,
  then fix or document.
- minic static-initializer FLOAT const-expr folding (`static float x = 2.0f*3.14f;`
  dies "unsupported operation in constant expression"; INFINITY/NAN macros are
  runtime calls so they need a bits-level static-init story).  General C gap;
  also unlocks MICROPY_PY_MATH_CONSTANTS for free.
- Multi-decl items after the first skip block_scope_decl (loud "double
  definition", not silent — lowest priority of the four).

### 5. Maintenance (cheap, do opportunistically)
- **ROADMAP.md is stale since 2026-05-23** — predates the entire MP campaign,
  the emit-bracket audit, split stack, slot coloring, FLOAT, the math module.
  One consolidation pass; also prune the CLAUDE.md header scroll (the Prior:
  chain is most of a year of sessions deep).
- Kw spill-slot sharing (Kl/Ks got §4w coloring; plain Kw never shares) — small
  frame lever, no known consumer pain.
- Standing tool reminder: `tools/run-emit-audit.sh` after ANY i8086/emit.c change.

### 6. STRATEGIC (own campaign, needs user go/no-go): newlibc integration
- `~/projects/newlibc` is the planned real libc replacing libstub's organically
  grown helper set (printf engine as a python EPILOGUE string, near/far
  duplication, `_mktemp` deferral, per-model ret-rewrite hacks).  Biggest open
  workstream; would also resolve item 2 cleanly and retire a whole class of
  libstub ABI bugs ([[libstub-null-ptr-dx]] family).  Do NOT start it as a side
  effect of another track — it deserves its own bring-up plan (per-model ABI,
  OMF object production, gate migration).

---

# (DONE in §5b — successor §5c above) Next session (§5b is DONE 2026-06-11 — **the MicroPython `math` module is ON and Victor-verified byte-exact** (the standing §4x/§4z open track).  softfloat.c grew the missing soft-libm: `sf_sqrt` (fdlibm bitwise restoring, **correctly rounded** — 0 ulp vs host libm over 2M random samples), `sf_sin`/`sf_cos`/`sf_tan` (Cephes octant reduction with a **4-part 8-bit-chunk π/4 split** so every `y*DPn` product is exact for y < 2^16, |x| ≲ 51471 — the classic 3-part Cephes split has 11 bits in DP2, exact only to y < 2^13, which showed up as 8-ulp cos errors near |x| ~ 28000), `sf_atan`/`sf_atan2` (Cephes two-threshold + IEEE zero/inf grid matching CPython), `sf_asin` (atan of x/sqrt((1−x)(1+x)) — factored to dodge the 1−x² cancellation), `sf_acos` (2·atan(sqrt((1−x)/(1+x))), exact at both endpoints), `sf_frexp`/`sf_ldexp`/`sf_modf` (modf keeps x's sign on a zero frac per CPython), `sf_isfinite`.  Host ulp harness `build/sf-math-host-test.c` (cc -DSF_HOST + libm): sqrt 0 ulp, sin/cos ≤4, atan/atan2 ≤3, asin/acos ≤4, frexp/ldexp/modf bit-exact; near the zeros of sin/cos the final DP4 product rounding bounds the ABSOLUTE error at ~1e-12 (the pure-single-precision reduction limit — glibc sinf uses doubles internally; we have none).  **THE MINIC BUG the bring-up found: a float-returning function POINTER decoded as int-returning.**  FLOAT is type-flag bit 18 and IDIR(FUNC(ret)) shifts ret up 6 bits — FLOAT lands exactly on bit 24 == FAR, which DREF strips, so `float (*f)(float)` called through the pointer emitted `=w call` + a bogus swtof (garbage results).  This is exactly MicroPython modmath.c's shape: `math_generic_1(x, sqrtf)` passes every math function BY POINTER into `mp_float_t (*f)(mp_float_t)`.  Fix: `fpproto[]` (the §2q indirect-call side table) gains `rett` — the declared return type carried UNSHIFTED; `fpproto_alloc(rett, chain)` at all 6 declarator sites; the two indirect-call sites override the double-DREF decode when an fpid exists; and the two `par1` fn-ptr PARAMETER rules now record an fpid at all (they previously recorded NONE — no arg coercion either).  Sibling latent hole documented-not-fixed: `float **` hits the same bit-24 collision (no consumer).  Because modmath also needs the bare libm names as REAL SYMBOLS (a function-like macro never expands without a following `(`), softfloat.c exports alias functions `sqrtf`/`sinf`/.../`fmodf` (#ifndef SF_HOST — they would collide with the host harness's libm reference) and math.h declares those prototypes BEFORE defining the same-named macros (a later declaration line would itself be macro-expanded).  Wiring: `MICROPY_PY_MATH=1`; **MICROPY_PY_MATH_CONSTANTS stays 0** (tau's initializer is a float CONST-EXPR (2.0*M_PI) and inf/nan are INFINITY/NAN = sf_inff()/sf_nan() RUNTIME CALLS in minic's math.h — minic cannot fold either in a static initializer; math.e/math.pi are plain literals and work); 27 QDEF0 qstr appends (djb2 via py/makeqstrdata.py compute_hash); `moduledefs.h` MODULE_DEF_MATH; minic `NGlo` 256→512 (qstr.c's pool overflowed with the new strings).  Probe `mathfns_probe.c` (medium + compact, --softfloat; 63 bit-pattern lines incl. the fn-POINTER path; golden DOSBox-captured and line-by-line verified against host doubles by `build/mathfns-verify.py`, untracked).  Gate **248→254/254** (also closed the §4x cheap-thickening track: softfloat/softlibm/softtrig/double_float each gained a compact entry); `make check` green; conflicts unchanged 115 s/r.  MP body 717,168 → **731,088** (+13,920), ~93 KB under the ~824 KB ceiling.  **Victor: mp-math-probe.py BYTE-EXACT vs host python3** (exact prints: sqrt/floor/ceil/trunc/fabs/copysign/fmod/pow + frexp/modf tuples; 17 tolerance checks over sin/cos/tan/asin/acos/atan/atan2/exp/log/log-base/degrees/radians/pi/e; ValueError domain raises for sqrt(-1) and asin(2)); feature-4t byte-exact; float probe byte-exact; DOSBox small-config smoke byte-exact first (the §4z fast loop).  Successor: §5c above (core-toolchain pivot).)

## 2026-06-11 §5b notes (math module: soft-libm trig/sqrt + the fn-ptr float-return DREF/FAR collision)

### softfloat.c additions (minic/dos/softfloat.c, +~330 lines; existing functions untouched)
- `sf_sqrt`: fdlibm e_sqrtf bitwise restoring algorithm, U32-only, one result bit per
  iteration, remainder stays < 2^27 (no overflow); negative-half-exponent case (`m >>= 1`
  with m negative) verified through minic.  CORRECTLY ROUNDED (nearest-even).
- `sf_trig_reduce`: j = trunc(|x|·4/π) rounded up to even, octant = j&7, then
  r = (((|x| − y·DP1) − y·DP2) − y·DP3) − y·DP4 with DP1..DP3 of ≤8 significand bits
  each (EXACT products for y < 2^16) and DP4 the full-precision tail (the only rounding).
- sin/cos kernels: Cephes minimax (3 terms over z=r² + the r/1−z/2 leads); tan =
  sin/cos (one extra rounding, ≤8 ulp budget in the gate verifier).
- atan: Cephes two-threshold reduction (tan π/8, tan 3π/8) + 4-term poly; atan(inf)
  exact π/2.  atan2: explicit IEEE zero/inf grid (CPython-matching: atan2(±inf,±inf) =
  ±π/4 / ±3π/4, atan2(±0,−0) = ±π...), general case atan(y/x) + π-shift for x<0.
- frexp/ldexp/modf: pure bit manipulation; ldexp clamps n to ±280 BEFORE the int
  exponent add (16-bit int would overflow at n ~ 32767−254); modf returns ±0 frac with
  x's sign (CPython prints modf(-2.0) = (-0.0, -2.0)).
- Alias functions sqrtf/sinf/cosf/tanf/asinf/acosf/atanf/atan2f/expf/powf/fmodf — real
  exported symbols for the fn-pointer path, #ifndef SF_HOST.

### THE MINIC FIX — fpproto rett (minic/minic.y; conflicts unchanged 115 s/r)
- Repro: `static unsigned long via1(float (*f)(float), float x) { return fbits(f(x)); }`
  emitted `%t3 =w call %t4(s %t6)` + `swtof` — result class w, garbage float.
  `long (*f)(float)` was FINE (=l): LNG lives in the KIND bits, which shift cleanly.
- Root cause: FLOAT flag (bit 18) + two encoding shifts (FUNC, IDIR) = bit 24 = FAR;
  DREF masks ~FAR.  The same collision hits `float **` (FLOAT one IDIR up + FUNC...
  i.e. any type with FLOAT two levels deep) — LATENT, no consumer, documented here.
- Fix: fpproto[] gains `unsigned rett`; fpproto_alloc(rett, chain) also allocates when
  chain is empty but rett carries a FLOAT bit (FLOAT | FLOAT<<3 — float and float*
  returns); the 6 declarator sites pass the parsed return type; call()'s fn-ptr branch
  and expr case 'I' use fpproto[fpid].rett instead of the double-DREF decode when an
  fpid is recorded; the par1 fn-ptr param rules now varsetfpid(fpproto_alloc(...)).
  Existing behavior: for every non-float fn ptr rett == the old decode — gate goldens
  unmoved (254/254).
- NGlo 256 → 512 (qstr.c hit "too many globals" with the 27 new qstr pool strings).

### Verification
- Host: build/sf-math-host-test.c ALL OK (limits above).  Golden independently
  re-derived: build/mathfns-verify.py checks every golden line vs host doubles —
  sqrt/specials/frexp/ldexp/modf EXACT, trig within declared ulp budgets.
- Gate 248 → 254/254 (mathfns_probe medium+compact + 4 compact-thickening entries);
  make check green.
- DOSBox small config (MP_HEAP_SIZE=8192 MP_HEAP2_SIZE=12288 MP_STACK_SIZE=16384,
  612 KB image): mp-math-probe.py byte-exact vs host python3.
- **Victor (full 751,664-byte image, body 731,088): math probe byte-exact, feature-4t
  byte-exact, float probe byte-exact.**  Clean D4/C5 exits.

### Open tracks (ordered into the §5c plan above; carried + new)
- `math` SPECIAL_FUNCTIONS (log2/log10/hyperbolics/erf/gamma) — needs more soft-libm;
  log2f exists already, log10/cosh/sinh/tanh/acosh/asinh/atanh/erf/lgamma do not.
- MICROPY_PY_MATH_CONSTANTS — needs minic float const-expr folding in static
  initializers (tau = 2.0*M_PI) + a static-init story for INFINITY/NAN (runtime calls
  in minic's math.h).  math.isclose (MICROPY_PY_MATH_ISCLOSE) is another cheap add.
- **`float **` latent collision** (same bit-24/FAR mechanism as the §5b fn-ptr fix) —
  fix would need the FLOAT flag out of the shifted chain entirely; no consumer today.
- Third GC area (gcheap3.c clone, +~64 KB; §4z note) — ~93 KB headroom now, so a
  third FULL area no longer fits; heap1 could still grow ~11 KB inside main_BSS.
- Upstream sync: 3 commits (`c081897..e786f06`).
- Carried: `jmp_buf bufs[6]` latent minic note (§4v, unreduced); huge `_qbe_huge_add`
  ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup in the external tree; Kw spill-slot sharing;
  MP_STACK_LIMIT 8192→~2048 lever; multi-decl items after the first skip
  block_scope_decl (loud, not silent).

---

# (DONE in §5b above) Next session (§5a is DONE 2026-06-11 — **the minic multi-declarator init-hoisting hole is CLOSED** (the top §4z open track).  `T k, nf = 0;` inside a block/loop body used to run the init ONCE at function entry — `emit_local_multi_decl`'s per-declarator `expr()` calls fired at parse time, which is the entry block; the deferred-Stmt discipline the single-declarator rule got long ago ([[minic-decl-init-hoisting]]) never reached the list form.  Worse, a hoisted init EXPRESSION read its operands before they were live (`int k, *p = &g[i];` computed `&g[i]` at entry with `i` uninitialized).  Fix: `emit_local_multi_decl` and `emit_local_multi_decl_full` now RETURN a comma-chain of `=` nodes (the mk_local_array_init shape) instead of emitting; the stmt-context rule wraps it in `mkstmt(Expr,…)` so inits run in control-flow order, the four dcls-context call sites `expr()` it immediately (entry == lexical position at function top, so dcls semantics unchanged).  TWO SIBLING BUGS found during the reduce, both fixed: (1) the dcls `_full` path (decorated first declarator: `int a[5], b = 3;` at function top) silently DROPPED later declarators' inits — `b` read uninitialized stack; `_full` now chains op-0/'P' inits like the plain helper.  (2) `int a = 1, b = 2;` inside a BLOCK (first declarator carries the init) was a PARSE ERROR — no stmt-level `type IDENT '=' expr ',' init_decllist ';'` rule existed; added (deferred chain, block_scope_decl on the first declarator), grammar conflicts UNCHANGED at 115 s/r 0 r/r.  Probe `multi_decl_init_probe.c` (medium+compact; bug-loud: unfixed minic = hard compile error on the new form, and the nf-accumulation shape prints 0 1 3 vs 0 1 2).  Gate **246→248/248**; `make check` green; MP compact rebuild 108/108 TUs, body 717,168 **byte-identical** to §4z (shipping MP never hit these shapes — only §4z's debug counter did), so no Victor re-run needed.  FOR-loop init rules were audited and were ALREADY correct (they defer via mkfor comma-chains).  No designated successor — open tracks at the §5a notes' end.)

## 2026-06-11 §5a notes (multi-declarator init hoisting: defer the chain, not the emit)

### The fix (minic/minic.y, action-only + one new C helper; conflicts unchanged 115 s/r)
- New `multi_decl_chain_init(chain, v, init)` — appends `V(name) = init` to a comma-chain
  (the mk_local_array_init shape, one Node wrappable in mkstmt(Expr,…)).
- `emit_local_multi_decl` (plain first declarator) and `emit_local_multi_decl_full`
  (decorated first declarator) now return that chain; allocs still emit at parse time
  (entry block, QBE convention).  `_full` previously had NO init handling at all — later
  op-0/'P' declarators with `= expr` were silently dropped (e.g. `int a[5], b = 3;`).
- Call sites: stmt-context `type IDENT ',' ext_decllist ';'` wraps the chain in
  mkstmt(Expr,…) — control-flow order, re-inits per loop iteration; the four dcls-context
  sites (plain, `[N]` first, `()` first, fnptr first) `expr()` the chain immediately.
  SSA ordering note: dcls-context inits now emit AFTER the whole decl line's allocs
  instead of interleaved per declarator — allocs are side-effect-free, init order among
  declarators is preserved, gate goldens unmoved.
- NEW stmt rule `type IDENT '=' expr ',' init_decllist ';'` — `int a = 1, b = 2;` inside a
  block was a parse error before (dcls had the rule at minic.y:7803, stmt did not).  First
  declarator goes through block_scope_decl (rename stamps later uses); all inits in one
  deferred chain, source order.  Zero new conflicts.
- AUDITED-OK: the three C99 for-init rules already defer (mkfor comma-chain); the
  multi-scalar for-init (§1k) was never broken.  K&R/file-scope walkers untouched.

### Probe + verification
- `multi_decl_init_probe.c` (medium + compact): loop-body re-init (nf accumulation),
  hoisted-init-reads-dead-operand (`int k, *p = &g[i];`), first-declarator-init block form,
  dcls `_full` dropped init, side-effecting init in a never-taken branch (must not run) and
  in a taken branch (runs exactly once at the decl point), pointer-decorated later
  declarator in a loop.  Bug-loud vs unfixed minic: hard compile error.
- Gate **246 → 248/248**; `make check` green; minic rebuilt via the staleness-proof recipe.
- MP compact rebuild: 108/108 TUs, body 717,168 byte-identical → no Victor re-run.
  (Process note: `build-micropython.sh` without `--model=compact` fails fast now that
  MP_SPLIT_STACK=1 is the default — split stack requires a far-data model.)

### Open tracks (no §5b designated; carried from §4z)
- **Third GC area** if a consumer wants it: gcheap3.c clone (+~64 KB → body ~782 KB,
  ~42 KB ceiling margin) — mechanical; heap1 could also grow ~11 KB inside main_BSS.
- MicroPython `math` module (sqrtf + trig in softfloat.c, §4x recipe).
- The four older soft-float suites still medium-only in the gate; they pass under compact
  (§4x) — cheap thickening.
- Upstream sync: 3 commits (`c081897..e786f06`).
- Carried: `jmp_buf bufs[6]` latent minic note (§4v, unreduced); huge `_qbe_huge_add`
  ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup in the external tree; Kw spill-slot sharing;
  MP_STACK_LIMIT 8192→~2048 lever.  NOTE: multi-decl items after the first still skip
  block_scope_decl (a different-type shadow across blocks dies "double definition" —
  loud, not silent; same pre-existing behavior as the for-init list forms).

---

# (DONE in §5a above) Next session (§4z is DONE 2026-06-11 — **the GC heap is SPLIT and 2.3× bigger: MICROPY_GC_SPLIT_HEAP=1 + a second 65,024-byte area in its own far segment; total heap 49,152 + 65,024 = 114,176 bytes, Victor-verified byte-exact vs host python3** (user-designated track: "increase the heap size").  The flip itself was config + ~30 lines (mpconfigport `MICROPY_GC_SPLIT_HEAP (1)` + `MP_GC_HEAP2_SIZE (65024)`; new `ports/dos8086/gcheap2.c` holding `char mp_gc_heap2[...]` — its OWN TU because minic puts a TU's far BSS in one shared `<tu>_BSS` segment capped at 64 KB and main_BSS already holds heap[]+REPL buffers; main.c `gc_add()` after `gc_init()`; build-micropython.sh `MP_HEAP2_SIZE` knob + `-DMP_GC_HEAP2_SIZE` + PORT_SRCS).  py/gc.c's split-heap machinery is 8086-clean AS-IS (no qbe/emit change): uintptr_t is 32-bit under FAR_DATA, each area's pool lives inside ONE <64 KB segment so `gc_get_ptr_area`'s unsigned 32-bit far-pointer range compares are correct even against cross-segment garbage words, and BLOCK_FROM_PTR/PTR_FROM_BLOCK stay same-segment.  **THE BUG the bring-up found is a minic frontend hole: `extern char a[65024];` with a BARE-NUMBER dimension registered as a SCALAR** — the LR machine routes `IDENT [ NUM ]` through `ext_decllist`'s op-'B' node (kr_array_node), which the EXTERN list walkers didn't handle, so references LOADED the first byte instead of decaying to the address; `gc_add` received 0:0 and `gc_setup_area` zeroed the interrupt vector table (wedge between the C2/C3 boot markers).  A parenthesized dimension `[(65024)]` dodges it (reduces via the dedicated `'[' expr ']'` rule) — which is why this never bit before.  FIXED at 4 sites in minic.y: the file-scope `EXTERN type ext_decllist` walker, the function-local `dcls EXTERN type ext_decllist` walker, the file-scope non-extern multi-decl (`int a, b[10];` used to emit a WRONG-SIZE scalar global for b), and the dedicated sized-extern rule now records arraybytes so `sizeof` answers correctly.  Probe `extern_array_decay_probe.c` (medium + compact; bug-loud: unfixed minic dies "dereference of a non-pointer").  Gate **244→246/246**; `make check` green; grammar conflicts unchanged (action-only edits; baseline is 115 s/r — the "111" in older notes is stale).  Body 650,352 → **717,168** (+65,024 heap2 + ~1.8 KB split-heap gc code), ~107 KB under the ~824 KB ceiling.  **Victor: heap-split stress probe BYTE-EXACT vs host python3** (220×400 B = 88 KB live strings spilling deep into area 2, integrity-verified; 8 churn rounds across repeated MULTI-AREA collects; 120-string post-churn reallocation; graceful MemoryError-with-traceback verified at genuine exhaustion); feature-4t byte-exact; churn scale2 all correct + DONE; float probe byte-exact.  No designated successor — open tracks at the §4z notes' end.)

## 2026-06-11 §4z notes (split GC heap: 114 KB total; the extern-array-decay minic hole; DOSBox fast loop rediscovered)

**§4z executed the user-designated "increase the heap size" track via `MICROPY_GC_SPLIT_HEAP`,
and the bring-up surfaced one real minic frontend bug plus two process lessons worth keeping.**

### The change (external tree + build harness)
- `ports/dos8086/mpconfigport.h`: `MICROPY_GC_SPLIT_HEAP (1)`; `MP_GC_HEAP2_SIZE` default
  65024 (`#ifndef`-guarded so the build's `-D` wins).
- **`ports/dos8086/gcheap2.c` (new TU)** — `char mp_gc_heap2[MP_GC_HEAP2_SIZE];`.  One TU per
  extra area is the load-bearing trick: minic/asm_to_omf put a TU's far BSS in ONE shared
  `<tu>_BSS` segment (≤64 KB), and main_BSS already carries heap[] 49152 + repl_hist 4096 +
  repl_edit_saved 512.  The gc_add() reference from main.c keeps the segment alive under
  --gc-sections.
- `main.c`: `gc_add(mp_gc_heap2, mp_gc_heap2 + MP_GC_HEAP2_SIZE)` right after gc_init.
- `tools/build-micropython.sh`: `MP_HEAP2_SIZE` env knob → `-DMP_GC_HEAP2_SIZE` (all TUs),
  gcheap2.c in PORT_SRCS (108 TUs now).
- **Why the split-heap gc.c needs NO backend work** (checked before flipping): uintptr_t =
  unsigned long under FAR_DATA (stdint.h), so the `(uintptr_t)ptr & ~(BYTES_PER_BLOCK-1)`
  masks keep the segment word; each area is one array inside one far segment, so per-area
  pointer subtraction cancels segments exactly; `gc_get_ptr_area` bounds checks are §4s
  `cult/cule` unsigned 32-bit compares, and a pointer with ANY other segment word falls
  outside [S:lo, S:hi) because every pool spans <64 KB; the area struct that gc_add carves
  from the start of the new region is reached through ordinary far loads/stores.
- Sizes: body 650,352 → 717,168; total GC heap 114,176 (was 49,152).  DOS reports ~107 KB
  of load-ceiling headroom remaining.

### THE BUG — minic extern-array decay (minic/minic.y, 4 sites)
- First Victor boot wedged between C2 and C3.  Bring-up markers (temporary gz_s/gz_h prints
  in gc.c) showed `gc_add` receiving `start=0x00000018-0x18 = 0`, `end=0xFFFFFE00` (0 +
  65024 sign-extended): **`mp_gc_heap2` evaluated to a LOADED BYTE, not an address** — the
  area struct landed at 0:0 and gc_setup_area memset the IVT.
- Root cause chain: `extern char a[65024];` (bare NUM dim) does NOT reduce through the
  dedicated `EXTERN type IDENT '[' expr ']' ';'` rule — `IDENT '[' NUM ']'` is captured by
  `ext_decl`/`ext_decllist` (the K&R multi-name machinery) as an op-'B' node from
  kr_array_node, and BOTH extern list walkers handled only 'F'/'G'/'A'/'P': 'B' fell into
  the scalar else-branch (`t = base`, isarray=0).  `[(65024)]` parses via the expr rule and
  works — pure historical luck that every prior extern array in the tree had parens or no
  size.
- Fixes: (1) file-scope `EXTERN type ext_decllist ';'` — 'B' → IDIR + isarray=1 +
  var_set_arraybytes; (2) same in the function-local `dcls EXTERN type ext_decllist ';'`;
  (3) file-scope NON-extern multi-decl (`int a, b[10];`) — 'B' used to emit a one-element
  zero block AND register a scalar (wrong size + no decay), now emits `z total` + arraybytes;
  (4) the dedicated sized-extern rule now const_evals the dim into arraybytes so
  `sizeof(extern_arr)` stops answering pointer-size.  Action-only edits — conflict count
  unchanged (115 s/r baseline; the "111 s/r" in old notes is stale).
- **Probe `extern_array_decay_probe.c`** (medium + compact): bare-NUM extern, parenthesized
  extern, multi-name extern with sized array, function-local extern, `int ga, gb[10];`,
  pointer-identity (`name == &name[0]`), sizeof×4.  Single-TU linkable: uses emit at main's
  closing brace while symbols are still extern-state; real definitions follow at EOF
  (varadd upgrades extern→definition).  Bug-loud vs unfixed minic: hard compile error.
- Gate **244 → 246/246**; `make check` green.

### Verification (all green, shipping image body 717,168)
- **Victor `build/mp-heap-split-probe.py` BYTE-EXACT vs host python3**: 220 strings ×
  400 B = 88 KB live (≫ area-1's 47.6 KB pool — deep area-2 occupancy), per-element
  integrity, 8 churn rounds (each triggering multi-area collects observed via the
  bring-up markers in earlier runs), 120-string reallocation after the drop, DONE.
- Victor regressions: `mp-feature-4t.py` byte-exact; `mp-churn-scale2.py` 20..120 all
  correct + DONE; `mp-float-probe.py` byte-exact.
- Graceful exhaustion verified TWICE (DOSBox small config + Victor with an over-sized
  200-string build): scan-fail → collect → rescan → `MemoryError: memory allocation
  failed` with intact traceback and clean C5 exit — the gc returns NULL properly when
  both areas are genuinely full/fragmented.

### Process lessons (cost real wall-clock; keep)
- **The "hang" after the first collect was 5 MHz 8088 SLOWNESS.**  The original probe
  printed nothing between `tot` and `churn DONE`; 30 rounds × 50 string-builds exceeded
  even a 700 s window, reading as a hang.  Phase markers showed every collect healthy.
  Rule: Victor-bound probes MUST print progress per phase/round — silence is unreadable.
- **DOSBox fast loop for SPLIT-HEAP work**: `MP_HEAP_SIZE=8192 MP_HEAP2_SIZE=12288
  MP_STACK_SIZE=16384 tools/build-micropython.sh --model=compact` → ~600 KB image that
  LOADS IN DOSBOX (PROG.PY goes next to the exe; run-dos-exe.sh mounts the exe dir).
  Small areas exercise every split path (multi-area alloc, cross-area mark/sweep,
  area-list walks, exhaustion) in 30-second cycles.  This proved the GC end-to-end while
  the Victor runs were still ambiguous.
- **My own debug counter hit a LIVE minic bug**: `size_t k, nf = 0;` declared inside the
  per-area loop hoisted `nf = 0` to FUNCTION ENTRY (SSA: one `storew 0, %nf`), so nf
  accumulated across areas and printed an impossible 1539-of-999.  This is the
  [[minic-decl-init-hoisting]] class surviving in the MULTI-DECLARATOR form (`T a, b = X;`
  inside a block) — the earlier fix covered the single-declarator stmt rule.  NOT fixed
  this session (debug-only victim); reduced + documented as an open track.

### Open tracks (no §5a designated)
- **minic multi-declarator init hoisting** (NEW, from this session's debug code): `T a,
  b = 0;` inside a block/loop body runs the init once at function entry, not per
  iteration/at the decl point.  Single-declarator form was fixed long ago
  ([[minic-decl-init-hoisting]]); the list form (`emit_local_multi_decl*`?) was not.
  Reduce: `for(...){ size_t k, nf = 0; ... }` — nf keeps its prior value.
- **Third GC area** if a consumer wants it: gcheap3.c clone (+~64 KB → body ~782 KB,
  ~42 KB ceiling margin) — mechanical now; also heap1 could grow ~11 KB inside main_BSS.
  GC pause cost grows with area count (gc_get_ptr_area walks the list per scanned word).
- MicroPython `math` module (sqrtf + trig in softfloat.c, §4x recipe).
- The four older soft-float suites still medium-only in the gate; they pass under compact
  (§4x) — cheap thickening.
- Upstream sync: 3 commits (`c081897..e786f06`).
- Carried: `jmp_buf bufs[6]` latent minic note (§4v, unreduced); huge `_qbe_huge_add`
  ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup in the external tree; Kw spill-slot sharing;
  MP_STACK_LIMIT 8192→~2048 lever.

---

# (DONE in §4z above) Next session (§4y is DONE 2026-06-10 — **the emit-bracket audit is now a STANDING TOOL and it found + fixed the §1h two-div-one-call bug** (user-designated track).  New machinery: `QBE_EMIT_CHK=1` makes `i8086/emit.c` precede every emitted IR instruction with `; CHK <op> to=<dest> live=<regs>` carrying the EXACT post-rega GPR live-after set (per-function CFG fixpoint over {ax,cx,dx,bx,si,di}; ret blocks read AX/DX; the only implicit pair-use is the Kl `copy R1` call-result — `argcls` can NOT discriminate because `Km == Kl` lies about address operands on a 16-bit target), plus `; CHKT` terminator markers and a `cons=` cross-check of the §2w conservative tracker; off-mode output byte-identical.  `tools/check_emit_brackets.py` symbolically executes each marked region (symbolic regs, push/pop stack, tracked [bp+N] cells, calls clobber AX/CX/DX per ABI) and flags (a) any live non-dest GPR whose final value is not the entry value, (b) a register DEST that ends holding its entry value (the §4x pop-over-the-result shape), (c) ES/DS not entry-valued (DGROUP invariants), (d) a dropped/malformed Oswap exchange.  VALIDATED by reverting the §4x fixes: catches all three emit-side shapes (Ocmps/cnes/stosi CX-dest incl. in real objfloat divmod, Ocast AX).  `tools/run-emit-audit.sh` sweeps 107 MP TUs (compact) + every gate probe under its gate model (~440 asm, ~110k regions).  **THE FIND: Kw `Odiv`/`Orem`/`Oudiv`/`Ourem` clobber BOTH AX (dividend staging + quotient) and DX (cwd / xor + remainder) with NO liveness bracket** — the §1h "two divisions feeding one call corrupt the first result" found-not-fixed bug, 21 live-clobber sites in the shipping MP image (mp_format_mantissa = every float print, mp_map_lookup = every dict access, mp_lexer_to_next, gc, objint, ringbuf...).  Fixed with liveness-gated dest-skipped push/pop brackets (+ slot-dest result stores the old code silently lacked).  Probe `div_live_clobber_probe` (medium+compact, verified bug-loud: y=4 printed 3 unfixed); audit corpus CLEAN after fix; gate 242→244/244; make check green; MP body 650272→650352 (+80 B); Victor float probe + feature-4t byte-exact, churn scale2 + gen sweep clean.  No designated successor — open tracks at the §4y notes' end.)

## 2026-06-10 repo-state update (post-§4y housekeeping — PR #24 merged; two stale open-track notes corrected)

- **PR #24 merged** (`97376dc`): the 41 commits §3p→§4y (soft-float campaign, churn-GC
  root-cause, per-fn gc-sections, split stack, Kl slot coloring, FLOAT-on-Victor, emit-bracket
  audit) are now on GitHub master.  Local master fast-forwarded — local and GitHub are in sync.
- **The 211-commit upstream-qbe rebase is DONE and has been since 2026-06-06** — PR #23
  ("[codex] Test upstream QBE rebase", merge `a6ef88d`) landed it; `amd64/winabi.c` etc. are
  in-tree.  The "upstream rebase" open-track bullet repeated below §4y (and in CLAUDE.md) was
  stale.  As of 2026-06-10 only **3 newer upstream commits** (`c081897..e786f06` on
  `upstream/master`) are unmerged — the remaining track is a small periodic sync, not a
  campaign.
- **The "stevie build broken at hexchars.c" §4y bullet was WRONG** — verified 2026-06-10:
  `tools/build-stevie.sh --exe` compiles 24/24 TUs and links (146,672 bytes), hexchars.c
  rebuilt fresh (not cached, empty .err), and the user interactively verified stevie on the
  real Victor.  The "gate uses a STALE exe" half was also wrong: `test-dos.sh::run_stevie_size`
  rebuilds via build-stevie.sh before measuring.  hexchars.c's `chars[]` initializer is fully
  braced — the claimed brace-elision construct doesn't even exist there.  Likely a transient
  mid-§4y observation (stale-minic class, see [[minic-make-staleness]]) jotted down and never
  re-verified.
- The §4y open-tracks list below is edited accordingly.

## 2026-06-10 §4y notes (emit-bracket audit: exact-liveness CHK markers + symbolic checker; the Kw div/rem AX/DX hole closed)

**§4y turned the recurring "emit handler clobbers a register rega owns" bug class (§2l, §4r,
§4t, §4x×2) into a MECHANICAL check, and the very first full sweep found the oldest
documented open codegen bug.**

### The instrumentation (i8086/emit.c, QBE_EMIT_CHK=1, off-mode byte-identical)
- Exact GPR liveness post-rega: per-function CFG fixpoint (`chk_fixpoint`) + per-block
  backward walk; kills = register dest / call (AX,CX,DX); uses = any RTmp/RMem operand;
  ret blocks read {AX,DX}; Jjnz reads its register cond.
- **Modeling lessons (each cost a false-positive class):** (1) the §2w blanket "Kl touches
  AX/DX" rule manufactures phantom DX liveness — it deliberately over-approximates so save
  brackets stay put; the audit needs exact, so the only implicit pair-use kept is
  `Ocopy Kl from RAX` (call result in DX:AX).  (2) `argcls()` CANNOT be used to find pair
  args: `Km == Kl`, so a store/load ADDRESS in AX reads as a "Kl arg" — on i8086 near-data
  an address is 16-bit, no DX half.  (3) `cons=` in the marker prints the conservative
  tracker for cross-checking: conservative ⊂ exact at any point = a checker bug (it found
  two of mine).
- `; CHKT <jmptype>` markers audit terminator emission too (the historical Jjnz-spilled-cond
  AX clobber shape).

### The checker (tools/check_emit_brackets.py) + driver (tools/run-emit-audit.sh)
- Symbolic execution per region: register symbols, push/pop stack, `add/sub sp,N`
  adjustment, tracked direct `[bp+N]` cells (slot-roundtrip saves), 8-bit subreg writes
  clobber the parent, `call` clobbers AX/CX/DX and preserves BX/SI/DI/ES/DS (inductive
  cdecl invariant), linear scan over local labels (brackets are straight-line).
- Four rules: live non-dest GPR must survive; a REGISTER dest must NOT end entry-valued
  (the §4x `pop cx` over the result — caught only by this rule; dest-destroyed, no
  bystander); ES/DS entry-valued always; Oswap must be a clean 2-reg exchange (an EMPTY
  swap region = the silently-dropped slot-swap, flagged as DROPPED).
- `asm` regions skipped (user inline asm declares its own clobbers).
- Validation: with the §4x emit fixes reverted, the checker flags clts/cnes/stosi CX-dest
  (incl. `_mp_obj_float_divmod` in real objfloat) and the Ocast AX clobber.  With them
  applied: clean.
- Driver sweeps build/mp-link/*.ssa (compact) + every `tools/test-dos.sh` RUNTIME_TESTS
  entry rebuilt under its own model with the gate's exact flags (QBE_FAR_STATIC_DATA /
  --softfloat / --split-stack — model PAIRING matters: a compact .ssa run through
  `qbe -m medium` is garbage).

### THE FIND — Kw div/rem clobber live AX and DX (i8086/emit.c)
- 21 sites in the MP image where a live temp sat in AX or DX across an inline 16-bit
  division: `mp_format_mantissa` (every float print), `mp_map_lookup` (every dict access),
  `mp_lexer_to_next`, `gc_*`, `mp_int_format_size`, ringbuf, `repl_hist_add`,
  `mp_print_strn`...  This is `[[i8086-two-div-one-call-clobber]]`, documented
  found-not-fixed since §1h.  The handlers had the §1-era divisor-staging fix (a divisor
  IN AX/DX) but never protected a BYSTANDER temp in AX/DX: `mov ax, dividend` +
  `cwd`/`xor dx,dx` + quotient/remainder writes.
- **Fix**: liveness-gated dest-skipped AX/DX push/pop brackets (the §2z/§4x house
  discipline), one shared result-move tail per handler (which also adds the RSlot dest
  stores the old code silently LACKED — a spilled Kw div result previously went nowhere),
  signed + unsigned paths.
- **Probe `div_live_clobber_probe.c`** (medium+compact): two-divs-feeding-one-call,
  3-result printf, digit-extraction loop (the format_mantissa shape), quotient live across
  a second division, and a register-pressure case (5 locals live across div+rem — the CHK
  markers confirmed `live=ax,cx,dx,bx,si,di` on that div).  Verified bug-loud with the
  brackets neutered (y=4 prints 3).  Gate 242→244/244.
- MP body 650272 → 650352 (+80 B — the brackets are liveness-gated so almost free).

### Notes / leftovers
- The audit found NOTHING else across ~110k regions — the §2-era bracket campaign
  (§2l/§2w/§2x/§2z/§3a/§4r/§4t/§4x) plus this close out the known surface.  The marker
  machinery + checker stay in-tree; run `tools/run-emit-audit.sh` after any emit.c change.
- The `cons=` field stays in the markers (cheap, audit-only) for future triage.
- Checker scope limits (documented in the file): flags-register liveness between a compare
  and its consuming Jjfi* terminator is NOT audited; `asm` regions skipped; liveness of a
  reg whose value is "stored to a slot and reloaded by a LATER IR instruction" is per-IR
  honest (rega-visible) so no gap there.
- Final audit run: **339 files / 112,443 regions / 0 violations / 0 build failures**
  (107 MP TUs compact + every gate probe under its gate model with the gate's flags).

### Open tracks (no §4z designated; list corrected 2026-06-10 — see repo-state update above)
- The four older soft-float suites (softfloat/softlibm/softtrig/double_float) still gated
  medium-only; they PASS under compact (§4x bisect) — cheap gate-thickening.
- MicroPython: `math` module (needs sqrtf + trig in softfloat.c — recipe per §4x
  discussion); heap expansion via MICROPY_GC_SPLIT_HEAP (multiple ≤64 KB areas).
- ~~stevie build broken at hexchars.c~~ — **STALE, removed**: build verified 24/24 + linked
  + user-verified interactively on Victor 2026-06-10; the gate's stevie check rebuilds
  (it never used a stale exe).  See the repo-state update above.
- Upstream sync: the 211-commit rebase landed via PR #23 (2026-06-06); only 3 newer
  upstream commits (`c081897..e786f06`) pending — small periodic sync.
- Latent minic note (§4v, NOT reduced): `jmp_buf bufs[6]` array-of-jmp_buf cross-frame
  longjmp; huge `_qbe_huge_add` ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup; Kw spill slots
  never share; MP_STACK_LIMIT 8192→~2048 lever.

---

# (DONE in §4y above) Next session (§4x is DONE 2026-06-10 — **MicroPython FLOAT is ON and Victor-verified** (user-designated track): `MICROPY_FLOAT_IMPL_FLOAT` flipped per the §4a recipe; body 650272 — the float delta is only **+38 KB** over §4w's 612048 at per-function gc-sections granularity (not §4a's 59 KB at 56 KB granularity), ~174 KB under the ~824 KB ceiling.  First-ever float EXECUTION (§4a only ever linked) flushed out FOUR real toolchain bugs, all fixed + gate-pinned: (1) **minic `coerce_arg` had no int↔float argument conversion** (C11 6.5.2.2p7) — parsenum.c's `powf(5, -dec_exp)` passed raw int words as binary32 denormals, sf_powf saw `powf(eps,eps)≈1.0`, so EVERY float literal mis-parsed (1.5→7.5) and mp_parse hung on Victor; fix = swtof/sltof int→float-param, stosi float→int-param (probe `float_arg_coerce_probe`).  (2) **i8086 Ocmps/Ostosi emit brackets pushed/popped CX unconditionally** — a compare/convert result rega placed in CX was popped over with stale garbage: objfloat's modulo sign-fix misfired (`7.5 % 2.0`→3.5) and `bool(0.0)`→True; fix = dst_in_cx skip, mirror of the AX/DX skips (probe `float_cmp_cx_probe`, verified bug-loud).  (3) **load.c forwarded a stored float through a `Kw` cast** — lossless when w=4B, a TRUNCATION on i8086 (w=2B): medium-model `mp_decimal_exp` read its float as 16 bits (+ `loadsz` claimed a Ks `Oload` is wordsz=2 bytes); fix = direct `cast Ks→Kl` when `T.wordsz==2` + `loadsz` Ks=4 (target-general, stock targets byte-identical).  (4) **i8086 soft-float `Ocast` slot→slot used AX as scratch with no liveness bracket** — clobbered a live `dec_exp` in AX → `powf(5, -16624)` → inf; fix = `g_live_ax_after` bracket, same discipline as the Kl Ocopy path.  Bugs 3+4 were caught by the GATE (the new probe's medium entry failed while compact passed — far-data routes around load-forwarding).  **Full 29-line float probe byte-exact vs host python3 on real Victor**; feature-4t byte-exact; churn scale2 + gen sweep clean; gate 238→242/242; make check green.  No designated successor — open tracks at the §4x notes' end.)

## 2026-06-10 §4x notes (FLOAT flip: +38 KB, four real bugs — minic arg conversion, emit CX clobber, load.c Ks truncation, Ocast AX clobber)

**§4x flipped `MICROPY_FLOAT_IMPL` → FLOAT (the §4a/§4t recipe) and the bring-up found the
recipe itself was fine — what broke was code that had NEVER EXECUTED: §4a verified the LINK
only.**  Sequence: flip → measure (fits easily) → first Victor run hung in mp_parse → bisect
→ minic arg-conversion gap → fixed → 27/29 probe lines pass → two float-compare failures →
backend CX clobber → fixed → 29/29 byte-exact → the GATE then failed the new probe's
medium entry (compact passed) → load.c Ks-forwarding truncation + Ocast AX clobber → fixed
→ gate green.

### Bug 3 — load.c forwards a float store through a Kw cast (load.c, target-general)
- Gate caught it: `float_arg_coerce_probe` medium FAILED its two `decimal_exp` lines with
  `7f800000` (inf) while compact passed — far-data stores (`storefs`) are not load-forwarded,
  so only MEDIUM exercises the forwarding path.  (Process note: the original "medium golden"
  run had actually run the compact exe — medium and compact builds share
  `build/examples/<name>/`, and the golden was captured after a compact rebuild.  The gate
  rebuilds per entry and told the truth.)
- `load.c::cast()` widens a forwarded Ks value to Kl via `cast Ks→Kw; extuw` — lossless
  when w is 4 bytes, but on i8086 (w=2B) the Kw cast TRUNCATES the float to its low 16
  bits: post-isel showed `%ld =w cast %t0; =l extuw` — sign+exponent gone.  Fix: when
  `T.wordsz==2`, emit a direct `cast Ks→Kl` (both 32-bit; the §3q emit handles Ocast in
  the Kl move block).  Also `loadsz()` claimed a Ks `Oload` is `T.wordsz` bytes (2 on
  i8086) — now returns 4, the mirror of storesz's `Ostores` case.  Both changes are
  byte-identical on stock targets (wordsz==4 ⇒ same values).
### Bug 4 — i8086 soft-float Ocast slot→slot clobbers AX (i8086/emit.c)
- With bug 3 fixed the IR was right but medium still returned inf.  Hand-trace found it:
  the Ks-result `Ocast` slot→slot branch copies through AX with NO liveness bracket; rega
  had `dec_exp` live in AX across it, so the @l7 negate computed `-(float bits)` = -16624
  and `powf(5, -16624) = 0` → division → inf.  (The print-instrumented variant "worked" —
  layout-sensitive, the §4q heisenbug class.)  Fix: `g_live_ax_after` push/pop bracket,
  exactly the Kl Ocopy discipline.  Pre-existing since §3q — first exposed now because
  float literals + union puns + live ints across casts only EXECUTE under FLOAT.
- The probe's `dexp1`/`dexp2` lines pin both fixes under medium in the gate.

### The flip (external micropython tree + genhdr)
- `ports/dos8086/mpconfigport.h`: `MICROPY_FLOAT_IMPL_FLOAT` + `MICROPY_PY_BUILTINS_COMPLEX (0)`
  + `MICROPY_FLOAT_USE_NATIVE_FLT16 (0)`; the dead "won't fit" comment block rewritten.
- `ports/minimal/build/genhdr/qstrdefs.generated.h`: the two §4a QDEF0 appends
  (`float` 17461/5, `__float__` 28725/9; djb2 hashes re-verified).
- `MICROPY_FLOAT_FORMAT_IMPL` defaults to APPROX under IMPL_FLOAT, so `mp_large_float_t`
  = float and the mantissa is uint32_t — no 64-bit anywhere.
- **Size: body 612048 → 650272 (+38 KB), image 669408** (final, all four fixes in) —
  per-function gc-sections strips the unused soft-libm/objfloat surface far better than
  §4a's 56 KB-granularity 59 KB estimate.  ~174 KB headroom remains.

### Bug 1 — minic `coerce_arg` int↔float argument conversion gap (minic/minic.y)
- Victor run of ANY float literal hung between D1 and D2 (inside mp_parse).  Bisect:
  the four medium-only soft-float suites (softfloat/softlibm/softtrig/double_float)
  all pass **golden-exact under compact** in DOSBox → the `_sf_*` layer was innocent.
- Standalone repro of py/parsenum.c's float path (`build/parsefloat_probe.c`, compact,
  DOSBox 30-second loop): `mp_decimal_exp(15.0f, -1)` returned 7.5 — the
  `res.f /= powf(5, -dec_exp)` was a NO-OP.  The SSA showed why:
  `call $sf_powf(w 5, w %t46, ...)` — **integer args passed raw to float params**.
  `coerce_arg` explicitly bailed on any float involvement ("a real conversion, not a
  width fix"), so the callee read two denormals (~1e-44) and `powf(eps, eps) ≈ 1.0`.
  Every float literal parsed to mantissa·2^dec_exp instead of mantissa·10^dec_exp;
  the Victor hang was downstream of the same garbage (gone with the fix).
- **Fix**: `coerce_arg` now implements C11 6.5.2.2p7 — int arg → float param emits
  `swtof`/`sltof` (by arg KIND); float arg → int param emits `stosi` with the param's
  result class (`l` for long — the §3z Ostosi-Kl path; `dtosi` FAILS QBE's typecheck
  since no Kd exists on this target — first attempt taught that); float→float returns
  unchanged.  Covers the direct (fnproto) AND indirect (fpproto) call paths — both go
  through coerce_arg.
- **Probe `float_arg_coerce_probe.c`** (medium + compact, --softfloat, one shared golden):
  int Con/var/negative/long → float param; both arg positions mixed; float → int and
  long params; powf(5,1)/powf(10,2); the exact parsenum decimal_exp dance; int→int
  regression.  All values dyadic-exact, printed as IEEE bit patterns.

### Bug 2 — i8086 Ocmps/Ostosi CX-dest clobber (i8086/emit.c)
- With bug 1 fixed: 27/29 float-probe lines byte-exact on Victor, but `7.5 % 2.0` → 3.5
  and `bool(0.0)` → True.  objfloat.c SSA was CORRECT (`clts/clts/cnew`); the generated
  asm had the smoking gun: `mov cx, ax` (store_ax_to, dest=CX) immediately followed by
  `pop cx` — the soft-float compare lowers to `call far _sf_cmp` at EMIT time with a
  push/pop CX bracket that skipped AX and DX when they were the destination but pushed
  CX UNCONDITIONALLY.  A compare result rega placed in CX was overwritten with the stale
  pre-compare CX; `(lhs<0) != (rhs<0)` then compared against garbage and the sign-fix
  `lhs += rhs` fired on positive operands (1.5+2.0=3.5).  Same hazard in Ostosi/Ostoui
  (`mov cx, ax` via the generic reg-dest move, then `pop cx`).
- **Fix**: `dst_in_cx` skip for the CX bracket in both handlers, the exact mirror of the
  existing dst_in_ax/dst_in_dx skips.  (The Oswtof family stores to Ks slots — always
  slot-resident — so it has no reg dest and is safe; the Kl compare family already used
  kl_save_axdx which skips the dest.)
- **Probe `float_cmp_cx_probe.c`** (medium + compact, --softfloat): the objfloat modulo
  dance VERBATIM (fmodf + copysignf + the sign-fix compare) over 5 sign combinations,
  the bool(0.0) shape, and a float→int convert pair.  **Verified bug-loud against the
  unfixed emit**: m3 lost its sign-fix (1.5 vs -0.5), b0 printed garbage `12`, c0 798
  vs 298.  Caveat pinned in the gate comment: rega-dependent trigger ⇒ green probe is
  necessary-not-sufficient; the real guard is the dst_in_cx skip itself.

### Verification (all green)
- **Real Victor: `build/mp-float-probe.py` (29 lines) BYTE-EXACT vs host python3** —
  add/sub/mul/div, true division (`1/2` = 0.5), floordiv (incl. negative), modulo, neg,
  abs, pow (incl. negative exponent), float and mixed int/float comparisons, int()/float()
  conversions, round (incl. ndigits), float() string parsing, literals (1e3/2.5e-1),
  %-format width/precision, .format, inf/nan semantics, min/max/sum, list comprehension,
  bool, dict float keys, user fn.  Values chosen dyadic-exact so host doubles print
  identically.  Clean D4/C5.
- Real Victor regressions on the shipping image: `mp-feature-4t.py` byte-exact;
  `mp-churn-scale2.py` churn(20..120) all correct + DONE; `mp-gen-sweep.py` 4–30 all
  correct + DONE (the §4w frontier unaffected).
- Gate **238 → 242/242** (float_arg_coerce_probe + float_cmp_cx_probe, each
  medium + compact); `make check` green.
- DOSBox note: the 669 KB image no longer fits DOSBox's 640 KB — the fast loop for float
  work is standalone compact probes (parsenum repro ran in 30-second cycles).

### Open tracks (no §4y designated)
- The four older soft-float suites (softfloat/softlibm/softtrig/double_float) are still
  gated medium-only; they PASS under compact (verified this session, used as the bisect
  baseline) — adding compact entries is cheap gate-thickening if wanted.
- Latent minic note (§4v, NOT reduced): `jmp_buf bufs[6]` array-of-jmp_buf cross-frame
  longjmp misbehaved; possibly the §2m array_vartyp stride family.  Reduce before trusting.
- huge `_qbe_huge_add` ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup in the external tree;
  211-commit upstream-qbe rebase.
- Kw spill slots still never share (lazy `slot()` carve) — small frame lever, irrelevant
  for MP.
- `MP_STACK_LIMIT` headroom still 8192 (§4w note: could drop to ~2048 for ~9 more levels).

---

# (DONE in §4x above) Next session (§4w is DONE 2026-06-10 — the generator frame diet LANDED via Kl/Ks stack-slot COLORING (user-designated track): `spill.c::colorklslots()` assigns the i8086 forced Kl/Ks slots by interference-graph coloring so disjoint live ranges SHARE slots, instead of one private 2-word slot per temp.  mp_execute_bytecode: 1261 Kl temps / 12 colors → frame 5464 → 472 bytes (11.6×); generator resume ~5772 → ~665 B/level; Victor frontier 8 → ~80 levels (75 clean, 85 = clean CAUGHT RuntimeError, sweep 4–30 all correct + DONE).  Bonus: MP body 632112 → 612048 (−20 KB; small frames re-enable 8-bit [bp-N] displacements).  Gate 236 → 238/238 (new kl_slot_color_probe medium+compact); make check green; feature-4t byte-exact; churn scale2 clean.  No designated successor — open tracks at the §4w notes' end.)

## 2026-06-10 §4w notes (Kl/Ks slot coloring: the generator frame diet — one spill.c pass, 10× depth)

**§4w attacked the §4v-measured ~5772 B/level generator-resume cost and the entire cost
turned out to be ONE allocation policy.**  Under i8086, every Kl (32-bit long / far-pointer)
and Ks temp is forced slot-resident ([[i8086-kl-load-loses-high]] — rega has no register-pair
concept), and `spill.c`'s eager pass gave EVERY such temp a private 2-word slot for the whole
function.  Frame size therefore grew with the Kl temp COUNT: `mp_execute_bytecode` has 1261
Kl temps = 5044 bytes of slots on a 5464-byte frame, while its **maximum simultaneous Kl
liveness is ~10** (instrumented measurement; real C locals are only 541 bytes of allocas).

### The change (`spill.c::colorklslots()`, i8086-only, replaces the eager carve loop)
- Builds the interference graph over candidate temps (Kl/Ks, `slot == -1` — i.e. excluding
  ABI-aliased params (negative slots) and isel fast-local alloca temps) via one backward
  liveness walk per block off filllive's `b->out` (still pristine at that point; spill's main
  loop hasn't replaced in/out yet), then greedy-colors and assigns `slot = locs + 2*color`.
  `slot4 = slot8 = 2*ncolors` so later lazy Kw spill slots continue past the colored region.
- **Conservative interference rules** (each is load-bearing):
  - a def interferes with everything live across it (standard);
  - a def interferes with its own instruction's args — the i8086 emit handlers are
    multi-instruction sequences that may write the dest slot's two words while still reading
    arg slots;
  - a phi def interferes with the block's live-in, the block's other phi defs, AND every phi
    argument of the block: rega's `pmgen` orders edge parallel-copies by comparing refs
    (RSlot included), and a slot shared between a phi def and a phi arg of the same block
    could force a slot↔slot cycle that emit's `Oswap` handler does not implement (it only
    swaps registers — verified, it silently emits NOTHING for slot operands).
- Sharing is safe by construction everywhere else: slot writes happen only at the owning
  temp's def (interference covers them), `pmgen`'s ref-equality ordering turns any remaining
  src/dst slot aliasing into correct read-before-write sequencing, and Ocopy Kl slot→slot
  already exists (the §2x param self-copy path).
- `QBE_SLOT_DBG=1` env prints per-function `kl=<n> colors=<c>` stats.

### Measured results
- `mp_execute_bytecode`: **frame 5464 → 472 bytes** (12 colors); `mp_obj_gen_resume`
  112 → 40; `gen_wrap_call` 92 → 68; `build_slice_stack_allocated` 118 → 46.
- Full MP rebuild (107/107 TUs): body **632112 → 612048 (−20 KB)**, code 452461 → 434851 —
  the frame diet collaterally shrinks CODE because small `[bp-N]` offsets fit 8-bit signed
  displacements again.
- **Victor, all green**: `mp-feature-4t.py` byte-exact; `mp-churn-scale2.py` churn(20..120)
  all correct + DONE (GC clean over the completely-reshaped frames); `mp-gen-sweep.py`
  **4–30 ALL correct + DONE** (the §4v image errored at 9); targeted frontier probe:
  **gc(75) = 2850 clean, gc(85/95/105) = RuntimeError CAUGHT by try/except, then DONE** —
  the stack check + exception unwind work perfectly at the new cliff.  Per-level cost
  ≈ 53248 (MP_STACK_LIMIT) / ~80 ≈ **~665 B/level** (was ~5772): **~10× depth**.
- Gate **236 → 238/238** (new `kl_slot_color_probe.c`, medium + compact: 14 longs live
  across a call, disjoint chains that DO share, a loop-carried long swap cycle pinning the
  phi no-share rule, longs live across in-loop calls, pointer ping-pong).  `make check`
  green (coloring is gated behind the i8086-only force_kl_slot flag; other targets
  byte-identical by construction).

### Notes / leftovers from the session
- `MP_STACK_LIMIT` headroom is still 8192, sized in §4v for one ~5.6 KB overshoot frame;
  with ~665 B frames it could drop to ~2048 and buy ~9 more levels.  Not changed — margin
  is cheap and the frontier is no longer the bottleneck.
- Probe scripts kept (untracked): `build/mp-gen-frontier-4w.py`, `build/mp-gen-sweep-deep.py`.
- The deep sweep (4..200) timed out at gc(48) on wall clock, not stack — the sweep is O(n²)
  resumes on a 5 MHz 8088; use targeted depths for frontier hunting.

### Open tracks (no §4x designated)
- **FLOAT** (recipe §4a/§4t, ~59 KB delta vs now ~210 KB headroom).
- Latent minic note (§4v, NOT reduced): `jmp_buf bufs[6]` array-of-jmp_buf cross-frame
  longjmp misbehaved; possibly the §2m array_vartyp stride family.  Reduce before trusting.
- huge `_qbe_huge_add` ≥0x8000 gap (§4i); `-DMP_DBG_*` cleanup in the external tree;
  211-commit upstream-qbe rebase.
- Possible follow-on in the same vein: Kw spill slots still never share (lazy `slot()`
  carve); irrelevant for MP (Kl dominates) but a small frame lever elsewhere.

---

# (DONE in §4v below) Next session (§4v is DONE 2026-06-10 — split stack LANDED + Victor-verified, and the bring-up EXPOSED then FIXED the real generator-depth story: per-resume C-stack cost is ~5.6 KB (NOT ~2 KB), every pre-§4v "frontier" was silent stack overflow into libstub's unused `_heap_buf`, and MICROPY_STACK_CHECK=1 now raises a clean RuntimeError at the true limit.  MP_STACK_SIZE default 61440 (the 16-bit SP is the only cap left).  Gate 236/236; feature-4t byte-exact; churn scale2 clean; recsum(60) clean.)

## 2026-06-10 §4v notes (split stack SS≠DS landed end-to-end; the generator "frontier" was always overflow-UB; stack check ON)

**§4v shipped the user-designated SS≠DS split across all four toolchain layers, and the
MicroPython bring-up turned into a root-cause hunt that REWROTE the §4c/§4u stack-depth
story.**  Headline numbers: MP_STACK_SIZE 24576 → **61440** (the DGROUP cap is gone; the
16-bit SP is the only cap), measured generator-resume cost **~5772 B/level** (probe-derived,
below), true frontier at 61440 ≈ 8 levels with a **clean RuntimeError** at the cliff
(MICROPY_STACK_CHECK=1), body 632112 (~190 KB under the ~824 KB ceiling).

### 1. The toolchain split (qbe + omf_link + libstub + harnesses)
- **`tools/omf_link.py --separate-stack`** — MZ header SS = the STACK segment's own
  para_base, SP = stack size; layout byte-identical to default (the flag only changes the
  header words + swaps the 64 KB check to data+bss-only).  Default linking byte-identical
  (relink-at-same-args cmp'd IDENTICAL).
- **`qbe -s`** (main.c flag → `T.splitstack`, i8086 far-data models only) — new
  `i8086/emit.c::near_seg()` puts an `ss:` override on every register-indirect NEAR deref:
  the isel Kw-narrowing of Oaddr-of-slot addresses (`lea bx, [bp-N]; mov [bx]`) is
  DS-relative on stock 8086, correct only while SS==DS.  Audited: under far-data EVERY
  register-held near address is stack-derived (globals are FARSTORAGE, all C pointers are
  far, no `__near`), pinned by a transitive setter-trace over all 107 generated MP TUs
  (2620 `[ss:` sites, every one slot/lea-derived).  Applied in emit_memref, the emitf
  `Ref:`/`%M` RMem/RTmp cases, and the Kl Oload/Ostorel register-indirect paths; RSlot
  `[bp±N]` is SS-relative by hardware and RCon `[_sym]` stays DS — no prefix.  Default
  (-s absent) output byte-identical; `make check` green.
- **`tools/libstub_to_exe.py`** — new loader-relocated `_dgroup_para: dw DGROUP` word in
  the code segment ([cs:]-readable while DS is swapped away).  Fixed every "SS as a synonym
  for DGROUP" idiom: `_malloc`/`_far_fopen` segment returns (`mov dx, ss` →
  `mov dx, [cs:_dgroup_para]`), 8× `push ss/pop ds` DS-restores (`mov ds, [cs:_dgroup_para]`),
  `_far_sprintf`'s `[ss:_spr_*]` state reads (brief DS=DGROUP window).  THE PLAN'S SURVEY
  UNDER-COUNTED a whole class: **stack-resident INT 21h DS:DX buffers** (`_far_fputc`
  scratch byte, `_far_puts` CRLF word, `_far_printf`/`_far_fprintf` output buffers) need a
  DS=SS bracket, and `_far_sprintf`'s engine read its fmt scratch via `lodsb` (DS) and its
  varargs via `[bx]` (DS) — fmt scratch moved to a DGROUP static (`_fsp_fmtbuf`; printf was
  never reentrant anyway), vararg reads got `[ss:bx]`.  Conversely the survey OVER-listed
  `mov ax, ss` at the printf dest-formation sites (the dest is a [bp-N] STACK buffer — SS is
  CORRECT there) and far-setjmp (stores no SS at all; SS is process-constant).  libstub.asm's
  own `push ss/pop ds` (int86x/intdosx) are near-pointer medium-only paths — left alone.
- **Harness plumbing** — `build-example.sh --split-stack`; `build-micropython.sh` +
  `recompile-mp-tu.sh` default `MP_SPLIT_STACK=1` (and their MP_STACK_SIZE defaults are now
  BOTH 61440 — found+fixed a stale 16384 in recompile-mp-tu.sh that §4u missed, which would
  have silently relinked fast-loop images at the wrong size).
- **New gated probe `split_stack_probe.c`** (compact + large, built with --split-stack):
  escaped `&local` writes, stack-struct member chains, far_sprintf into a stack buffer,
  fn-ptr callback with stack ptr, setjmp/longjmp, malloc-seg == DGROUP-seg != stack-seg
  (ok8 is the discriminator: a default link prints `ok8 0`).  Gate **234 → 236/236**.

### 2. THE INVESTIGATION — what "broke" on Victor was never the split
- Step A equivalence run at 24576: feature-4t byte-exact, churn scale2 clean (GC fine under
  split), but `mp-gen-sweep.py` died at gc(5) with garbage serial + NameError-with-corrupt-
  qstr (later variants: empty-text exceptions, machine REBOOTS) — while the IDENTICAL image
  relinked without --separate-stack reached the §4u "frontier 11".
- A long forensic chain (MAME reset-vector breakpoint with register tracelog + stack-window
  `save` + BP-chain walk + full instruction trace; the **none.cpp one-line patch from §4p is
  now APPLIED and `tools/run-victor-wp.sh` WORKS** — wait_for_debugger now runs
  process_source_file, so headless `-debugscript` wpset/bpset/tracelog/save all fire) kept
  landing post-derailment (wild PCs executing the vector table; DOS internals poisoned).
- **The breakthrough was cheap**: the failing image FITS IN DOSBOX (612 KB loads!), turning
  5-minute Victor cycles into 30-second loops, and a guarded VM-entry probe
  (`-DMP_DBG_VM=1`, py/vm.c prints a param's far address per mp_execute_bytecode entry)
  gave the smoking gun directly:
  ```
  V7A7548B0  (module exec)        V7A7530D0  (resume depth 1)
  V7A751A44  (depth 2, -0x168C)   V7A7503B8  (depth 3, -0x168C)
  V7A75ED2C  (depth 4 — SP WRAPPED BELOW 0)   V7A75D6A0  (depth 5) ...
  ```
  **Generator resume costs 0x168C ≈ 5772 bytes of C stack per level** (gen_instance
  iternext → gen_resume → mp_execute_bytecode chain), not the ~2 KB §4u inferred.  At
  24576 the SP wraps below 0 at C-depth 4 and the frames land 30+ KB past the stack
  segment, trampling the FAR_DATA/heap/qstr segments → every downstream symptom.
- **Why non-split "worked": the §4c/§4u frontiers were overflow luck.**  Under SS==DS the
  stack bottom sat at DGROUP offset ~0x91FE with libstub's UNUSED 34 KB `_heap_buf` right
  below it — overflowing frames silently landed there.  "gc(8) hangs at 16384 / gc(11) at
  24576" measured where the LUCK ran out, not where the stack did.  (§4u's own warning —
  "do NOT read a clean-wrong-value as graceful" — applied to its frontier number too.)
- The earlier stack-size sweeps (fail@5 for 24–36K, fail@7 at 40960, clean at 61952) and
  the wild writes/reboots all follow from "frames land at stack_seg:wrapped-offset": what
  they hit depends on how much segment lies between the wrap point and the live data.

### 3. The fix beyond the split: stack check ON + honest sizing
- `ports/dos8086/mpconfigport.h`: **MICROPY_STACK_CHECK (1)** — mp_cstack_check's
  `stack_top - &dummy >= limit` is same-segment offset math, split-safe.  main.c's
  duplicate `mp_raise_recursion_depth` now guarded `#if !(MICROPY_STACK_CHECK || ...)`
  (py/runtime.c provides it when the check is on).
- `MP_STACK_SIZE` default **61440** (SP ≤ 65534 is the only cap now);
  `MP_STACK_LIMIT` default **MP_STACK_SIZE − 8192** (checks run at VM/parser entry, so one
  ~5.6 KB resume frame + libstub/ISR transients can land past the last check).
- **Victor verification (all green, shipping image body 632112):** gen-sweep prints 4–8
  correct then `RuntimeError: maximum recursion depth exceeded` WITH AN INTACT TRACEBACK
  (the §4c "wrong 99 with clean exit" class is gone for good); feature-4t byte-exact vs
  host python3; churn scale2 all correct + DONE; plain recsum(60) = 1830 (STACKLESS heap
  frames unaffected by the check).  Gate 236/236; `make check` green.

### Instrumentation kept (all guarded, external micropython tree)
- py/vm.c `-DMP_DBG_VM=1` VM-entry stack probe + main.c `mp_dbg_vm_enter` printer.
- py/gc.c `-DMP_DBG_SWEEP_WATCH='"name"'` (the §4p watch qstr is now parameterized;
  default "churn") + a `GCS` print at gc_collect_start.
- ~/projects/mame patched none.cpp (debugscript works headless) — REBUILD REQUIRED if MAME
  is updated; `tools/run-victor-wp.sh` is now a working watchpoint/breakpoint/trace harness
  (bpset+tracelog+save all verified this session).

### Open tracks (no §4w designated)
- **Generator-resume frame diet**: 5772 B/level is the new depth bottleneck (≈8 levels at
  61440).  mp_execute_bytecode + gen_resume frame bloat under minic (everything
  slot-resident, no register pairs) is the lever; halving it roughly doubles depth.
- **FLOAT** (recipe §4a/§4t, ~59 KB delta vs ~190 KB headroom).
- huge `_qbe_huge_add` ≥0x8000 gap (§4i); §4p/§4q/§4v `-DMP_DBG_*` cleanup in the external
  tree; 211-commit upstream-qbe rebase.
- Latent minic note (found via a dead-end probe, NOT reduced): `jmp_buf bufs[6]` —
  array-of-jmp_buf cross-frame longjmp misbehaved identically under split and non-split;
  possibly the §2m array_vartyp stride family.  Reduce before trusting arrays of jmp_buf.

---

# (DONE in §4v above) Next session (§4v — DESIGNATED by the user 2026-06-09: move the C stack OUT of DGROUP into its OWN segment (the classic SS≠DS split), as an OPT-IN omf_link flag for far-data builds.  Payoff: the stack cap goes from ~28.4 KB (64 KB DGROUP minus 37118 data+bss) to a full ~64 KB → ≈30 generator-recursion levels at the §4u-measured ~2 KB/level (vs 11 today), and DGROUP gets back 24 KB of near-data slack.  The backend is ALREADY half-ready: `i8086/emit.c` stamps **SS** (not DS) into the segment word of `&local` far pointers, so the language-level far-pointer path is split-correct as-is.  The work is (1) the linker flag, (2) ~15 SS==DGROUP idioms in the libstub EPILOGUE, (3) ONE real investigation — the far→near narrowing path that derefs stack addresses DS-relative.  Feasibility surveyed end of §4u; full plan below.  §4u is DONE: stack default 24576, body 592512, frontier 7→11, gate 234/234.)

## §4v plan (gathered 2026-06-09 at end of §4u; survey done, implementation not started)

**Scope guard: opt-in, far-data only.**  Medium-model programs (stevie) pass near 2-byte
`char *` to stack buffers; those derefs are DS-relative by ABI, so SS must stay ==DGROUP
there.  The split is a new `omf_link.py --separate-stack` flag that only
`build-micropython.sh` (and future far-data consumers) pass.  Default linking is
byte-identical — the gate's existing 234 entries must not move.

1. **`tools/omf_link.py --separate-stack`** (easy).  Today the STACK segment is laid inside
   DGROUP and the MZ header gets SS=DGROUP para, SP=offset-within-DGROUP+size
   (`omf_link.py:1237-1255` — the comment documents the SS==DS invariant and its reason;
   stack seg created ~line 850; the `sp_full > 0xFFFF` check is the 64 KB cap §4u hit).
   Under the flag: do NOT group STACK into DGROUP; SS = stack seg's own para_base,
   SP = stack_size (validation already caps at 65535).  DGROUP's 64 KB check then covers
   data+bss only.
2. **libstub EPILOGUE: every "SS as a synonym for DGROUP" idiom** (mechanical but must be
   EXHAUSTIVE — a miss is a wild segment).  Inventory from the §4u survey grep:
   - `tools/libstub_to_exe.py`: line 158 `mov dx, ss` (_malloc returns the near-heap's
     segment — the [[libstub-null-ptr-dx]]-era fix); 848 + 914 `mov ax, ss` (far-ptr
     formation for DGROUP scratch); 1260 `mov cx, [ss:_spr_width]` (SS-override read of a
     DGROUP global); 1570/1732/1745/1758/1768/1926/2093/2150 `push ss / pop ds` (restore
     DS=DGROUP after a segment swap); 1790 `mov dx, ss` (comment literally says "= SS at
     runtime").  Line 1626 `mov [es:bx+4], ss` is far-setjmp saving the REAL SS — correct
     under split, leave it (verify longjmp restores it).
   - `minic/dos/libstub.asm`: 2586 + 2671 `push ss` (audit); 2532 is near-setjmp's real-SS
     save (legit); 2514 is just a comment.
   - **Fix pattern**: stash the DGROUP paragraph ONCE at startup in a `dw` inside the
     libstub CODE segment, read via `cs:` override (`mov ds, [cs:_dgroup_para]`) — the
     standard DOS idiom; works even when DS is currently swapped away (which is exactly
     when these sites run).  crt0 itself doesn't touch SS (grep-verified; DOS loader sets
     SS:SP from the header) and sets DS=DGROUP — unaffected.
3. **THE INVESTIGATION — far→near narrowing of stack addresses** (`i8086/emit.c:1265-1267`
   documents it: a far stack address "narrowed back to Kw because it feeds a near deref").
   A pointer to a local held in a register and deref'd as `[bx]` is implicitly DS-relative
   — correct today ONLY because SS==DS.  Under split this is a latent-invariant bug of the
   §4o/§4r family.  Options, in preference order: (a) prove the shape can't fire under
   compact far-data (grep the generated `build/mp-link/*.asm` for near derefs fed by `lea`
   of bp slots; read the minic `NEAR_DATA()` gates and the gvn/copy narrowing sites); (b)
   suppress the narrowing for slot-derived addresses when a new split-stack target flag is
   set; (c) `ss:` override on provably-stack-derived near derefs (hardest, avoid).  Do NOT
   ship the flag until this is closed one way or the other.
4. **New gated probe `split_stack_probe.c`** (compact + large): `&local` escaping to a
   callee that writes through it; a stack struct's member address through a chain; a stack
   buffer filled by `_far_sprintf` and read back; fn-ptr callback receiving a stack ptr;
   setjmp/longjmp across frames with stack ptrs live; malloc'd-vs-stack far-ptr compare
   (different segments under split — pins that nothing assumes one segment).  Plus
   `_malloc`'s returned segment must still be DGROUP (the site-1 fix).
5. **MicroPython bring-up, two steps** (the §4o lesson: change one variable at a time):
   - Step A — EQUIVALENCE: `--separate-stack` at the SAME `MP_STACK_SIZE=24576`.  Victor:
     `mp-feature-4t.py` byte-exact, churn scale2 clean, `mp-gen-sweep.py` frontier STILL 11
     (same stack size ⇒ same frontier; any drift = a missed SS assumption).
   - Step B — RAISE: bump `MP_STACK_SIZE` (try 49152, then ~61440; SP cap 65535) and
     re-sweep the generator frontier — expect ~2 KB/level scaling (≈24 / ≈30 levels).
     Re-run feature-4t + churn scale2 at the final size.  Update the build-micropython.sh
     comment block (it currently documents the DGROUP cap as binding — that dies with the
     split).
   - The MP conservative GC stack scan is split-safe by construction (`mp_stack_set_top`
     takes `&stack_dummy` as a FAR pointer whose segment is SS via the emit.c path above;
     VERIFY_PTR range-checks against the heap segment only) — but it's on the step-A
     verification list anyway, churn scale2 exercises it.
6. **Gate**: `tools/test-dos.sh` 234 existing entries byte-identical (flag is opt-in);
   +new probe entries.  `make check` green.  Commit at green per the milestone convention.
- **Composing follow-up (separate, cheap, do after)**: `MICROPY_STACK_CHECK=1` turns the
  (now further-out) overflow cliff into a clean `RuntimeError` and makes
  `mp_stack_set_limit` real (§4u confirmed it's a no-op macro today).  Split for depth,
  check for safety at the new edge.
- Other open tracks unchanged: FLOAT (size objection gone, ~59 KB delta vs ~232 KB
  headroom, recipe §4a/§4t); huge `_qbe_huge_add` ≥0x8000 gap (§4i); §4p/§4q `-DMP_DBG_*`
  cleanup in the external tree; 211-commit upstream-qbe rebase.

## 2026-06-09 §4u notes (MP_STACK_SIZE 16384 → 24576; generator frontier measured 7 → 11; gc(15) is still UB-deep)

**§4u landed the user-designated stack bump and measured exactly what it bought.**  One file
changed (`tools/build-micropython.sh`); no qbe/minic/external-tree change.

### The change
- `MP_STACK_SIZE=${MP_STACK_SIZE:-16384}` → `24576`, with the stale comment block REWRITTEN
  (it cited the dead §4c load-ceiling premise 828224 > ~824416; the binding cap is now DGROUP:
  data+bss 37118 + stack 24576 = 61694 of 64 KB, ~3.8 KB slack, 32768 fails to link).  The
  stale `--stack-size` comment at the omf_link call site got the same correction.
- **`MP_STACK_LIMIT` left at 8192 and documented vestigial**: `MICROPY_STACK_CHECK` is OFF, so
  `mp_stack_set_limit()` is the no-op macro in py/stackctrl.h (`(void)(limit)`) — scaling it
  would change nothing.  Turning STACK_CHECK ON is a separate track (open list above).
- Build: 107/107 TUs, body **592512** = §4t's 584320 + 8192 exactly; image 610160.
  Cross-check: relink of the SAME objects at `--stack-size 16384` reproduces §4t's 584320
  byte-for-byte, so the only delta is the stack.

### The frontier measurement (the real §4u deliverable)
- New sweep probe **`build/mp-gen-sweep.py`** (kept, untracked): prints `i sum(gc(i))` for
  i=4.., where `gc` is the §4c recursive generator — each level C-recurses on resume
  (objgenerator.c → mp_execute_bytecode; STACKLESS does NOT cover resume).
- **At 16384 (relink-only image): depths 4–7 correct, gc(8) HANGS.**
- **At 24576 (shipping image): depths 4–11 correct, gc(12) HANGS.**
- So +8192 bytes bought +4 levels → **~2 KB of C stack per generator-resume level**, and the
  bump verifiably took effect at runtime.
- **`sum(gc(15))` (the §4c case) is BEYOND the frontier at BOTH sizes.**  The §4c report
  ("returns wrong 99 with a clean exit at 16384, degrades gracefully") was a LAYOUT ACCIDENT,
  not a property: with MICROPY_STACK_CHECK off, beyond-frontier = stack overflow into DGROUP
  data = UB.  The standalone `mp-gen-probe.py` at 16384 happened to come back with 99; the
  sweep at 16384 hangs at gc(8); the 24576 image hangs at gc(12)/gc(15).  Do NOT read a
  clean-wrong-value as "graceful" — if graceful is wanted, that's MICROPY_STACK_CHECK=1.

### Verification (all green)
- `build/mp-feature-4t.py` on real Victor: **byte-exact vs host python3** (filter/reversed/
  count/%-format + comprehension/dict/str.format/slicing), clean D4/C5.
- `build/mp-churn-scale2.py` on real Victor: churn(20..120) all correct (`120 7980`, `DONE`)
  — the stack bump shifts every far-data segment; GC stays clean on the new layout (§4r's
  CX-pin fix holding).
- Gate `tools/test-dos.sh` **234/234** (no qbe/minic change, as expected).

---

# (DONE in §4u above) Next session (§4u — DESIGNATED by the user 2026-06-09: bump `MP_STACK_SIZE` 16384 → 24576 (build-micropython.sh default).  §4c picked 16384 ONLY because 24576 → body 828224 > the ~824416 load ceiling; §4t's per-function gc-sections (body 584320) removed that constraint entirely, and the user judged the bigger C stack "more clear day-to-day value than float" (deep GENERATOR recursion still C-recurses on resume — objgenerator.c mp_execute_bytecode — which STACKLESS does NOT cover; at 16384 `sum(gc(15))` returns a WRONG value 99 with a clean exit, §4c).  Plan below.  Float stays available-not-scheduled; heap is segment-bound (~60 KB max), not ceiling-bound.)

## §4u plan (executed 2026-06-09, see notes above)
- **Change:** `tools/build-micropython.sh` `MP_STACK_SIZE=${MP_STACK_SIZE:-16384}` → `24576`,
  and REWRITE the stale comment block above it (it still says "a bigger stack would push the
  image over the Victor load ceiling" with the §4c 820096/828224 numbers — that premise died
  with §4t's per-function stripping).  Expected body ≈ 584320 + 8192 ≈ 592.5 KB (§4c measured
  the 16384→24576 delta as ~+8.1 KB) — vastly under the ~824 KB ceiling.
- **Hard cap check (why 24576, not more):** stack lives in DGROUP; DGROUP data+bss is
  **37118** in the §4t image, so 64 KB − 37118 ≈ 28.4 KB is the absolute max (32768 famously
  fails to link, §4c).  24576 leaves ~3.8 KB DGROUP slack — keep it; do NOT chase 28K.
- **`MP_STACK_LIMIT` (default 8192, sed-patched into main.c `mp_stack_set_limit`):** decide
  whether to scale it with the stack (e.g. 16384) — read ports/dos8086/main.c to see what it
  actually gates first (MICROPY_STACK_CHECK is OFF, so it may be vestigial).
- **Victor verification:**
  1. `build/mp-gen-probe.py` — the §4c generator-depth bisect; at 16384 the `sum(gc(15))`
     case prints a WRONG value (99 instead of 120) with clean exit.  At 24576 expect 120; if
     still wrong, find the new depth frontier (gc(N) sweep) and DOCUMENT it — the point of
     the bump is moving the frontier, not magic.
  2. `build/mp-feature-4t.py` — byte-exact vs host python3 (regression).
  3. `VICTOR_SRC=build/mp-churn-scale2.py … 240` — churn(20..120) + DONE (GC regression;
     the stack bump shifts every far-data segment, the §4o lesson says re-verify, though
     §4r's fix made the old alignment sensitivity moot).
- **Gate:** `tools/test-dos.sh` must stay 234/234 (no qbe/minic change expected — this is a
  harness-default + external-tree-free change; only build-micropython.sh moves).
- Commit at green per the milestone convention.

# (§4t notes follow) §4t was a TRIPLE win: (1) per-FUNCTION text segments (QBE_TEXT_SEG_BUDGET=1 in build-micropython.sh) let --gc-sections strip 4101 segments → MP code 703553→452461 (-251 KB, -36%), body 835888→584320 — **~240 KB of headroom under the ~824 KB Victor ceiling**; (2) that headroom funded the last four MINIMUM-ROM gaps: filter/reversed/str.count/str %-format are ON and Victor-verified; (3) the %-format bring-up flushed out + FIXED a REAL i8086 emit bug — the Osub Kw two-address rescue hardcoded BX as scratch, so to==BX compiled `right_pad -= p` to a NO-OP (mp_print_strn right-pad infinite loop, "%-5d" hang).  Gate 232→234.  Other reopened-but-unscheduled: float (§4a's "needs a code-size campaign" premise is GONE — FLOAT body was 882944 at 56 KB granularity); bigger heap (segment-bound, ~60 KB max).  Also open: (a) huge `_qbe_huge_add` ≥0x8000 gap; (b) §4p/§4q -DMP_DBG_* cleanup in the external tree; (c) 211-commit upstream rebase.

## 2026-06-09 §4t notes (per-function gc-sections -251 KB; filter/reversed/str.count/%-format ON; Osub rescue-scratch fix)

**§4t set out to enable the four documented MINIMUM-ROM feature gaps and ended up landing a
size breakthrough plus a real backend fix.**  Sequence: feature flip → didn't fit → measured
honestly → found the size lever → lever exposed a latent codegen bug on first-ever execution
of the %-width path → probe + fix.  All Victor-verified.

### 1. The features (external micropython tree, ports/dos8086/mpconfigport.h)
- `MICROPY_PY_BUILTINS_FILTER/REVERSED/STR_COUNT/STR_OP_MODULO` all `(1)`.
- **5 qstrs QDEF0-appended** to `ports/minimal/build/genhdr/qstrdefs.generated.h` (the §4a
  recipe; pool 0 is unsorted+positional so appends are index-safe): `filter` 48677/6,
  `reversed` 28321/8, `__reversed__` 65377/12, `%#x` 6779/3, `%#o` 6764/3 (hex()/oct()
  format through %-modulo).  djb2 `hash*33^b & 0xFFFF` re-verified against count/__dir__/
  __call__/float.  Exact-need check: preprocess every TU with the build's cpp flags, grep
  MP_QSTR_, comm against the pool (source-grep over-counts config-gated refs).
- Cost at the OLD 56 KB granularity: +10,944 (filter+reversed+count; objfilter/objreversed
  whole-TU text ~3 KB each + objstr count +2.6 KB) +6,784 more for OP_MODULO — vs ~6.2 KB
  headroom (baseline 818,160 = §4s 818,080 + 80 B qstr data).  DID NOT FIT → size lever.

### 2. The size lever: per-function text segments (tools/build-micropython.sh)
- `export QBE_TEXT_SEG_BUDGET=${QBE_TEXT_SEG_BUDGET:-1}` — asm_to_omf.py already splits
  .text at function boundaries when over budget, and omf_link --pack-code was DESIGNED for
  per-function granularity (its comment says so; word-aligned packing, no paragraph waste).
  Budget=1 = every function its own segment → --gc-sections strips DEAD FUNCTIONS
  (statics included) instead of whole-TU text blocks.
- **dead-stripped 201 → 4101 segments; code 703553 → 452461; body 835888 → 584320.**
  ~250 KB of the image was dead functions inside partially-used TUs (mpz, showbc, profile,
  the gated-out emitters, half of objstr/runtime/vm helpers...).
- Same default wired into `tools/recompile-mp-tu.sh` (a TU rebuilt with a different budget
  would silently revert that TU to whole-TU granularity on relink).
- asm_to_omf's GLOBAL default stays 56000: per-function segments WITHOUT --pack-code would
  add paragraph padding per function (stevie links without gc-sections/pack-code).
- Reachability is fixup-based, so a kept function's targets are kept by construction — a
  dangling reference is impossible; the risk class is layout-sensitivity, hence the full
  Victor re-verification (below).

### 3. The bug: i8086 Osub Kw two-address rescue, scratch hardcoded to BX (i8086/emit.c)
- First-ever Victor execution of the %-width path: `"%5d" % 7` printed correctly,
  `"%-5d" % 7` HUNG.  py/mpprint.c `mp_print_strn`: str.format always passed width=-1, so
  the pad>0 loops had never run on target before; %-modulo with width is what reached them.
- Generated asm for `right_pad -= p` (SSA `sub %rp, %p`, rega: rp=SI, p=BX, dest=BX):
  `push bx / mov bx, bx / mov bx, si / sub bx, bx / pop bx` — the "rescue" of arg[1]
  through the HARDCODED BX scratch degenerates when to==arg[1]==BX: the save is a self-mov,
  the dst-mov clobbers it, the op computes rp-rp=0, and `pop bx` discards even that.
  right_pad never decrements → infinite vstr-append loop → hang.  Non-commutative sibling
  of [[i8086-two-addr-arg1-alias]] (that fix swapped COMMUTATIVE operands only; sub can't
  swap).  Second latent hole, same site: arg[0] in BX was clobbered by the save before
  being read.  In practice the path serves Osub Kw (shifts/div/rem have dedicated early
  handlers; add/and/or/xor commute).
- **Fix:** the rescue scratch is now CHOSEN from {BX,CX,SI,DI}, skipping the destination
  and arg[0].  emitf prints the op against the scratch; pop restores it; the bad/swap_bx
  addressing unwind is untouched (BX itself is no longer disturbed when scr!=BX).
- **Probe `sub_arg1_alias_probe.c` (medium+compact, gate 232→234):** pad_out2 (the
  mp_print_strn right-pad shape + two extra loop-carried values) lands the sub's dest in
  BX and is bug-loud — VERIFIED against the reverted fix: ok6/ok7/ok8 fail, all 8 pass
  with the fix.  Guard-bounded so a regression prints wrong sums instead of hanging the
  gate.  rega-dependent trigger ⇒ green probe is necessary-not-sufficient (the §4r
  caveat); the real guard is the scratch chooser + the Victor %-format run.
- minic gap noted en route: `static emit_fn ep = emit_n;` (file-scope fn-ptr initializer
  to a function) dies "non-constant in case label" — worked around with runtime assign;
  not reduced this session.

### 4. Verification (all green)
- `make check` green; gate **234/234** (new probe medium+compact).
- MP image: 107/107 TUs, body 584320 (features ON, per-function stripping, fixed qbe).
- Real Victor: `mp-feature-4t.py` — filter/filterN/reversed(list,range,str)/count/
  %-format (%d %s %x, %5d %-5d %05d, %c %%, single-arg) + comprehension/dict/str.format/
  slicing regressions ALL byte-exact vs host python3; clean D4/C5.
- Real Victor: `mp-churn-scale2.py` churn(20..120) all correct + DONE on the new layout.
- Probe scripts kept: `build/mp-feature-4t.py`, `build/mp-fmt-bisect.py`.

### Reopened by the headroom (decisions for the user, not unilaterally taken)
- **FLOAT**: §4a measured body 882944 at 56 KB granularity vs ~824416 ceiling and the user
  parked it ("needs a code-size campaign").  Per-function stripping IS that campaign:
  the integer image dropped 251 KB; the float delta was only ~59 KB.  Re-attempt recipe
  unchanged (mpconfigport float comment + 2 QDEF0 lines, §4a).
- **MP_STACK_SIZE 16384 → 24576**: §4c rejected 24576 ONLY because body 828224 exceeded
  the load ceiling; now ~240 KB clear.  Bigger C stack = more deep-generator headroom
  (generator resume still C-recurses under STACKLESS).
- **Heap**: still segment-bound (~64 KB max for the single static array), not ceiling-bound.

---

# (DONE in §4t above) Next session (§4t — §4s FIXED the §4o latent minic bug: pointer RELATIONAL compares now lower UNSIGNED (`cult`/`cule`), gate 230→232/232, MP image byte-count-identical, Victor scale2 still all-correct.  No designated successor — pick from the open lower-priority tracks: (a) huge-model `_qbe_huge_add` ≥0x8000 index gap (§4i scope note); (b) MicroPython feature/perf work now that GC is sound under churn; (c) clean up the §4p/§4q `-DMP_DBG_*` instrumentation left in the external micropython tree; (d) 211-commit upstream-qbe rebase, pure plumbing.)

## 2026-06-09 §4s notes (FIXED the §4o latent minic signed pointer-relational-compare bug)

**§4s closed open track (a) from §4r: C pointer relational compares (`<`,`<=`,`>`,`>=`) were
lowered SIGNED; C11 6.5.8 requires unsigned (address comparison).**  One frontend fix, one new
gated probe; no backend change needed.

### The fix (minic/minic.y, emit site ~4352)
The unsigned-compare branch in expr()'s Binop emission keyed ONLY on `ISUNSIGNED(s0/s1)`;
pointer types never carry the UNSIGNED flag, so `p < q` fell through to signed `cslt`/`csle`.
The branch now also fires on `KIND(s0.ctyp)==PTR || KIND(s1.ctyp)==PTR` → `cult`/`cule`.
- `>`/`>=` are parse-time-normalized to swapped `<`/`<=` (mknode at the grammar rule), so the
  one emit site covers all four operators.
- prom() already returns the pointer operand's type for "ne<l" ops with operands untouched
  (the §1i no-Scale rule), so KIND==PTR is reliably visible at the emit site.
- Signed/unsigned INTEGER compares are untouched (the probe pins this).
- i8086 backend already had Ocultw/Ocultl/Oculew/Oculel handlers (used by unsigned integer
  compares) — frontend-only fix.  Also updated the stale `/* meeeeh, wrong for pointers! */`
  comment on the otoa[] table.

### New gated probe: `ptr_relational_probe.c` (medium + compact, one shared golden)
- Far-pointer compares across the SEGMENT sign bit (`MK_FP(0x9000,4)` vs `MK_FP(0x7000,4)`,
  all four operators + one false case) — far ptrs are Kl in EVERY model, so these discriminate
  under medium AND compact.
- Default `char *` compares from synthetic addresses 0x9000 vs 0x7000 — under medium that is
  the near Kw path (offset sign bit); under compact the cast widens to a far ptr but the
  expected output is identical, so one golden serves both models.
- Regression guards: in-array ordering; `iident(-1) < iident(1)` and
  `iident(-28672) < iident(0x7000)` must STAY signed (=1; unsigned would invert the latter).
- **Verified bug-loud against the reverted fix**: exactly the 7 discriminating cases invert
  (ok1-ok7), guards stay correct.  Opaque identity fns defeat const folding.

### Verification (all green)
- `make check` (SSA suite) green.  Gate **230 → 232/232** (new probe medium + compact).
- Full MicroPython rebuild (build-micropython.sh --model=compact): body **818080 —
  byte-count IDENTICAL to §4r** (signed vs unsigned compare emit the same instruction
  lengths on i8086; only the jcc conditions changed, jl/jle → jb/jbe).
- Real-Victor scale2: churn(20..120) all correct (`120 7980`, `DONE`) — the image-wide
  compare-condition change (VERIFY_PTR etc.) is behaviorally clean on target.

### Left open (no successor designated — pick by need)
- huge-model `_qbe_huge_add` ≥0x8000 gap; reconfirmed this session that probe TUs still
  don't get -DFAR_DATA from build-example.sh (only crt0 does) — the §4i scope note.
- §4p/§4q `-DMP_DBG_SWEEP`/`-DMP_DBG_GLOBALS` instrumentation still in the external
  micropython tree (guarded, normal builds unaffected).
- MicroPython feature/perf work; 211-commit upstream-qbe rebase.

---

# (DONE in §4s above) Next session (§4s — the churn(120) saga is CLOSED: §4r landed the i8086 variable-shift CX pin and VERIFIED it on real Victor at ALL FOUR mod-4 segment alignments (scale2 prints churn(20..120) correct + DONE everywhere).  Gate 230/230.  No designated successor — pick from the open lower-priority tracks: (a) the §4o LATENT minic bug: C pointer RELATIONAL compares (`<`,`<=`,`>`,`>=`) lower to SIGNED `cslel`/`csltl` but C requires unsigned — harmless today only because every segment is ≥0x8000; probe + fix (same family as §2r extsw/extuw); (b) huge-model `_qbe_huge_add` ≥0x8000 index gap (§4i scope note); (c) MicroPython feature/perf work now that GC is sound under churn; (d) clean up the §4p/§4q `-DMP_DBG_*` instrumentation left in the external micropython tree.)

## 2026-06-09 §4r notes (THE FIX LANDED + VICTOR-VERIFIED at all 4 alignments — the 13-session churn(120) saga is CLOSED)

**§4r implemented the §4q fix recipe exactly and the saga is over.**  Two qbe changes, one
new gated probe, one harness fix:

### The fix (i8086/isel.c + i8086/emit.c)
- **`i8086/isel.c::selshift`** — a variable (non-RCon) shift count is now PINNED to CX,
  mirroring `amd64/isel.c` selshift: emit (program order) `Ocopy CX <- count`, the shift with
  `arg[1]=TMP(RCX)`, then a no-dest `Ocopy <- CX` clobber-marker so rega keeps CX busy across
  the shift (and the dest out of CX).  The front copy is a REAL instruction spill/rega lower
  correctly — when the count is spilled, the copy's arg is rewritten to the slot and the
  reload happens at the copy, not via emit.c's stale `rname[r1.val]` read.  Immediate (RCon)
  counts keep all the old emit paths (0/1 direct, 2–8 unroll, >8 via CL with push/pop).
- **`i8086/emit.c::emitins`** — early-return for `Ocopy` with `to == R` (the clobber-marker),
  same as amd64's emit.  Without it the marker would hit the generic `mov %=, %0` omap entry.
- Kw emit handler needs NO change: with the count pinned, `r1.val == RCX` skips the stale
  `mov cx, <reg>`; the (now-redundant) `push cx`/`pop cx` bracket is harmless.  The Kl
  (32-bit) variable-shift handler likewise sees `rname[r1.val]=="cx"` and skips its mov; its
  jcxz/loop consumes CX directly.  Verified in regenerated `build/mp-link/gc.asm`: at the §4q
  smoking-gun site the count moves to CX immediately after the `imul`, BEFORE the extub can
  reuse AX — `atb >> atb` is gone.

### Verification (the §4q checklist, all green)
- `make check` (SSA suite) green.
- **Gate 228 → 230/230** with the new probe (below); stevie size budget still ok.
- Full MicroPython rebuild (107/107 TUs): body 817840 → **818080** (+240 B, under the
  ~824416 ceiling).
- **Real-Victor scale2 at the shipping link: churn(20..120) ALL correct — `120 7980`,
  `DONE`.**  The NameError is gone.
- **`--stack-size` 4-paragraph alignment sweep (16384/16400/16416/16432): ALL FOUR mod-4
  segment classes PASS** (under §4q, the marking miss was alignment-independent and ~2 of 4
  alignments manifested NameError).  Ran in parallel, one MAME each, ~4 min wall.

### New gated probe: `shift_count_spill_probe.c` (medium + compact)
Recreates the gc_mark_subtree shape — packed 2-bit ATB-style table scan where the count is
`2*(block&3)` (imul product), the value is a byte load (extub), under register pressure from
live loop-carried values — cross-checked against shift-free expectations (byte composition /
doubling, so a shift miscompile can't corrupt both sides).  Plus: `1<<n` (need_val_load),
count 0 / count≥8, `x>>x` (the exact §4q wrong formula must differ), Kl variable shifts both
directions, signed sar.  **Caveat pinned in the gate comment: the original miscompile was
layout-sensitive, so a green probe alone is necessary-not-sufficient — the real guard is the
isel pin itself + the Victor scale2 run.**

### Harness fix: `tools/run-victor-sasi.sh` watchdog orphan (cost ~30 min twice this session)
`kill "$WATCHDOG_PID"` killed the watchdog SUBSHELL but orphaned its `sleep`, which inherits
the script's stdout: any caller that PIPES the script (`... | tail -30`) then blocks on pipe
EOF until the full wall budget (~18 min) expires even though the run finished.  New
`retire_watchdog()` (`pkill -P` the subshell's children first) used at both the normal-exit
site and `cleanup()`.  Symptom to remember: "MAME exited but the harness prints nothing for
many minutes" = NOT a hang, it was the orphan sleep.

### Left open (no successor designated — pick by need)
- §4o LATENT minic bug: pointer relational compares are SIGNED (`cslel`/`csltl`); C requires
  unsigned.  Harmless in current images (every segment ≥ 0x8000) but real; own probe + fix.
- The §4p/§4q `-DMP_DBG_SWEEP`/`-DMP_DBG_GLOBALS` instrumentation is still in the external
  micropython tree (guarded, normal builds unaffected) — remove or keep as debugging kit.
- huge-model `_qbe_huge_add` ≥0x8000 gap; `build-example.sh` -DFAR_DATA scope note (§4i).
- 211-commit upstream-qbe rebase (pure plumbing, deferred).

---

# (DONE in §4r above) Next session (§4r — FIX the i8086 variable-shift codegen bug §4q ROOT-CAUSED.  §4q CRACKED THE 13-SESSION SAGA: the churn chunk is freed-while-live because `gc_mark_subtree`'s child-check `ATB_GET_KIND` = `(atb >> (2*(block&3))) & 3` is MISCOMPILED — the variable Kw shift reads its count from a register the preceding `extub` (atb-byte zero-extend) clobbered, so it computes `atb >> atb` instead of `atb >> shift`, mis-classifies the chunk's HEAD as non-HEAD, skips marking → freed-while-live → reused → NameError.  Register-allocation-dependent ⇒ CODE-LAYOUT-sensitive (the heisenbug).  NOT segment-sensitive — §4o's period-4 only governs whether the reused-garbage hash misses the lookup slot (NameError manifestation).  FIX RECIPE = mirror amd64/isel.c `selshift`: pin a variable shift count to RCX via `Ocopy RCX<-count` + a clobber-marker; i8086 `selshift` currently does neither.)

## 2026-06-09 §4q notes (ROOT-CAUSED the saga: i8086 variable-shift count-operand clobber in gc_mark_subtree's ATB_GET_KIND)

**§4q definitively cracked the churn(120) bug via on-target instrumentation (real Victor/MAME)
+ generated-asm analysis.**  No qbe source changed this session (diagnosis + a precise fix
recipe for §4r).  All instrumentation is in the EXTERNAL micropython tree, guarded by
`-DMP_DBG_SWEEP=1` (normal builds unaffected); LEFT IN PLACE for §4r.  The clean shipping FAIL
image is restored (`build/mp-link/mpython.exe`, body 817840, byte-identical to §4p).

### THE BUG (airtight, multi-stage)
1. **Freed-while-live = a MARKING MISS, not a sweep mis-read.**  Instrumented `py/gc.c`
   `gc_sweep_free_blocks` to walk back to the "churn" chunk's HEAD block and print its ATB kind
   after the mark phase (`PRESWP ... hk=`), and main.c `gc_collect` to set the watch from
   `qstr_str(qstr_find_strn("churn",5))`.  The chunk head is **block 0xA5** in every run (the
   intra-heap layout is stable).  Across an 8-way `--stack-size` sweep (heap_seg 0xBB10..0xBB17,
   two full mod-4 periods) the head is `hk=1` (AT_HEAD, **UNMARKED**) in EVERY alignment, and
   `SWPLIVE` fires — the chunk is freed-while-live UNCONDITIONALLY.
2. **NOT segment-sensitive.**  The PASS/FAIL outcome still followed §4o's period-4 (FAIL iff
   heap_seg mod4∈{0,3}), but `hk=1` regardless.  So the segment only governs whether the
   reused-object garbage that overwrites the freed "churn" string (a far type-ptr whose SEGMENT
   word shifts with `--stack-size`) hashes to a slot that still finds "churn" (PASS) or misses
   (FAIL→NameError).  §4o's "segment-sensitive wild write" was a DOWNSTREAM red herring.
3. **The "instrumentation hides it" heisenbug, EXPLAINED twice.**  (a) A naive watch that
   materialized the chunk POINTER in `gc_collect`'s frame got picked up by the conservative
   C-stack scan and ROOTED the chunk (masking the bug) — fixed by doing the qstr lookup entirely
   inside a deep void helper (`gc_dbg_update_watch`) so only the integer block escapes.  (b) Even
   then, adding the CHILD/ROOT probes (code in the MARK functions) flipped `hk=1`→`hk=3` (chunk
   marked, PASS) at the SAME segment class — i.e. the marking miss is **code-layout-sensitive**,
   not data/segment-sensitive.
4. **ROOT CAUSE in the generated asm.**  `build/mp-link/gc.asm` (clean) `gc_mark_subtree`'s
   child-check `if (ATB_GET_KIND(ptr_block) != AT_HEAD) continue;` compiles to (SSA
   `%t146 =w mul 2,%t148` = 2*(block&3) = shift; `%t154 =w extub %t134` = atb byte;
   `%t133 =w sar %t154,%t146`):
   ```
   imul bx            ; t146 (count) -> AX
   mov [bp-16], ax    ; SPILL count to slot
   mov dx, di         ; extub: atb byte
   and dx, 255
   mov ax, dx         ; t154 (value) -> AX   <-- reuses AX, clobbering the count
   push cx
   mov cx, ax         ; cx = AX = VALUE  (BUG: rega says count is RTmp(AX), but AX now=value)
   sar ax, cl         ; atb >> (atb&31)   instead of   atb >> shift
   ```
   The count `t146` was spilled to `[bp-16]`, but the `sar`'s arg[1] is still tracked as
   **RTmp(AX)**; the intervening `extub` reused AX; the i8086 shift emit's `mov cx, rname[r1.val]`
   reads the stale (clobbered) AX.  So the shift count is the atb byte itself → wrong
   `ATB_GET_KIND` → the unmarked HEAD reads as non-HEAD → `continue` (skip) → never marked.
   (Why only "churn" visibly breaks: the wrong formula mis-skips SOME live heads, but under churn
   almost everything is transient garbage about to die anyway — only a long-lived interned-qstr
   chunk produces a visible NameError.)

### THE FIX FOR §4r (mirror amd64; the recipe is exact)
`i8086/isel.c::selshift` (line ~248) just `fixarg`s both operands and lets emit move the count to
CL — with no register pin, so rega can place the count in a register an adjacent op clobbers.
`amd64/isel.c::selshift` (case Osar/Oshr/Oshl, ~306-321) is the canonical fix: for a non-RCon
count it does `i.arg[1]=TMP(RCX); emit(Ocopy,Kw,R,TMP(RCX),R); emiti(i); emit(Ocopy,Kw,TMP(RCX),r0,R)`
— pinning the count to RCX via a real `Ocopy RCX<-count` (which rega lowers correctly, reloading
from the spill slot) and a no-dest `Ocopy <- RCX` clobber-marker so rega knows the shift writes RCX.
- Mirror that in i8086 `selshift` for variable (non-RCon) counts.  Then the shift's arg[1] is
  always RCX, so `i8086/emit.c`'s Kw-shift handler (~1098-1210) hits the `r1.val==RCX` path and
  emits no stale `mov cx, <reg>` — the count is materialized into CX by the isel `Ocopy`.
- Keep the RCon (immediate) paths unchanged (imm 0/1, 2-8 unroll, >8 via CL with push/pop).
- Check the Kl (32-bit) shift path (separate handler in emit) also expects the count in CL/RCX.
- **The 224-probe gate did NOT catch this** (layout-sensitive) — so DON'T trust a green gate alone.
  Add a probe that recreates the spill scenario (a variable shift whose count is spilled across an
  intervening single-byte zero-extend under register pressure), AND re-run scale2 on real Victor:
  `VICTOR_SRC=build/mp-churn-scale2.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 240`
  must print churn(120)=`120 7980` `DONE` at the clean (uninstrumented) FAIL image.

### Repro / instrumentation cheat-sheet (verified this session)
- Clean FAIL baseline restored: `build/mp-link/mpython.exe` body 817840 (heap_seg 0xBA8B).
  Baseline run reproduces churn(20..100) correct then churn(120)→NameError.
- Instrument: `MP_EXTRA_CPPFLAGS="-DMP_DBG_SWEEP=1" tools/recompile-mp-tu.sh gc …/py/gc.c` then
  `… main …/ports/dos8086/main.c`.  gc.c adds `gc_dbg_watch_block` + `gc_dbg_update_watch()` +
  `PRESWP`/`SWPLIVE`/`CHILD`/`ROOT` prints (all `#if MP_DBG_SWEEP`).  main.c `gc_collect` calls
  `gc_dbg_update_watch()`.  Image stays under the ~824416 ceiling (body ~820800 with all probes).
- `--stack-size` sweep (segment only, intra-heap layout fixed): relink
  `tools/omf_link.py -o /tmp/qK.exe --stack-size $((16384+16*k)) --gc-sections --pack-code <objs>`
  from `/tmp/mp_objs.txt` (109 objs); run all in parallel via `run-victor-sasi.sh` (~2.5 wall-min
  each).  WARNING: heap_seg mod-4 does NOT predict FAIL for an instrumented body — sweep 4-8 and
  observe.  The MARKING miss (`hk=1`) is alignment-INDEPENDENT, so any single alignment shows it —
  but adding code to the MARK functions perturbs rega and can flip it (that IS the bug's nature).

---

# (SUPERSEDED by §4q above) Next session (§4q — find WHY the X00 mark phase fails to mark the qstr-string chunk.  §4p NAILED THE MECHANISM: churn(120) NameError = `mp_load_global(MP_QSTR_churn)` misses because the "churn" qstr STRING was FREED-WHILE-LIVE during the churn(100) collection and reused — its recomputed hash then lands the lookup on an empty slot.  NOT a wild write (§4o's framing was wrong); it's a freed-while-live GC marking failure, layout-sensitive per §4o's period-4.  The exact failing lookup is fully understood; the open question is the marking link that breaks.)

## 2026-06-09 §4p notes (CRACKED the failure mechanism: freed-while-live of the "churn" qstr string → lookup miss; instrumentation via SERIAL, MAME debugger ruled out)

**§4p turned §4o's "layout-sensitive wild write" into a precise, fully-traced mechanism** using
SERIAL instrumentation in the EXTERNAL micropython tree (all guarded by `-DMP_DBG_GLOBALS=1`;
normal builds unaffected).  The MAME-debugger watchpoint path was investigated and RULED OUT
(see below).  No qbe/minic source changed this session — pure diagnosis.

### THE MECHANISM (airtight, multi-step, all serial-verified on real Victor)
1. **The failure is a name LOOKUP miss, not a structural corruption.**  Instrumented
   `py/runtime.c::mp_load_global`'s terse-NameError path to print the failing qstr + globals
   map state.  Result on a FAIL run: `NFq=00DB` → qstr **219 = `churn`** (a MODULE global, not a
   builtin — that's why it's not found in builtins either → NameError).
2. **The globals TABLE is fully intact at the miss.**  Dumped every slot: `s03 k=000006DA
   v=<fnptr>` — slot 3 holds exactly `MP_OBJ_NEW_QSTR(219)=(219<<3)|2=0x6DA` and churn's
   function pointer.  `a=4 u=3`, table ptr unchanged.  So nothing overwrote the dict.
3. **The lookup misses purely on the HASH.**  Globals is a hash map; `pos = qstr_hash(219) %
   alloc`, then linear-probe until found or an EMPTY slot.  Slot 1 is empty; churn is at slot 3.
   So the lookup finds churn IFF `qstr_hash(219)%4 ∈ {2,3}` (reaches slot 3 before empty slot 1)
   and MISSES iff `∈ {0,1}`.  churn(20..100) all worked, churn(120) misses → **`qstr_hash(219)`
   CHANGED**.
4. **`qstr_hash` RECOMPUTES from the string** (`MICROPY_QSTR_BYTES_IN_HASH=0`, confirmed:
   `qstr.ssa` `qstr_hash` calls `qstr_compute_hash(pool->qstrs[q], pool->lengths[q])`).  Dumped
   the string: `ql=0005` (correct len 5) but `qs=0402BBB91000` — **the "churn" string bytes are
   GARBAGE**: a reused-object header (a far type-pointer `0xB9BB:0x0204` + a length word `0x0010`).
   So the "churn" string's heap memory was REALLOCATED to a new object.
5. **=> The "churn" qstr string (packed in `qstr_last_chunk`, py/qstr.c) was FREED-WHILE-LIVE
   during the churn(100) "X00" collection, then reused by churn(120)'s allocations**, overwriting
   it → `qstr_hash` recomputes garbage → `pos∈{0,1}` → lookup hits empty slot 1 → NameError.
   This is the §4f/§4o "freed-while-live qstr_pool" family — now CONFIRMED with the smoking-gun
   reused-header bytes.  §4o's "wild write" framing was WRONG; it's a marking failure.
6. **The string, gc_pool_start, gc_pool_end all share ONE segment** (`qp=BE0F1F50 gp=BE0F1500
   ge=BE0FD200`), so `VERIFY_PTR`'s range check (`ptr>=gp && ptr<ge`) is correct for the string
   itself — the freed-live is from the **MARK phase failing to mark the qstr-string chunk**, not
   a VERIFY_PTR rejection of the string.  Layout-sensitive exactly per §4o (FAIL iff the *pool
   start's* normalized segment ≡ {0,3} mod 4; period 64 bytes = one ATB byte = 4 blocks).

### THE OPEN QUESTION FOR §4q — which marking LINK breaks?
The "churn" string is reachable for marking via `last_pool` (root) → pool chunk → `qstrs[]` →
the string-data chunk (`qstr_last_chunk`).  Strings are PACKED into a shared chunk (qstr.c
`qstr_from_strn_helper`: `m_new` a chunk, append each string at `qstr_last_used`), so `qstrs[idx]`
point INTO the chunk at non-block-aligned offsets; the chunk survives all-or-nothing via the ONE
block-aligned pointer to its HEAD (the first string at `chunk_base`).  Statically every link
"should" mark fine (all same-segment, `BLOCK_FROM_PTR` flat-sub and `PTR_FROM_BLOCK` flat-add are
self-consistent with no 16-bit carry on this 49 KB heap — verified in `gc.ssa`).  So the break is
a RUNTIME/alignment effect not visible statically.  §4q must instrument the MARK phase directly:
- In `py/gc.c` `gc_sweep_free_blocks`, print the address of every block freed during the X00
  collection; confirm the `qstr_last_chunk` block (whose addr = the healthy `qstr_str(219)` rounded
  to its chunk HEAD) is among them → proves swept-while-live and gives the chunk's block #.
- Then in `gc_collect_start`/`gc_mark_subtree`, trace whether that chunk block ever gets
  `ATB_HEAD_TO_MARK`'d, and if the pool-chunk scan produces the chunk-base pointer and what block
  `BLOCK_FROM_PTR(chunk_base)` computes.  The period-64 = ATB-byte hint points at a block↔ATB
  index mismatch or a far-ptr representation divergence on ONE link of the chain.
- **CAUTION**: the `dbg_churn_atb` ATB-kind probe added to the port `gc_collect` this session is
  UNRELIABLE — it printed `k=0` (FREE) for the churn block in PASS runs too (impossible for a live
  block), so its far-byte ATB read (`mp_state_ctx.mem.area.gc_alloc_table_start[blk/4]`) is itself
  miscompiled/misreading.  FIX or replace that probe before trusting it; don't read the §4p `CB`
  lines as truth.  (It may even hint at a SECOND minic far-array-index bug — worth a 2-line probe.)

### MAME headless watchpoint — RULED OUT in this MAME (0.287); needs a source patch
The §4o "approach 1" (debugscript `wpset`) does NOT work: MAME's `process_source_file()` only runs
commands **while the CPU is stopped** (debugcpu.cpp), but `-debugger none`'s `wait_for_debugger`
immediately `go()`s (and `DEBUG_FLAG_OSD_ENABLED` is always set, machine.cpp:95), so the
debugscript is opened but NEVER executed → `wpset`/`trace` produce nothing (matches §4o's empty
output).  gdbstub only supports the i486 reg map (`debuggdbstub.cpp`), NOT the Victor's 8088.
A watchpoint's ACTION *does* run in `debug_watchpoint::triggered()` regardless of frontend, and
`trace`/`tracelog` write to a file — so the ONLY missing piece is getting the initial `wpset` to
run.  **Fix if pursued**: patch `~/projects/mame/src/osd/modules/debugger/none.cpp`
`wait_for_debugger` to call `m_machine->debugger().console().process_source_file()` before `go()`,
then rebuild MAME (slow full relink).  New harness `tools/run-victor-wp.sh` (committed) already
drives `-debug -debugger none -debugscript` + captures the trace file; it's ready once none.cpp is
patched.  Lower priority than the serial gc.c instrumentation above.

### Reproduction cheat-sheet (verified this session)
- Shipping clean FAIL image restored: `build/mp-link/mpython.exe` heap_seg **0xBA8B** body 817840.
- The FAIL alignment is **NOT predictable from the .map** (it depends on the *runtime-normalized*
  `gc_pool_start` segment mod 4, not `main_BSS` para mod 4 — those differ by a build-dependent
  offset).  So after ANY instrumentation edit, SWEEP `--stack-size` over 4 paragraphs (relink-only,
  to /tmp/*.exe) and run all 4; ~2 of 4 FAIL.  Recipe used:
  `for ss in 16384 16400 16416 16432; do omf_link.py -o /tmp/hN.exe --stack-size $ss --gc-sections
  --pack-code <objs>; done` then `run-victor-sasi.sh /tmp/hN.exe 240` (parallelises; ~2.5 wall-min
  each, 4 parallel competes for cores so ~4 min).
- Instrument via `MP_EXTRA_CPPFLAGS="-DMP_DBG_GLOBALS=1" tools/recompile-mp-tu.sh runtime …` then
  `… main …` (relinks).  Image ceiling ~824416 body; the §4p probes (qstr dump + table dump) just
  fit at ~820400 — keep probes lean.  `/tmp/mp_objs.txt` (109 objs) current.
- The §4p instrumentation is LEFT IN PLACE (guarded) in `~/projects/micropython/py/runtime.c`
  (`mp_load_global` NFq+qstr dump + `dbgp_s`/`dbgp_x` helpers) and `ports/dos8086/main.c`
  (`dbg_churn_atb` + the older `dbg_dump_globals` under `MP_DBG_GC`).  Reuse for §4q.

---

# (§4p done above) Next session (§4p — FIND THE WRITER.  §4o nailed the churn(120) corruption to a FAR-DATA-SEGMENT-ALIGNMENT-sensitive wild write with a clean PERIOD-4 signature (FAIL iff far-data seg ≡ {0,3} mod 4), reproducible/maskable purely via `--stack-size` (relink-only).  This also CRACKS the §4k "instrumentation hides it" heisenbug — it was just the far-data segment's mod-4 flipping — so instrumentation is now usable by re-pinning a FAIL alignment with `--stack-size`.  Two armed approaches below.)

## 2026-06-09 §4o notes (BISECT cracked the heisenbug: period-4 far-data-segment-alignment sensitivity; instrumentation now unblocked)

**§4o ran the layout bisect (the §4n approach #1) and it paid off hugely.**  No source
changed; all work was relink-only experiments + reading generated SSA.  The shipping image
(`build/mp-link/mpython.exe`, body **817840**, heap_seg **0xBA8B**) was confirmed to
deterministically FAIL: `mp-churn-scale2.py` prints churn(20…100) correct then churn(120)
→ `NameError` (markers `DE` then `C5`) — exactly §4j.

### The bisect lever: `--stack-size` shifts the far-data SEGMENT, relink-only, no recompile
`STACK` (para 0xB0A1) sits BEFORE every `FAR_DATA` segment, and CODE + near-`DGROUP`
(0xA790) sit before STACK.  So growing `--stack-size` by Δ shifts EVERY far-data segment
(qstr/objstr/…/`main_BSS` heap) up by Δ/16 paragraphs **as a unit**, while CODE and
near-data stay put, and the intra-heap layout (offsets) is byte-identical.  Recipe (≈5 s,
no TU recompile):
```
OBJS=(); while IFS= read -r l; do [ -n "$l" ] && OBJS+=("$l"); done < /tmp/mp_objs.txt
tools/omf_link.py -o /tmp/X.exe --map /tmp/X.map --entry _start \
    --stack-size $((16384+16*k)) --gc-sections --pack-code "${OBJS[@]}"
# heap_seg (main_BSS para) = 0xBA8B + k
```
Relink at k=0 (stack 16384) is **byte-identical** to the committed `mpython.exe` — lever
validated.

### THE RESULT — clean PERIOD-4 flip in the far-data segment value
8 runs on real Victor (`run-victor-sasi.sh`, scale2), one per paragraph:
| stack | heap_seg | seg mod 4 | result |
|---|---|---|---|
| 16384 | 0xBA8B | 3 | **FAIL** |
| 16400 | 0xBA8C | 0 | **FAIL** |
| 16416 | 0xBA8D | 1 | PASS |
| 16432 | 0xBA8E | 2 | PASS |
| 16448 | 0xBA8F | 3 | **FAIL** |
| 16464 | 0xBA90 | 0 | **FAIL** |
| 16480 | 0xBA91 | 1 | PASS |
| 16496 | 0xBA92 | 2 | PASS |

Clean `FFPPFFPP`: **FAIL iff far-data segment ≡ {0,3} mod 4, PASS iff ≡ {1,2}** (period 4
in the segment = period **64 bytes** in the linear base; equivalently the far-data base's
linear-address bits 4–5).  The shipping image is mod-4 = 3 → FAIL.

### What this proves / re-opens
- **It IS a layout/segment-sensitive WILD WRITE** (§4o's hypothesis), and the sensitive
  quantity is the far-data base's **alignment mod 64 bytes** — i.e. some far-pointer
  computation whose overshoot AMOUNT depends on a pointer's segment low-2-bits.  There is NO
  explicit `& ~0x3F`/`+0x3F` alignment mask in the generated asm (grepped) — so it is a
  subtler carry/shift interaction, not a literal round-up.
- **CRACKS the §4k "instrumentation hides it" heisenbug.**  Adding code shifts the far-data
  base segment's mod-4, which has a 50 % chance of flipping FAIL→PASS — that is the entire
  "heisenbug."  **Decoupling fix:** add instrumentation freely, then re-pin a FAIL alignment
  with `--stack-size` (it moves the segment mod-4 INDEPENDENTLY of code size).  This removes
  the §4e/§4k wall — runtime instrumentation is finally usable on this bug.
- **§4l's "GC core is CLEAN" is NOT conclusive.**  That standalone probe ran at some other
  far-data segment whose mod-4 was likely a PASS value, so a segment-mod-4-sensitive GC op
  would not have fired.  **The GC is back on the suspect list** alongside the VM/runtime —
  but only for an op that actually uses a far pointer's SEGMENT in arithmetic.
- The GC MARKING itself is segment-robust for genuine roots (read `gc_collect`,
  `gc_collect_root`, `gc_mark_subtree` SSA): every genuine heap pointer (stack-resident or
  in-object) shares the heap segment, so the same-segment `VERIFY_PTR` bounds checks and the
  `(ptr-pool_start)/16` block math are segment-independent.  So the writer is more likely a
  far STORE in the churn workload (list-comp fill / dict store+rehash / `str(i)` intern) than
  in marking — but verify, don't assume.

### LATENT BUG found en route (NOT the churn cause; fix separately)
minic lowers C pointer **relational** comparisons (`<`,`<=`,`>`,`>=`) as **SIGNED**
(`cslel`/`csltl` in the `VERIFY_PTR` SSA), but C pointer comparisons must be UNSIGNED.
Harmless in THIS image only because every segment is ≥ 0x8000 (all "negative", so signed
ordering matches unsigned), but it is wrong whenever pointers straddle the 0x8000 segment
boundary.  Real bug, own probe + fix when convenient (same family as
[[feedback-minic-unsigned-widen-extsw]] §2r and §4h).

### THE GOAL FOR §4p — catch the writer's PC (now well-armed)
Two approaches, both newly viable:
1. **MAME debugger watchpoint on the shipping FAIL image** (heap_seg 0xBA8B, no rebuild → no
   perturbation).  `mp_state_ctx` is at a FIXED addr (`mpstate_BSS` para 0xBA7A : off 0 in
   the 817840 build; see `mpython.map`).  Boot under `~/projects/mame/mame victor9k … -debug
   -debugger none -debugscript <f>` (adapt the launch from `run-victor-sasi.sh`); break after
   `mp_init` (e.g. at the `C4` marker tx, or `do_str` entry); read `thread.dict_globals` /
   `vm.last_pool` far pointers from mp_state_ctx (offsets per §4n: dict_locals@8,
   dict_globals@12, vm.last_pool@32) to get the long-lived globals-dict / qstr-pool heap
   address; `wpset` a write-watch on its `map.table` / chunk bytes; `go`; the PC that writes
   garbage → the offending function → the far-arith.  Work item = headless 8088 segmented
   debugger scripting (physical addr = seg*16+off; reading a far ptr = combine two words).
   **MAME feasibility ALREADY TESTED this session (partial):** `~/projects/mame/mame victor9k
   … -debug -debugger none -debugscript F -debuglog` runs **headless without hanging**, a
   debugscript of `printf "…",pc` + `go` executes and the machine runs to completion
   (`Average speed: …` on stdout), and `debug.log` (in cwd) is created.  **OPEN PROBLEM = the
   OUTPUT channel:** under `-debugger none` the console is dropped, so the `printf` text does
   NOT reach `debug.log` (only the debugger banner does), and `trace <file>` / `wpset …` either
   error or produce nothing (run exits ~2 s with empty stdout, no trace file, no error in
   debug.log).  So a watchpoint that FIRES can't yet be observed.  NEXT: solve emission — try
   `-debugger gdbstub -debugger_port N` then drive via `gdb` (set the wp + `commands`), OR find
   the correct MAME-0.287 `trace`/`wpset` action syntax that lands in a file, OR `-oslog`.  A
   reusable disk (FAIL `mpython.exe` + scale2 PROG.PY) is staged at `/tmp/mamedbg/run.img`
   (rebuild via `vtg_image_util copy … :0:\\PROG.EXE` / `\\PROG.PY`); the exact launch flags
   are in this session's transcript.
2. **Instrument the external micropython (now that --stack-size re-pins FAIL).**  Add to
   `~/projects/micropython/ports/dos8086/main.c` (or py/gc.c) a check that CHECKSUMS the
   globals dict (`mp_state_ctx.thread.dict_globals` → its `map.table` entries) and/or the
   first qstr-pool chunk after EACH churn iteration (or each `gc_collect`), printing the
   iteration where it first changes unexpectedly → which churn op corrupts it.  Build with
   `tools/build-micropython.sh --model=compact` (or relink one TU via `recompile-mp-tu.sh`),
   then RELINK at a `--stack-size` that lands the far-data segment on mod-4 ∈ {0,3} (the
   instrumented body size differs, so compute k to hit a FAIL seg — try a few; each MAME run
   ~2.5 wall-min with `-nothrottle`).  This is the §4e marking-completeness plan, finally
   unblocked.

### Reproduction cheat-sheet (verified this session)
- FAIL (shipping): `VICTOR_SRC=build/mp-churn-scale2.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`
- Toggle alignment by relink (above); FAIL k∈{0,1,4,5,…} (seg mod4∈{3,0}), PASS k∈{2,3,6,7,…}.
- Runs PARALLELISE cleanly (separate temp dirs/serial files) — ran all 6 intermediates at once.
- `/tmp/mp_objs.txt` (109 link objects) is current; `build/mp-link/` is fully populated.

---

# (§4o done above) Next session (§4o — the churn(120) corruption is now isolated to a LAYOUT-SENSITIVE NON-GC WILD WRITE — the ONLY hypothesis left after EVERY structural/algorithmic one was ruled out (GC core §4l, all live-type ptr alignment §4m, mp_state root scan §4n, mark-stack overflow §4k).  Static analysis is fully spent.  §4o MUST observe the wild write at runtime on the shipping image without perturbing it (instrumentation hides it — §4k).  §4i+§4j fixed+verified the far-ptr bug; three GC probes gated.)

## 2026-06-09 §4n notes (mp_state root-scan re-audit — CLEAN; suspect B ruled out; only the wild-write hypothesis remains)

**§4n extended `gc_offset_probe.c`** (the gated §4m audit) to the mp_state root section that
`gc_collect_start` scans, and **statically confirmed it is correct** — closing suspect B
without any MAME run.  The scan covers `[offsetof(thread.dict_locals), offsetof(vm.qstr_last_chunk))`
at a void**=4 stride; the probe prints (compact/large, far-data):
- `root_start`(dict_locals)=**8** (4-aligned — §4g padded it from the packed-6), `root_end`
  (qstr_last_chunk)=**108**, scan window `[8,108)`.
- Every root pointer is 4-aligned AND inside the window: thread.dict_locals@8, dict_globals@12,
  nlr_top@16, pending_exc@24; vm.last_pool@32, dict_main.base@64, dict_main.map.table@72,
  readline_hist@76 (the last root, ending exactly at 108).
So `gc_collect_start` finds every mp_state root → **suspect B (a missed root) is RULED OUT**,
which also agrees with "16 KB works" (a missed root is layout/heap-size-INDEPENDENT and would
fail on the small heap too).

### The suspect set is now a SINGLE hypothesis
RULED OUT (static + faithful repro): GC core algorithm/codegen (§4l); pointer-field alignment
in every live object type incl. the value stack (§4m); the mp_state root scan (§4n); mark-stack
overflow (§4k).  ONLY ONE hypothesis fits ALL the evidence —
**a LAYOUT-SENSITIVE NON-GC WILD WRITE**: some compiled VM/runtime code (NOT the GC, NOT the
gate-verified addfo path) does far-pointer arithmetic that, on the 49 KB heap's specific
address range, computes a target a little past an object and overwrites a live object (the qstr
pool / globals dict → NameError).  This is the UNIQUE fit for: §4k's heisenbug (a ~32-byte
image shift moves the target onto harmless padding); "16 KB works, 49 KB fails" (the bad target
only lands on a live object when the heap occupies the larger offset range); and all
GC/structural analysis being clean.

### THE GOAL FOR §4o — find the wild write at runtime (the only avenue left)
A wild write's target depends on RUNTIME addresses, so no static audit can find it — §4o must
observe it on the SHIPPING `build/mp-link/mpython.exe` (no source change → no layout shift;
§4k proved any added code hides it).  Concrete approaches, by tractability:
1. **Layout-sensitivity BISECT (most tractable, mechanical).**  Add a sized UNUSED global
   (e.g. `static char pad[N];` in a TU, or a linker pad) to shift the image by N bytes, and
   binary-search N over [0, 64] for the fail↔pass threshold (each step = recompile-one-TU +
   one ~3-min MAME run).  §4k bracket: clean 817840 FAILS, +112 (817952) PASSES.  The flip
   granularity (does it flip every 2 / 4 / 16 bytes?) reveals the target's alignment, and the
   absolute address at the flip, cross-referenced with `build/mp-link/mpython.map`, localizes
   WHICH data region gets clobbered → which code writes near it.
2. **MAME debugger observation (definitive, harder).**  Boot under MAME `-debug` with a
   `-debugscript`: break at `gc_collect` (or `do_str`), walk `mp_state_ctx` (static addr from
   the .map) → dict_globals → map.table to get the live globals-table heap address, `wpset`
   write-watch it, continue, and catch the PC of the instruction that writes garbage to it.
   That PC → the function → the offending far-arith.  Headless MAME debugger scripting
   (expressions reading memory via `dword(...)`, conditional `wpset`) is the work item; adapt
   the launch from `tools/run-victor-sasi.sh`.
3. **Suspect-guided code audit.**  The wild write is far-arith that overshoots on large
   offsets.  §4i fixed `far_ptr ± idx` (addfo, gate-verified); look for OTHER far-arith shapes
   minic emits that DON'T go through addfo and could overrun: e.g. `memcpy`/`memmove`/`memset`
   length or dest computed with a far pointer near the segment top, struct-copy byte loops,
   `m_renew`/array-grow far-pointer recomputations, or a far-pointer COMPARE used as a bound
   that mis-orders at high offsets.  Grep the generated `build/mp-link/*.asm` for far stores
   (`mov [es:...]`) whose address is computed by a non-addfo add/adc pair.

   **Already CLEARED under approach 3 (don't re-tread):**
   - **libstub `_far_memset`/`_far_memcpy`/`_far_memmove` are correct for heap buffers** — the
     count is 16-bit (size_t=2) and `dest+count` stays inside the heap `[0x1200,0xD200]` (<
     0xFFFF), so `rep stosb/movsb`'s DI never wraps the segment.  Not the overshoot source.
   - **A missed REGISTER root (live far ptr only in a callee-saved reg across the collection,
     not on the scanned C stack) is ruled out** by spill.c `force_kl_slot`: every Kl far-ptr
     temp is slot-resident, so the value is always in a stack slot the conservative scan
     covers.  §4l's repro (which roots its retained chain ONLY via the same C-stack scan +
     globals) was clean, corroborating the invariant.  Also, a missed register/root would be
     layout-INDEPENDENT (regalloc is fixed per binary), contradicting §4k.
   So approach 3 should focus on **minic-emitted far STORES from the VM/runtime** (objstr /
   objlist / vm value-stack writes / mp_obj_new_* fill loops) whose dest far-arith is NOT the
   addfo path and could compute a target past the object — OR just do the runtime bisect (1)
   which doesn't depend on guessing the shape.

### Three gated GC probes (regression guards locked in this session)
`gc_churn_probe` (§4l) — faithful GC-core + far-ptr-fix guard.  `gc_offset_probe` (§4m+§4n) —
§4g far-data alignment guard for every live object type AND the mp_state root section.  Plus
`gc_bigheap_probe` (§4i).  Gate 228/228 (gc_offset_probe golden extended with the mp_state
lines; same 2 gate entries).

---

# (§4n done above) Next session (§4n — all STRUCTURAL/algorithmic GC hypotheses for churn(120) are now EXHAUSTED by static audits + a faithful repro; the bug is a LAYOUT-SENSITIVE NON-GC WILD WRITE (or an mp_state-scan residual) that needs RUNTIME OBSERVATION of the shipping image.  §4i+§4j fixed+verified the far-ptr bug; §4k=heisenbug; §4l=GC core CLEAN; §4m=all live-type pointer fields 4-aligned.  Two probes gated.  No more static angles — observe the corruption at runtime without perturbing.)

## 2026-06-09 §4m notes (static layout audit — all live-type pointer fields are 4-aligned; structural hypotheses exhausted)

**§4m built `minic/dos/examples/gc_offset_probe.c`** (GATED compact+large, no GC at runtime —
pure `offsetof`/`sizeof` prints; struct defs copied VERBATIM from `build/mp-link/*.pp.c`).
It verifies §4l's hypothesis #1: does every far-POINTER field in MicroPython's live heap
object types sit at a `sizeof(void*)`=4-aligned offset, so the conservative GC's 4-stride
`gc_mark_subtree` scan finds it (a 2-mod-4 pointer is split across reads → freed-while-live,
the §4f bug class)?

**RESULT: ALL 4-ALIGNED** under far-data (`sizeof void*=4, size_t=2, mp_obj_t=4`):
- `qstr_pool_t`: prev@0, **lengths@12, qstrs@16** (§4g correctly padded lengths from the
  packed-10 to 12), sizeof 16.
- `mp_map_t.table`@4; `mp_map_elem_t.key`@0/`value`@4 (sizeof 8); `mp_obj_dict_t` → `map.table`@8.
- `mp_obj_list_t.items`@8.
- stackless `mp_code_state_t`: fun_bc@0, ip@4, sp@8, old_globals@16, prev@20, **value stack
  state[]@24** — all 4-aligned (sizeof 24).
So §4g's alignment is correct for every type behind a name lookup AND the live value stack →
the GC scan finds every child pointer.  **Hypothesis #1 (off-stride pointer field) is RULED OUT.**

### Suspect set after §4k+§4l+§4m (structural causes exhausted)
RULED OUT: GC core algorithm/codegen (§4l); pointer-field misalignment in
qstr_pool/map/dict/list/code_state incl. the value stack (§4m); mp_state root-scan alignment
(§4g, offsetof(thread.dict_locals) 4-aligned); mark-stack overflow (§4k).
REMAINING (the only ones left):
- **(A) A layout-sensitive NON-GC WILD WRITE** — far-pointer arithmetic somewhere in the
  compiled VM/runtime (NOT the GC, NOT the simple gate-verified addfo path) that, on the big
  heap's specific addresses, overruns an object boundary into a live object.  This is the BEST
  fit for §4k's heisenbug profile (the overrun lands on something critical in one image layout,
  harmless padding in another).
- **(B) An mp_state root-scan residual** not caught by §4g's single alignment check (e.g. the
  `gc_collect_start` scan's start/length rounding misses a root at the section's edge, or a
  root field added since §4g).  Cheap to re-audit statically: extract `mp_state_ctx_t`/
  `mp_state_thread_t`/`mp_state_vm_t` from a `*.pp.c`, confirm offsetof(thread.dict_locals)%4==0
  and that every pointer between it and offsetof(vm.qstr_last_chunk) is 4-aligned AND inside the
  scanned `[root_start/4*4, root_end)` window.

### THE GOAL FOR §4n — observe the corruption at runtime (static angles are spent)
Static audits + the faithful repro have exhausted the structural hypotheses, so the next step
MUST observe the actual wild write/freed object at runtime on the SHIPPING image (no source
change → no layout perturbation; §4k proved instrumentation hides it).  Options, hardest-payoff
first:
1. **MAME debugger, non-perturbing.**  Boot `build/mp-link/mpython.exe` under MAME `-debug`
   with a `-debugscript` file (adapt the launch from `tools/run-victor-sasi.sh`).  The
   corrupted memory is dynamic (heap), so a fixed `wpset` is hard; instead consider: (a) a
   TRACE of all far writes (`mov [es:...], ...`) whose target is in the heap segment during the
   churn(120) collection window, diffed against a passing run; or (b) break at `gc_collect`
   entry/exit and DUMP the qstr-pool / globals-dict region (addresses found by walking
   mp_state_ctx in the debugger) to see WHICH bytes change to garbage and WHEN.  Headless MAME
   debugger scripting is the real work item.
2. **Static re-audit of the mp_state root section** (suspect B above) — purely static, cheap,
   do it FIRST as it may close B without any run.
3. **Bisect the layout sensitivity** to localize: pad the image by N bytes (a sized unused
   global) in small steps and find the fail↔pass threshold; the threshold maps to which data
   region's address alignment triggers the wild write — narrows (A) to a specific structure.

### Two gated probes this session
`gc_churn_probe` (§4l, GATED compact+large) — faithful GC-core + far-ptr-fix guard.
`gc_offset_probe` (§4m, GATED compact+large) — §4g far-data alignment guard (a future minic
alignment regression would re-introduce the §4f freed-while-live class; this catches it).
Gate 226 → 228.

---

# (§4m done above) Next session (§4m — the churn(120) GC corruption is NOT in the GC core algorithm: a faithful self-contained mark/sweep repro on a 49 KB far-data heap (18 collections, multi-level marking, far-array indexing) is CLEAN.  The bug is in the MicroPython-specific layer (object internals / mp_state roots) or a layout-specific wild access from non-GC code.  Next: a NON-PERTURBING MAME-debugger watchpoint on the real image (instrumentation hides it).  §4i+§4j DONE; §4k+§4l narrowed it sharply.)

## 2026-06-09 §4l notes (built the fast-repro; it CLEARS the GC core; bug is MicroPython-layer or a non-GC wild access)

**§4l built `minic/dos/examples/gc_churn_probe.c`** — a self-contained, faithful copy of the
gc.c CORE (2-bit ATB FREE/HEAD/TAIL/MARK, gc_setup_area table/pool split, gc_alloc first-fit
block scan, the bounded mark stack(64) + gc_deal_with_stack_overflow rescan, sweep, and a
conservative dual-aligned root+C-stack scan), driven by a churn workload that EXPLICITLY
verifies a retained singly-linked chain (40 nodes) AND a two-level dict-like container
(header → far table[] → value nodes) after each forced collection.  The explicit verify makes
it MORE sensitive than MicroPython (which only notices corruption when a wild access hits the
qstr pool/globals and raises NameError).  GATED compact+large (golden layout-independent —
counts derive from HEAP_BYTES; identical compact/large).  Gate **224→226**.

**RESULT: CLEAN.** compact AND large far-data, 49 KB heap, **18 collections** under heavy churn
(varied-size garbage → fragmentation; 0 overflows, consistent with §4k), the retained chain
AND dict survive every collection → `ALL OK`.  So the GC core is correct on far-data:
mark/sweep/alloc, the conservative dual-aligned scan, MULTI-LEVEL marking (dict→table→values),
FAR-ARRAY indexing (`table[i]` = the §4i addfo path), and varied sizes/fragmentation are all
fine.  **The MicroPython churn(120) corruption is NOT reproduced by a faithful standalone GC.**

### What this RULES OUT and what's LEFT
RULED OUT (by §4k + §4l): mark-stack overflow path; the GC core mark/sweep/alloc algorithm;
the conservative scan; multi-level marking; far-array indexing; far-ptr arith (addfo).
LEFT (the bug must be one of):
1. **MicroPython object INTERNALS** the probe doesn't model — a specific type whose pointer
   still sits at a non-stride offset despite §4g's struct-member alignment (e.g. a flex-array
   member, a union, an embedded sub-struct, or the qstr_pool/str/dict-map exact layout), so
   `gc_mark_subtree`'s sizeof(void*) stride skips a live child.  → Re-audit the ACTUAL offsets
   of pointer fields in the live object types (qstr_pool_t, mp_obj_dict_t/mp_map_t,
   mp_obj_str_t, the stackless code_state frame) in the GENERATED far-data layout, not on paper.
2. **The mp_state root section** scan (my probe used simple explicit roots) — re-verify
   offsetof(thread.dict_locals)..vm.qstr_last_chunk are all 4-aligned AND the void**-stride
   `gc_collect_start` scan covers every root in the real generated struct.
3. **The stackless VM value-stack / code_state** rooting — frames are heap objects reached via
   the code_state chain / a C-stack pointer; a live value-stack slot at a deep collection may
   not be covered.
4. **A non-GC WILD WRITE** — far-arith somewhere in the VM/runtime that, on the big heap's
   specific addresses, writes past an object into a live one (consistent with §4k's
   layout-sensitivity: it hits something critical in one layout, padding in another).

### THE GOAL FOR §4m — a NON-PERTURBING observation of the real image
Since the bug is layout-sensitive (any added code hides it — §4k) and a simplified probe
doesn't reproduce it (§4l), stop trying to add markers/probes.  Use a **MAME debugger memory
WATCHPOINT** on the SHIPPING clean image (no source change → no layout perturbation): boot
`build/mp-link/mpython.exe` under MAME `-debug` with a scripted command file, set a write
watchpoint on the qstr-pool / a known-live object's memory (address from the `mpython.map` for
statics, or discovered live), run `mp-churn-scale2.py`, and catch the instruction that writes
the wild value.  That PC → the offending function → the codegen/source bug.  Headless MAME
debugger scripting is the hard part (a `-debugscript` file with `wpset`/`bpset`/`trace`); the
Victor harness (`tools/run-victor-sasi.sh`) shows the launch/serial plumbing to adapt.
Alternative if watchpoints are impractical: audit the generated far-data field offsets of the
live object types (item 1 above) directly from the `build/mp-link/*.ssa`/`*.asm` — purely
static, no runs, no perturbation.

### Probe is also a permanent regression guard
`gc_churn_probe` stays gated: it's the strongest in-tree exercise of the §4i far-ptr fix +
GC-core far-data correctness (multi-level marking + far-array indexing across 18 real
collections).  Build: `QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact
minic/dos/examples/gc_churn_probe.c`; the heap size is `-DGC_HEAP_BYTES` overridable (49152
default; a NEAR-data/medium build needs a small heap — a 49 KB near heap overflows DGROUP,
which itself proves the bug requires far data).

---

# (§4l done above) Next session (§4l — churn(120) GC corruption on the 49 KB heap is a LAYOUT-SENSITIVE near-miss; the MAME loop is unsuitable to chase it (instrumentation hides it).  Build the MEDIUM-MODEL DOSBox fast-repro.  §4i+§4j are DONE (far-ptr fix landed, gate 224/224, Victor-verified).  §4k narrowed the residual: overflow RULED OUT, collections work, 16 KB clean — it's a layout-dependent heap-corruption heisenbug.)

## 2026-06-09 §4k notes (diagnosed the churn(120) corruption: layout-sensitive heisenbug; overflow ruled out; NO qbe code change)

**§4k is a DIAGNOSIS pass on the §4j-surfaced churn(120) NameError — no compiler bug
found, no qbe source changed (only a fast-loop harness fix + docs).**  The bug is in the
MicroPython conservative GC under heavy churn on the BIG heap; it is **layout-sensitive**
(a near-miss wild access), which makes the slow + perturbing MAME loop the wrong tool.

### What was measured (real Victor, `tools/run-victor-sasi.sh`, scale2 = churn 20→120)
All builds compact far-data, stackless, MP_STACK_SIZE=16384.  Markers added temporarily to
EXTERNAL `py/gc.c` (now reverted): `O` = a mark-stack overflow round (`gc_deal_with_stack_overflow`);
`g` = one per collection (`gc_collect_start`).
| build | heap | result |
|---|---|---|
| clean §4i (body 817840) | 49 KB | **FAIL** churn(120) `NameError` (the §4j result) |
| + `O` marker only (817920) | 49 KB | **FAIL** churn(120) `NameError`, and **NO `O`** → overflow path never fires |
| + `O`+`g` markers (817952) | 49 KB | **PASSES** `120 7980` DONE, only 2 collections (`g` at churn100, churn120), no `O` |
| + `O`+`g` markers | 16 KB | **PASSES** `120 7980` DONE, ~8 collections, no `O` |

### Conclusions (sharp)
1. **Mark-stack overflow is RULED OUT** — `O` never printed before the failure.  The
   `gc_deal_with_stack_overflow` O(blocks) path is NOT involved (so raising
   `MICROPY_ALLOC_GC_STACK_SIZE` is moot, again).
2. **Collections themselves work** — 16 KB does ~8 clean collections and completes; the
   markers don't change marking logic.
3. **It is a LAYOUT-SENSITIVE heisenbug.**  A ~32-byte image shift (adding the `g` marker:
   817920 FAIL → 817952 PASS) makes the corruption vanish.  MAME is deterministic
   per-binary, so this is across BINARIES (layout), not across runs — but the *shipping*
   clean build (817840) deterministically FAILS.  The corruption is a near-miss wild
   access that lands on something critical in the clean layout and on harmless padding in
   the shifted layout.  → ANY on-target instrumentation perturbs the layout and can hide
   the bug, so the MAME loop is the WRONG tool.
4. It fires at the churn(120) collection point on the big heap (the marker build's 2nd `g`
   is exactly there).  On 49 KB, `MICROPY_GC_ALLOC_THRESHOLD`=0 means collection only
   happens when the heap is nearly full, so the *first* collection is very late (deep into
   churn) with a large, specific live state — unlike 16 KB which collects early and often.
   This is the §4d/§4e/§4f "a LIVE heap object is freed across a collection" family; §4g's
   struct-alignment fix cleared it for moderate pressure (feature-probe) and the small heap,
   but not for this big-heap late-collection case.

### THE GOAL FOR §4l — build the MEDIUM-MODEL DOSBox fast-repro (the §4e plan's key tool)
The MAME loop (~3 min/run) + layout-sensitivity make on-target debugging impractical.  Build a
self-contained DOS probe that links `py/gc.c` with stub `mp_state`/`mphal`, allocates
cross-linked objects, drops most, forces `gc_collect`, reallocates, and verifies a retained
object's contents — at DOSBox speed (seconds/iter), where layout can be controlled and
instrumentation added freely.
- If it reproduces in the MEDIUM model (near-data, 2-byte pointers) → it's a GC LOGIC bug
  (mark/sweep/block-math), debuggable fast in DOSBox.
- If it does NOT reproduce in medium but DOES in a compact/far-data probe → it's
  far-data-specific (a far load/store / far-ptr value read at a wrong offset in the
  collection paths), which itself narrows it sharply.
- Either way, instrument MARKING COMPLETENESS directly (§4e step 1): capture a known-live
  object's block before the collection, assert its ATB kind != FREE after `gc_collect_end`,
  and print WHICH block is swept-while-live.  In a self-contained probe the live set is
  known exactly, so the smoking gun is unambiguous.
Suspects to check in the probe (codegen is otherwise clean — far-ptr arith now via addfo):
the conservative C-stack scan range `[sp, stack_top)` vs where a live root actually sits at
the deep collection point; a multi-block live object whose tail words (child pointers) aren't
traced; or a far-pointer VALUE inside a live container read at a wrong offset by the trace.

### Harness fix committed this session (real bug found while diagnosing)
`tools/recompile-mp-tu.sh` defaulted `MP_STACK_SIZE=24576` but `build-micropython.sh` uses
**16384** (the §4b stackless default), so a fast-loop relink reserved 8192 more stack than the
full build → a clean gc.c relink came out **826032**, OVER the ~824416 "Program too big"
ceiling → the relinked .exe would not load on Victor.  Fixed the default to 16384 (override via
env).  Use `MP_STACK_SIZE=16384` explicitly if on an older copy.  Reminder: the
`gc_bigheap_probe`/§4i scope-note items (huge `_qbe_huge_add` >=0x8000 gap; build-example.sh
-DFAR_DATA) are still open, independent, lower-priority.

---

# (§4k diagnosed above) Next session (§4k — the NEW frontier is churn(120) GC-pressure corruption on the 49 KB heap.  §4i+§4j are DONE: the offset-only far-pointer fix LANDED, is gate-verified 224/224, AND is Victor-verified on real MicroPython — the far-ptr churn(80) stall is GONE.  The remaining failure is a SEPARATE, newly-reachable GC bug.)

## 2026-06-09 §4j notes (VICTOR RE-VERIFY of the §4i far-pointer fix — DONE; far-ptr stall fixed; a further GC frontier surfaced)

**§4i+§4j land the far-pointer fix end-to-end.**  §4i implemented offset-only
far-pointer arithmetic (`addfo`/`subfo`); §4j re-verified it on a REAL Victor 9000
(MAME, `tools/run-victor-sasi.sh`, compact far-data, stackless, 49 KB heap — the exact
config where §4g/§4h saw the churn(80) stall).  **The far-pointer bug is fixed on the
actual consumer**, and a §4i refinement (below) made the MicroPython image SMALLER.

### §4i refinement committed after the first build: VARIABLE-index only
The first §4i `far_ptr_offset_binop` fired for BOTH constant and variable offsets.
That GREW the MicroPython image +2304 B (body 820400→822704) because a CONSTANT scaled
offset (`arr[const]`, `&arr[const]`, `p + const`) that QBE used to FOLD into a single
relocated `CAddr` was being routed through the opaque (non-foldable) `addfo`, defeating
the fold.  The §4h scope was always **variable-index only** (the bug needs a runtime
index >= 0x8000 or a runtime-wrapped negative delta; a constant is folded + linker-resolved).
Added `if (soff.t == Con) return 0;` to `far_ptr_offset_binop`.  Net effect: constant
far-arith folds again AND variable far-arith drops the `adc` → image **820400 → 817840
body (-2560 B vs the §4g baseline)**, more headroom under the ~824416 ceiling.  Gate
re-run **224/224 ok** (the restriction changes no runtime output — constant far-arith
reverts to the previously-passing flat add).

### Victor results (real hardware; redirect-to-file, never pipe through tail — [[feedback-victor-harness-pipe-buffer]])
| probe | result |
|---|---|
| `build/mp-churn-scale2.py` (49 KB heap) | `20 330` `40 1060` `60 2190` **`80 3720` `100 5650`** then churn(120) → `NameError`.  **churn(80)/(100) now CORRECT — was a hard hang at churn(80) pre-§4i.** |
| `build/mp-feature-probe.py` | **23/23 OK** (mul…enum incl. `comp`/`gen`/`sort` — the exact checks the §4h naive-extuw attempt REGRESSED to `ER list`/`XX gen`).  No regression. |
| `build/mp-fill-probe.py` | clean: all 16 markers `0`…`960` + `D4`/`C5` — 1000 iters force MANY gc_collects on the 49 KB heap (non-retained garbage), no corruption/hang. |

### THE NEW FRONTIER FOR §4k — churn(120) NameError (a SEPARATE GC-pressure bug, NOT far-ptr)
`scale2` now advances from the old churn(80) hang all the way to **churn(120)**, where it
raises `NameError: name not defined` at module scope (marker `DE`, then clean `C5`).  This
is NOT the far-ptr bug (the 49 KB heap maxes at offset ~0xC000, all < 0x10000 and all
covered by addfo; churn(80)/(100) at the same offsets are correct) and NOT a §4i regression
(pre-§4i it never even reached churn(120) — strictly more iterations complete correctly now).
It is the **§4d/§4e/§4f "a live heap object is freed across a GC collection under heavy
churn"** family — `NameError` at module scope = a freed-while-live qstr_pool / global-dict
(the §4f symptom).  §4g's struct-alignment fix cleared it for MODERATE pressure
(feature-probe 23/23, fill-probe clean) but NOT for scale2's extreme churn on the big heap —
and §4g never actually confirmed scale2 completing on 49 KB (it was blocked first by the
far-ptr bug, then mis-attributed to a perf cliff).  Now the far-ptr bug is gone, this is the
exposed remaining issue.  Plan (per the §4e discipline — measure, don't reason):
- Reduce on real Victor with the `build/mp-churn-*.py` family + a SMALLER heap to force the
  collection earlier (`MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh main …` then run scale2 /
  churn-lit).  §4e already showed churn-lit corrupts on a 16 KB heap when a live object spans
  a collection — re-confirm it still does post-§4g+§4i (it may now, since the far-ptr fix
  changes nothing about marking completeness).
- Instrument MARKING COMPLETENESS directly (§4e step 1/2): in the port `gc_collect` (external
  tree), capture a known-live object's block before the collection and check its ATB kind is
  not FREE after `gc_collect_end`.  Find WHICH live block is swept.
- Suspect: conservative C-stack scan range, or a multi-block live object's tail words (child
  pointers) not traced, or a far-pointer VALUE inside a live container read at a wrong offset.
- A medium-model DOS probe linking `py/gc.c` with stub `mp_state` (cross-linked objs, drop
  most, force gc_collect, verify a retained object) would give a DOSBox-speed repro — if it
  reproduces in medium it's not far-data-specific; if not, it's in the far load/store paths.

### OPTIONAL §4k side-tracks (lower priority, independent)
- **Huge-mode `_qbe_huge_add` >= 0x8000 gap**: `gc_bigheap_probe` still FAILS under
  `--model=huge` (NOT gated there).  Orthogonal to §4i (huge uses huge_ptr_binop →
  `_qbe_huge_add`, untouched; huge codegen byte-identical before/after).  Probe `rt`
  (far-ptr DIFF) is correct under huge so `*p` writes fine; only `pool[off]` (the
  `_qbe_huge_add` read) is wrong.  Fix in the libstub helper / `huge_ptr_binop` unsigned
  widening.  Real consumer (MicroPython) runs compact, so low priority.
- **build-example.sh -DFAR_DATA gap**: still self-#defined by gc_bigheap_probe.  Clean fix =
  build-example.sh adds `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for compact/large/huge; VERIFY it
  doesn't shift farglobal/fardata/farstruct_ptr goldens (medium stdint_probe asserts sizeof==2).

---

# (DONE — §4i+§4j landed, Victor-verified) Next session (§4j — VICTOR RE-VERIFY the §4i far-pointer fix on real MicroPython, then optionally close the orthogonal huge-mode `_qbe_huge_add` >=0x8000 gap.  §4i LANDED the offset-only far-pointer arithmetic fix in the compiler; DOSBox gate is 224/224 and the reduction probe is ALL OK, but the MicroPython end-to-end payoff on Victor is NOT yet confirmed.)

## 2026-06-08 §4i notes (THE FIX LANDED — offset-only 16-bit segment-preserving far-pointer arithmetic; DOS gate green; Victor re-verify still pending)

**§4i implemented the §4h-scoped fix.**  `far_ptr ± idx` on compact/large (and explicit
`__far` in any non-huge model) now lowers to dedicated **offset-only** backend ops
`addfo`/`subfo`: add/sub ONLY the 16-bit OFFSET word, segment preserved (no `adc`/`sbb`).
That is correct for BOTH a true large offset >= 0x8000 (MicroPython gc_alloc's
`pool_start + start_block*16` on a >32 KB heap) AND a 16-bit-wrapped "negative" `size_t`
delta — which neither `extsw` nor `extuw` of a flat 32-bit add can handle at the same time.

### What changed (4 tracked files; see [[project-far-ptr-unsigned-index-bug]])
- `ops.h`: new public ops `O(addfo,…)`/`O(subfo,…)`, `T(e,l,e,e, e,l,e,e)`, ALL flags 0
  (opaque to fold/gvn/copy so nothing rewrites them back to plain `add`).  Placed right
  after `faroff`, outside every `INRANGE` op range.  **No `tools/lexh.c` / `parse.c` K
  regen needed** — the existing perfect-hash `K=362902335`/`M=23` had slack for two more
  tokens (verified empirically: asserts are ON, and qbe `lexinit()`'s collision assert did
  NOT fire; `addfo` parses).  If you ever add MORE ops and it DOES collide, regenerate via
  lexh.c — but note lexh.c's `tok[]` is already stale vs the real op set (missing
  loadfs/storefs/vargp/callfar), so sync it to all public optab names first.
- `i8086/emit.c`: `case Oaddfo: case Osubfo:` in the Kl switch.  Loads the far ptr to DX:AX
  (DX=segment, AX=offset), `add/sub ax, <arg1 LOW word>` with NO adc/sbb, stores DX:AX
  back (segment word unchanged).  arg1's HIGH word is deliberately ignored — that's exactly
  the part that would wrongly carry into the segment.  5 insns vs the old flat-add's 6.
  Bracketed with `kl_save_axdx`/`kl_stage_arg` like Osub Kl; `die()`s on a CAddr offset.
- `minic.y`: new `far_ptr_offset_binop()` emits `=l addfo/subfo`; called in the prefix
  inc/dec site AND the default-Binop site (covers `a[i]`, `p+i`, `p-i`, postfix `p++/--`).
  Excludes MHuge (huge_ptr_binop runs first), fn-pointers (CS), and near (16-bit) pointers.
  **The Scale path is UNCHANGED** — the backend reads only arg1's low 16 bits, which already
  equal `(idx*sz) mod 0x10000` regardless of the sext, so no front-end extension change.

### Verified (this session, all on macOS/DOSBox — NO Victor run yet)
- `make check` green (generic backends unaffected; new ops are i8086-only in emit).
- `tools/test-dos.sh` **224/224 ok** (was 222; +2 = `gc_bigheap_probe` compact+large).
  Every pre-existing far-data/medium/huge probe still passes → no regression from addfo/subfo.
- `gc_bigheap_probe.c`: was `FAIL` (b>=2048 `direct` wrong/zero), now `ALL OK` under compact
  AND large.  Generated asm confirms the offset-only shape: `mov ax,[off]; mov dx,[seg];
  add ax,[idx]; mov [res],ax; mov [res+2],dx` — no `adc`.  Probe is now GATED (compact+large).

### THE GOAL FOR §4j (the real payoff, NOT yet done)
1. **Victor MicroPython re-verify** (`tools/run-victor-sasi.sh`, compact far-data, stackless).
   The DOSBox probe proves the codegen; this proves the consumer.  Run on real Victor:
   - `build/mp-churn-scale2.py` on the **49 KB** heap must now reach `120 7980` / `DONE`
     (the §4g/§4h stall point was churn(80) ≈ 33 KB ≈ just past offset 0x8000 — exactly
     the bug; it should be GONE).
   - `build/mp-feature-probe.py` must stay **23/23** (the §4h extuw attempt regressed this to
     `ER list`/`XX gen`; addfo must NOT — it preserves the wrapped-negative-delta path).
   - `build/mp-fill-probe.py` → `g G DE` / DONE.
   - HARNESS GOTCHA (cost runs before): do NOT pipe `run-victor-sasi.sh` through `tr`/`tail`/
     `head` — the watchdog subshell inherits the pipe fd and blocks ~WALL_SECS.  Redirect to
     a file then filter.  `-nothrottle` makes a 300 emulated-sec run finish in ~2 wall min.
   - The committed MicroPython image is the §4g baseline; rebuild it from the EXTERNAL tree
     with `tools/build-micropython.sh --model=compact` (no external-tree change needed — the
     §4g struct-alignment fix + §4i offset-only arith are both in the qbe repo now).
2. **OPTIONAL — close the orthogonal huge gap.**  `gc_bigheap_probe` still FAILS under
   `--model=huge` (NOT gated there).  That path is `huge_ptr_binop` → `_qbe_huge_add`
   (segment-normalising libstub helper, needed because huge objects can exceed 64 KB), which
   has its OWN >=0x8000 bug — untouched by §4i (huge codegen is byte-identical before/after).
   The probe's `rt` (far-ptr DIFFERENCE) is correct under huge, so `*p` writes fine; only
   `pool[off]` (the `_qbe_huge_add` read) is wrong.  Reduce to the `_qbe_huge_add` helper in
   `tools/libstub_to_exe.py` / `minic.y::huge_ptr_binop` (the unsigned `extuw` widening +
   the helper's offset normalisation for an offset whose top bit is set).  Lower priority —
   the real consumer (MicroPython) runs compact, not huge.
3. **Incidental harness gap (do alongside, verify goldens):** `tools/build-example.sh` does
   NOT pass `-DFAR_DATA` to cpp (only `build-micropython.sh` does), so compact/large probes
   get 16-bit `uintptr_t`.  `gc_bigheap_probe.c` self-`#define`s `FAR_DATA`/`DOS_FAR_DATA` to
   work around it.  Clean fix = build-example.sh adds `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for
   compact/large/huge — but VERIFY it doesn't shift farglobal/fardata/farstruct_ptr goldens
   (medium stdint_probe asserts sizeof==2, so leave medium alone).

---

# (DONE — §4i landed) Next session (§4i — IMPLEMENT THE FIX: offset-only 16-bit segment-preserving far-pointer arithmetic.  §4h root-caused the scale2 churn(80) stall to a REAL minic bug — `far_ptr + unsigned_index >= 0x8000` sign-extends → wild pointer; the §4f/§4g "perf cliff" framing was a FALSE hypothesis; no GC even runs before the stall.  Reduction probe + evidence committed; naive fix reverted because it regresses wraparound deltas.)

## 2026-06-08 §4h notes (root-caused the churn stall to a far-pointer codegen bug; reduction probe committed; proper fix scoped, NOT yet landed)

**The §4f/§4g "perf cliff" attribution was FALSIFIED.**  Those notes *hypothesised*
(never instrumented) that scale2 stalling at churn(80) on the 49 KB heap was a
`gc_block_stack` overflow → O(blocks) rescan perf cliff.  This session instrumented
it and the hypothesis is wrong: **no garbage collection runs at all before the
stall.**  So raising `MICROPY_ALLOC_GC_STACK_SIZE` (this session's original task)
is moot and was abandoned.  The stall is a genuine compiler bug.

### How it was found (the discipline paid off)
1. Added a depth probe (`gc_block_stack` high-water "H<n>" print) + `g`/`G` markers
   around the port `gc_collect` + an `o` overflow marker — all behind a config
   macro, in the EXTERNAL tree (now reverted).  Ran scale2/loc80 on real Victor:
   **zero `g`/`G`/`o`/`H`** ever printed → no collection, no overflow, no marking
   before the hang.  (Markers proven present in `main.asm`/`gc.asm`; `gc_alloc`'s
   only collect-trigger is gc.c:934/984, confirmed reachable.)  `MICROPY_GC_ALLOC_
   THRESHOLD` is 0 at MINIMUM ROM level, so nothing forces an early collection.
2. `build/mp-fill-probe.py` (forced fill, nothing retained) → `g G DE`: a
   collection *does* run/complete once the heap fills, then the program raises.
   `build/mp-churn-loc80.py` (per-iteration markers) → dies at churn(80) iter ~12
   ≈ **~33 KB of monotonic allocation — just past 32 KB (offset 0x8000)** into the
   single-segment heap.  16 KB heap completes (offsets < 0x4000); 49 KB breaks.
   The "just past 0x8000" signature pointed straight at a 16-bit-offset sign flip.
3. Reduced to `minic/dos/examples/gc_bigheap_probe.c` (compact far-data, DOSBox —
   a SECONDS loop, no Victor).  Its SSA is the smoking gun.

### THE BUG (committed repro: `gc_bigheap_probe.c`)
minic's pointer-scale path (`minic.y`, `prom()` label `Scale:`) lowers
`far_ptr + <variable index>` as a **flat 32-bit add of a SIGN-EXTENDED index**:
it unconditionally `sext(r)`s a sub-long index before `=l mul`/`=l add`.  When the
index is an UNSIGNED byte offset ≥ 0x8000 (top bit of the 16-bit offset set),
`extsw` makes it negative, so `ptr + off` lands at a wild address BELOW the object.
MicroPython's `gc_alloc` returns exactly this shape — gc.c:1020
`ret_ptr = area->gc_pool_start + start_block * BYTES_PER_BLOCK` — so on a >32 KB
heap, any block in the upper half (`start_block >= 2048`, offset ≥ 0x8000) is
handed back at a bogus address → heap corruption.  Same family as
`[[feedback-minic-unsigned-widen-extsw]]` (§2r), but at the pointer-scale site,
not a cast.  Probe SSA: `%t155 =l extsw %t154` for `pool[off]`.

### WHY a naive extuw fix is WRONG (do NOT just flip extsw→extuw)
Tried `if (ISUNSIGNED(r->ctyp)) extuw else extsw` in the Scale path.  It fixed
`gc_bigheap_probe` (ALL OK) and `make check` stayed green — but on Victor it
**REGRESSED the common path**: MicroPython `feature-probe` went 23/23 → `ER list`
/ `XX gen`, and scale2 raised immediately (`<class 'iterator'>`).  Reason: the
flat-32-bit-add model is fundamentally wrong for far pointers.  `extsw` was
"accidentally correct" for code that builds a *negative* `size_t` delta (16-bit
wraparound, e.g. `ptr + (a - b)` with `a < b`): `extsw` of the wrapped 16-bit
value reproduces the intended backward move, whereas `extuw` turns it into a huge
forward jump.  So:
- `extuw` ✔ true-large-offset (gc_alloc), ✘ wrapped-negative delta (list/gen).
- `extsw` ✔ wrapped-negative delta, ✘ true-large-offset ≥ 0x8000 (gc_alloc).
No single extension on a flat 32-bit add handles both.  **REVERTED** (minic.y back
to unconditional `sext`; MicroPython image byte-restored to the §4g baseline
843648 / body 820400; gate untouched, `make check` green).

### THE FIX TO IMPLEMENT NEXT (§4i) — offset-only far-pointer arithmetic
On 8086 compact/large, a far pointer's segment is FIXED per object (objects ≤ 64 KB)
and arithmetic stays within the segment.  The correct lowering of `far_ptr ± idx`
is **add/sub `idx` to the 16-bit OFFSET only, with 16-bit wraparound, segment
preserved** — NOT a flat 32-bit add/sub.  That is correct for BOTH cases:
- gc_alloc: `off(small) + start_block*16` (< 0x10000 within the segment) → right
  offset, segment kept.
- wrapped-negative: `off + 0xFFFF` wraps to `off-1`, segment kept.
Recommended implementation: a dedicated backend op (e.g. `Oaddfo`/`Osubfo`, "far
offset add/sub") that emits `add word <ptr-low>, idx16` with NO `adc`/`sbb` into
the segment word — both CORRECT and SMALLER than today's `add ax,lo / adc dx,hi`.
minic emits it for `far_ptr ± index` under compact/large (the `Scale:` else
branch + the postinc/preinc far paths).  This is meaty: new op → `ops.h` + `all.h`
+ the IL-lexer perfect-hash regen (`tools/lexh.c`, the `K` constant) + `i8086/emit.c`
handlers + minic emission, then `make check` + full `tools/test-dos.sh` + Victor
re-verify (scale2 must reach `120 7980` / `DONE`; feature-probe 23/23; fill probe
→ DONE).  Do NOT rush it at end-of-session — that's exactly why §4h reverted
rather than shipped.  A pure-IR alternative (`(ptr & 0xFFFF0000) | ((ptr_lo+idx)
& 0xFFFF)`) is correct but bloats every variable-index far-arith site (~5 insns);
the image is already near the ~824 KB ceiling, so prefer the compact backend op.
NOTE the **only-variable-index** scope: constant-index far arith goes through the
`r->t == Con` path (`r->u.n *= sz`; a large *constant* index ≥ 0x8000 could carry
into the segment too — handle if a real case appears, but it's rare).

### Verify the fix with the committed repro
`QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact minic/dos/examples/gc_bigheap_probe.c`
then `tools/run-dos-exe.sh build/examples/gc_bigheap_probe/gc_bigheap_probe.exe`.
BUGGY (today): `b>=2048` lines show wrong/zero `direct` + `FAIL`.  FIXED: every
`direct=<0x41+i>` + `ALL OK`.  Then GATE it (compact/large/huge) — but build-example.sh
must pass `-DFAR_DATA` first (see below), or keep the probe's self-`#define FAR_DATA`.

### Incidental harness gap found (fix alongside §4i)
`tools/build-example.sh` does NOT pass `-DFAR_DATA` to its cpp step (only
`build-micropython.sh` does), so a probe built compact/large/huge gets 16-bit
`uintptr_t`/`intptr_t` (stdint.h `#else` branch) instead of the 32-bit a far
pointer needs.  `gc_bigheap_probe.c` self-`#define`s `FAR_DATA` to work around it.
The clean fix: build-example.sh should add `-DFAR_DATA=1 -DDOS_FAR_DATA=1` for
compact/large/huge — but VERIFY it doesn't shift existing far-data probe goldens
(farglobal/fardata/farstruct_ptr reference these macros; stdint_probe is
medium-only and asserts `sizeof==2`, so leave medium alone).

### Build/run cheat-sheet (so next session doesn't re-derive it)
- Victor harness GOTCHA (cost a run this session): do NOT pipe `run-victor-sasi.sh`
  through `tr`/`tail`/`head` — the watchdog subshell inherits the pipe fd and
  blocks ~WALL_SECS.  Redirect to a file, then filter: `... > /tmp/x.out 2>&1` then
  `LC_ALL=C tr -cd '\11\12\15\40-\176' < /tmp/x.out`.  `-nothrottle` makes a 300
  emulated-sec run finish in a couple wall minutes.  (See [[feedback-victor-harness-pipe-buffer]].)
- Scratch repros (untracked `build/*.py`): `mp-fill-probe.py` (forced fill → `gGDE`),
  `mp-churn-loc80.py` (per-iter localization → dies iter ~12), `mp-churn-scale2.py`
  (the canonical 20→120 churn).

---

# Next session (§4g — ROOT-CAUSE FIX in the COMPILER: minic now far-data NATURAL-ALIGNS 4-byte struct members so MicroPython's sizeof(void*)-strided conservative GC works as-designed; §4f scanner workarounds REVERTED; verified on real Victor)

## 2026-06-08 §4g notes (the §4f workaround replaced by the real fix; user-chosen direction)

**§4g fixes the §4d/§4e/§4f churn GC corruption at its ROOT, in minic, and reverts
the §4f scanner workarounds.**  §4f had adapted MicroPython's conservative collector
to minic's packed struct ABI (2-byte scan stride + a dual-aligned mp_state rescan);
§4g instead makes minic emit a pointer-aligned ABI under far-data, so the *unmodified*
upstream collector works.  Committed to master as `d389d63` (compiler change, green-gate
milestone); the workaround revert lives in EXTERNAL `~/projects/micropython` (not the
qbe repo), same as the §4f fixes did.

### The fix (minic.y, `d389d63`)
Lay struct members out with **natural alignment under far-data** (NEAR_DATA stays PACKED
→ tiny/small/medium byte-identical, whole medium gate untouched):
- `structh[].align` = max member alignment; new `alignof_ctyp()` returns **1 under
  NEAR_DATA**, else **4** for a 4-byte member (long / far data ptr / float / 4-byte
  fn-ptr), the aggregate's own align for a struct/union member, and **1** for sub-4-byte
  scalars (char/short/int/near-ptr — they can't hold a pointer, so their alignment is
  irrelevant to the collector; this keeps padding, hence image growth, minimal).
- pre-pad `size` to the member's alignment + bump struct align in
  `structaddmember`/`structaddarrmember`/`structaddbitfield`.
- `structfinish()` tail-pads a struct to its own alignment (idempotent); called at all
  **9** struct-close grammar sites + `emit_struct_global_array`.
- `hoistanonymous()` aligns the anonymous body's base offset + propagates its align.
- The static-initializer machinery already gap-fills (`agg_zfill(m->offset - cursor)`)
  and tail-fills (`if (cursor < structsize)`), so it followed the aligned offsets with
  no change; `offsetof`/`emit_clit_aggr`/member access read `m->offset` directly.

**Minimal blast radius:** a struct with NO 4-byte member has align 1 → byte-identical
even under far-data.  The MicroPython compact image grew only **+224 B** vs the §4f
baseline (843424→843648, body 820160→820400; the alignment padding is +304 and the
reverted-workaround code is −80) — well under the ~824416 "Program too big" point.

### Why this works (the §4f bug, now at its source)
Upstream `gc_collect_start` scans the mp_state roots with
`gc_collect_root(ptrs + root_start/sizeof(void*), …)`, root_start =
`offsetof(mp_state_ctx_t, thread.dict_locals)`.  PACKED that was 6 → void**-arith rounds
to byte 4 → the whole root scan was 2 bytes out of phase (the §4f bug).  ALIGNED,
`dict_locals` (a pointer) sits at a 4-aligned offset, so `root_start/4*4 == root_start`
exactly and every root pointer is at a stride boundary → all found.  Likewise
`gc_mark_subtree`'s `sizeof(void*)` stride now lands on every heap-object child
(qstr_pool_t hashes/lengths/qstrs[] are 4-aligned).  So both §4f workarounds are
redundant.

### §4f workarounds REVERTED (external `~/projects/micropython`)
- `py/gc.c` → **`git checkout`** (full revert to upstream): the `MICROPY_GC_SCAN_PTR_STRIDE`
  macro and the byte-offset `gc_mark_subtree` loop are gone; stride is `sizeof(void*)` again.
- `ports/dos8086/main.c` `gc_collect()` → the mp_state dual-aligned **rescan block removed**
  (upstream gc_collect_start now scans it correctly).
- **KEPT** the pre-existing C-stack dual-aligned scan in `gc_collect()` — backend FRAME
  SLOTS are still only 2-aligned (§4g aligns struct member offsets, not stack slots), so a
  far pointer in a stack slot can still sit at a 2-mod-4 frame offset.  Unrelated to §4f.

### Verified on real Victor (`tools/run-victor-sasi.sh`, compact far-data, stackless, workarounds reverted)
| test | heap | result |
|---|---|---|
| `build/mp-churn-lit.py` (minimal corrupting repro) | 16 KB | `R 124750` ✓ clean `D4`/`C5` — **DECISIVE** |
| `build/mp-feature-probe.py` (23 std-surface checks) | 49 KB | **23/23 OK** ✓ — no regression |
| `build/mp-churn-scale2.py` (churn 20→120) | 16 KB | `20 330`…`120 7980`,`DONE` ✓ |
| `build/mp-churn-scale2.py` (churn 20→120) | 49 KB | stalls at churn(80) — see below |

Gates (qbe repo): `make check` green; `tools/test-dos.sh` **222/222** (3 new
`struct_align_probe` entries compact/large/huge; `scalar_array_probe` reworked from
hardcoded packed offsets to model-independent offsetof relationships — it was the ONLY
gated probe that asserted a packed layout, the entire blast radius).

### The 49 KB-heap extreme-churn STALL is the UNCHANGED §4f perf cliff (NOT a regression)
scale2 on the 49 KB heap stops after `60 2190` (values all correct) at churn(80) — and
does so identically at RUN_SECS 300 and 600.  This is exactly the §4f "perf cliff":
extreme churn on the big (3072-block) heap → `gc_block_stack` overflow
(`MICROPY_ALLOC_GC_STACK_SIZE`=64) → repeated O(blocks) full-heap rescans in
`gc_deal_with_stack_overflow`.  §4f's table only ever verified scale2 completing on the
**16 KB** heap and explicitly noted it "does NOT finish in 700 emulated seconds" on 49 KB
— so §4g matches §4f exactly here.  Correctness is unaffected (scale2 completes the full
20→120 on 16 KB; the values printed on 49 KB are correct, it just doesn't finish).
Reverting the §4f stride-2 amplifier should make the scan *faster*, but the fundamental
cause (heap size + stack-overflow rescans) dominates and is untouched by alignment.

### Known follow-up (unchanged from §4f — a perf cliff, correctness-unaffected)
If heavy-GC programs on the big heap need to finish: raise `MICROPY_ALLOC_GC_STACK_SIZE`
(cheap external port-config change, needs its own Victor check) to cut the O(blocks)
rescans.  Otherwise the integer-feature surface, deep recursion (stackless), and moderate
GC pressure are all verified-good on Victor.  No qbe/minic/i8086 source remains to change
for this; pick whichever frontier a real consumer needs next.

---

# (ARCHIVED) §4f — churn GC corruption FIXED via SCANNER WORKAROUND (now superseded by §4g's compiler fix and reverted)

# Next session (§4f — churn GC corruption FIXED: minic packed-struct 4-byte far pointers at 2-mod-4 offsets defeat MicroPython's sizeof(void*)-strided conservative GC scans; dual-aligned root re-scan + 2-byte gc_mark_subtree stride)

## 2026-06-08 §4f notes (§4d/§4e CLOSED — root cause found and fixed; verified on real Victor)

**The §4e "scan misalignment RULED OUT" was a FALSE NEGATIVE.**  The misalignment
WAS the bug — in TWO scan sites — and §4e's single, partial `+2` patch to
`gc_collect_start` fixed neither completely, which is why it "still corrupted."
Lesson: a botched experiment that *appears* to refute a hypothesis is not
evidence the hypothesis is wrong.

### Root cause (SSA-confirmed)
minic lays out struct members **packed**, with NO natural-alignment padding
(`minic.y` `structaddmember`: `m->offset = size; size += SIZE(ctyp)`).  Under
far-data a far pointer is **4 bytes** but `size_t` is **2 bytes**, so a 4-byte
pointer that follows a `size_t`/`uint16_t` lands at a **2-mod-4 byte offset**
(e.g. `mp_state_thread_t.dict_globals`@10, `qstr_pool_t.qstrs[]`@18).
MicroPython's conservative GC scans stride by `sizeof(void *)` = 4, so each such
pointer is **split across two read words and never recognised** → the block it
roots is freed while live → use-after-free → garbage output + hang.  This is
**far-data-specific**: in the medium model everything is 2-byte and 2-aligned,
so pointers are always at `sizeof(void*)` multiples and the bug cannot occur
(exactly as §4e point 4 predicted — a medium-model probe would NOT reproduce it).

The generated `gc_collect_start` SSA proved it: `root_start =
offsetof(mp_state_ctx_t, thread.dict_locals)` is **6**, but
`gc_collect_root(ptrs + root_start / sizeof(void *), …)` does `void**`-pointer
arithmetic that rounds the start DOWN to byte offset **4** (`6/4*4`), so the
*entire* root scan is 2 bytes out of phase and recognises none of the 2-mod-4
root pointers (`dict_globals`, the embedded `dict_main`, `last_pool`, …).

### The fix — two sites, same cause (both in EXTERNAL `~/projects/micropython`)
1. **`ports/dos8086/main.c` `gc_collect()`** — re-scan the `mp_state_ctx` root
   section (offsets `thread.dict_locals` … `vm.qstr_last_chunk`) **byte-accurate
   and at BOTH even alignments** (base and base+2), mirroring the dual-aligned
   C-stack scan already there.  `gc_collect_root` only marks unmarked heads, so
   this is purely additive on top of `gc_collect_start`'s (broken, ~no-op) scan.
   → cleared the **hang/heap corruption** (churn-lit reached clean `D4`/`C5`).
2. **`py/gc.c` `gc_mark_subtree()`** — the subtree (heap-object child) scan had
   the *same* flaw: it strides `sizeof(void*)` from each block start, missing a
   child pointer at a 2-mod-4 block offset.  After fix 1 the hang was gone but
   print still emitted garbage (`\x01`) because `qstr_pool_t`'s
   `hashes`@10/`lengths`@14/`qstrs[]`@18 (the interned-string pointers behind
   `"R"`/`"DONE"`) were skipped → the string chunk was freed/reused.  Changed the
   child scan to step by **`MICROPY_GC_SCAN_PTR_STRIDE`** (= **2** under
   `FAR_DATA`, else `sizeof(void*)` — byte-identical on aligned targets).
   → cleared the print garbage.

NOT a minic/qbe bug: minic's packed layout is intentional and relied on
throughout the tree.  The bug is MicroPython's conservative scanner assuming a
pointer-aligned ABI; the fix adapts the scanner, exactly like the pre-existing
dual-aligned C-stack scan.

### Verified on real Victor (`tools/run-victor-sasi.sh`, compact far-data, stackless)
| test | heap | result |
|---|---|---|
| `build/mp-churn-lit.py` (minimal corrupting repro) | 16 KB | `R 124750` ✓ clean `D4`/`C5` |
| `build/mp-churn-disc.py` churn(80) (dict+comprehension+str) | 16 KB | `A 3720` ✓ |
| `build/mp-churn-scale2.py` (full bisection, churn 20→120) | 16 KB | `20 330`…`120 7980`,`DONE` ✓ |
| `build/mp-feature-probe.py` (23 checks, std surface) | 49 KB | **23/23 OK** ✓ (matches §4c) |

Force a collection cheaply with `MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh
main …`.  Committed image is back to the proper 49 KB heap: **843424 / body
820160** (vs §4c 843344 / 820096 — +80 B for the two fixes; still under the
~824416 "Program too big" point).  No qbe/minic/i8086 source changed, so
`make check` and `tools/test-dos.sh` are unaffected (definitionally green).

### Known follow-up (NOT corruption — a perf cliff)
`scale2`'s **420-iteration extreme churn on the 49 KB heap** does NOT finish in
700 emulated seconds (stalls mid-run), whereas the identical workload completes
on a 16 KB heap and `feature-probe`'s moderate GC pressure is fast on 49 KB.
The 2-byte stride does ~2× the candidate reads and a 49 KB heap (3072 blocks)
collection is far slower; the likely amplifier is `gc_mark_subtree` finding more
(incl. false-positive) pointers → `gc_block_stack` overflow
(`MICROPY_ALLOC_GC_STACK_SIZE` = 64) → repeated O(blocks) full-heap rescans in
`gc_deal_with_stack_overflow`.  Correctness is unaffected.  If heavy-GC programs
on the big heap need to be fast, candidate mitigations (each needs its own
Victor check): raise `MICROPY_ALLOC_GC_STACK_SIZE`; or have minic 4-byte-align
far-pointer struct members so the `sizeof(void*)`-strided scans suffice (large,
risky ABI change — the packed layout is relied on elsewhere).

---

# (ARCHIVED) §4e — churn GC corruption NARROWED to "a live heap object across a GC collection is freed"; NOT a scan-misalignment; collection completes; needs marking-completeness instrumentation

## 2026-06-08 §4e notes (deep diagnostic pass on the §4d churn GC-pressure corruption — characterized, NOT yet fixed)
**No tracked source changed.** All work was external-MicroPython instrumentation (since
reverted) + scratch `build/mp-churn-*.py` repros + reading generated `build/mp-link/*.ssa`
/ `*.asm`.  `make check` green; the MP image was rebuilt to the **byte-identical** committed
baseline (843344 / body 820096).  `tools/test-dos.sh` unaffected (no qbe/minic/i8086 change).
Full working notes in `/tmp/churn-investigation-notes.md` (not tracked).

### What the bug IS (much sharper than §4d's "GC pressure corruption")
- **A heap object that must stay LIVE across a GC collection is freed**, corrupting the heap
  → garbage output + hang.  The collection itself **runs to completion** (all phases).
- **Trigger = (heap forced to collect) AND (≥1 heap object live across that collection).**
  With NOTHING live across the collection it is harmless.
- **NOT cross-call specific, NOT comprehension/dict/str specific** — reproduces with a single
  flat loop of plain list literals.  The §4d "churn(60) ok / churn(80) fail" boundary was just
  "when does the 48 KB heap first fill enough to force a collection"; standalone churn(80)
  passes ONLY because it never collects (its garbage < 48 KB).

### The Victor experiments that pinned it (all real-Victor, 16 KB heap to force a collection early)
- `build/mp-churn-disc.py` standalone `churn(80)` on the **48 KB** heap → PASSES (never collects).
- Shrink heap to **16 KB** (recompile-mp-tu.sh MP_HEAP_SIZE knob), same `churn(80)` → HANGS.
  => forcing one collection is the trigger; a single churn call suffices.
- Port `gc_collect` instrumented with raw phase markers `[g s 1 2 e]` (no heap alloc):
  output is **`[gs12e]` then hang** → gc_collect COMPLETES (start, both root scans, end),
  returns, VM resumes, then corrupts.  NOT a GC-internal infinite loop.
- `build/mp-churn-dead.py` (`[0,0,0,0,0,0,0,0]` as a bare auto-printed expr — nothing live) →
  **SURVIVES 2+ collections, keeps printing cleanly.**  => corruption requires a LIVE object.
- `build/mp-churn-lit.py` (`b=[...]; x=x+b[7]+i` at module scope — `b` is a live global) →
  **`[gs12e]` then garbage `Y[ZXPRS` + hang.**  The minimal corrupting repro.

### What is RULED OUT (verified in generated SSA + i8086 asm, free of Victor runs)
- **Scan misalignment of the mp_state root section** — hypothesized the single-4-byte-stride
  `gc_collect_start` scan skips root pointers shifted to odd-2-byte offsets by 2-byte `size_t`
  fields inside embedded `mp_obj_dict_t`/`mp_obj_exception_t` (the port's C-stack scan already
  scans BOTH 2-byte alignments for this reason; `gc_collect_start` does not).  **TESTED a
  +2-alignment scan of the mp_state section → churn-lit STILL corrupts (`[gs12e]ZXPRS`).**
  REFUTED and reverted.  With BOTH scans dual-2-byte-aligned, the live object is still lost.
- **All GC bit/pointer codegen is correct:** `PTR_FROM_BLOCK` (block*16 in 16-bit, safe ≤4095
  blocks), `BLOCK_FROM_PTR` (32-bit far-sub, same-segment), `VERIFY_PTR` (signed compares but
  true heap ptrs share the heap segment so never wrongly rejected), `ATB_HEAD_TO_MARK`/
  `MARK_TO_HEAD`/`ANY_TO_FREE` shifts (`mov ax,<k>; shl ax,cl` — §2k holds), the
  `gc_sweep_free_blocks` free_tail state machine, `gc_deal_with_stack_overflow` (terminates),
  `mp_state_mem_area_t` field offsets (table_start@0, byte_len@4, pool_start@6, pool_end@10,
  last_free@14, last_used@16 — self-consistent), and `gc_get_ptr`.  Heap is a single
  segment (`main_BSS:0x1200..0xD200`, 16-aligned, no 64 KB wrap).
- So it is **NOT a missed root from scan alignment, NOT a GC-internal loop, NOT the obvious
  block-math/ATB codegen.**  The live object is reachable on paper (e.g. global `b` via
  `dict_main.map.table` → entry value, all at 4-byte-aligned offsets that a 4-byte-stride
  trace covers) yet is freed.

### THE GOAL FOR NEXT SESSION — instrument MARKING COMPLETENESS directly
The contradiction ("reachable on paper but freed") means a marking/trace step is silently
incomplete for a live object, in a way not explained by scan alignment.  Stop reasoning;
**measure which live block is swept.**  Concrete plan:
1. In the port `gc_collect` (or a patched `py/gc.c`), capture the address of the known-live
   object before the collection (e.g. expose `MP_STATE_VM(dict_main).map.table` and a known
   global's value pointer) and, after `gc_collect_end`, check whether that block's ATB kind is
   FREE (i.e. it was swept while live).  Print a raw marker if so.  This directly confirms
   freed-while-live and identifies WHICH object (the table array? the list? an intermediate?).
2. Alternatively instrument `gc_sweep_free_blocks` to raw-print each freed block's address, and
   `gc_mark`/`gc_mark_subtree` to print marked-block addresses + total marked count, for the
   ONE collection in `build/mp-churn-lit.py` on the 16 KB heap.  Compare marked-set to the
   expected live set; a live block absent from the marked set is the smoking gun.
3. Suspect list (given codegen is clean): (a) `gc_mark_subtree` n_blocks undercount for a
   MULTI-block live object so its tail words (holding child pointers) aren't traced — re-derive
   on-target, not just from the SSA; (b) a far-pointer VALUE inside a live container read by the
   trace as a non-block-aligned/garbage value (check the actual `loadfl` offsets vs entry
   layout on-target); (c) the conservative C-stack scan range `[&stack_dummy, stack_top)`
   genuinely not covering the slot holding the live root at the collection point (verify
   `nbytes` and that the live `code_state`/value-stack slot is within range on-target —
   churn-dead's in-progress list survives, so module-frame value-stack rooting works for the
   transient case, but a RETAINED global may sit only in `dict_main`).
4. FAST-LOOP idea: try to reproduce in a **medium-model DOS probe** that links `py/gc.c` with
   minimal `mp_state` stubs, allocates cross-linked objects, drops most, forces `gc_collect`,
   reallocates, and verifies a retained object's contents.  If it reproduces in medium model
   (DOSBox, seconds/iteration), the 12-min Victor loop is no longer the bottleneck.  If it
   does NOT reproduce in medium, the bug is **far-data-specific** (compact-model far heap),
   which itself narrows it to the far load/store paths in the collection.
- Repro scripts live in `build/mp-churn-{disc,lit,dead,loc,bisect}.py` (untracked).  Use
  `MP_HEAP_SIZE=16384 tools/recompile-mp-tu.sh main ~/projects/micropython/ports/dos8086/main.c`
  to force a collection in a single small loop (fast relink), then
  `VICTOR_SRC=build/mp-churn-lit.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`.
  Restore the baseline image afterwards with `tools/build-micropython.sh --model=compact`
  (843344 bytes).  HARNESS GOTCHA still applies: redirect to a file + poll, never pipe through
  `tail`/`head` (see [[feedback-victor-harness-pipe-buffer]]).

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


---

# Next session (§3r DONE — Stevie operator/range bug traced to OMF DGROUP fixups; regression probes, scripted checks, and manual MAME are green)

Stevie was used as the integration test for an apparent operator/range corruption bug: `w` moved correctly, but `dw`/`de`/`yw` had operated from the cursor through EOF.  The original suspicion was bad `LPTR` struct assignment, so focused probes were added for aggregate assignment, pointed-to aggregate assignment, temp swaps, static-return-pointer copies, LPTR range iteration, and operator-pending state:

- `minic/dos/examples/struct_copy_probe.c`
- `minic/dos/examples/static_lptr_return_probe.c`
- `minic/dos/examples/lptr_range_probe.c`
- `minic/dos/examples/operator_pending_probe.c`

Those probes showed ordinary LPTR struct copies were not the bug.  The actual root cause was in `tools/omf_link.py`: NASM target-frame 16-bit offset fixups into grouped near data were patched relative to the physical `_BSS` segment instead of DGROUP.  Generated i8086 code uses DS for normal near `_DATA`/`_BSS` references, and DS is DGROUP, so Stevie's multi-TU `startop` reads could alias initialized data.  `_frame_para()` now uses the containing output-group paragraph for target-frame `loc=1`/`loc=5` offset fixups whose target output segment is a DGROUP member, leaving segment/far-pointer fixups unchanged.

Regression coverage added: `grouped_bss_probe.c` + `grouped_bss_def.c`, linked as two translation units, with BSS padding arranged so the old BSS-relative offset lands inside `data_guard`; the golden requires the BSS writes to work and initialized data to remain unmodified.

The provisional Stevie `LPCOPY()`/`LPCOPYP()` workaround layer was reduced back to normal aggregate assignment in `stevie-orig/stevie.h`, so existing Stevie call sites exercise compiler struct-copy codegen again:

- `LPCOPY(d,s)` -> `(d) = (s)`
- `LPCOPYP(d,s)` -> `*(d) = *(s)`

Validation on 2026-06-06:

- `make check` passes.
- `tools/test-dos.sh` passes: `208/208 ok`.
- `tools/build-stevie.sh --exe` succeeds; `build/stevie-orig/stevie.exe` is 146272 bytes.
- Redirected Victor/MAME production-Stevie checks via `build/repl-victor.sh build/stevie-orig/stevie.exe` pass: `1G0dw` leaves `beta gamma`; `1Gwdw` leaves `alpha gamma`.
- User manual MAME testing confirmed `dw`/`de` are working too.

Stevie state to preserve: `VICTOR9000` terminal/key handling in `dos.c`/`term.h`/`env.h`, INT 21h console I/O, Z-19/Victor escape sequences, CR stripping on read, CRLF writeback, row-only redraw/performance fix, DOS display restoration on exit, and the direct `x` behavior simplification unless a future compiler fix makes revisiting it useful.

Next direction after §3r: return to MicroPython as the primary integration exerciser for finding QBE/MiniC/i8086/runtime improvements.  Stevie remains a regression target rather than the main driver.

# Next session (§2u EMPTY-QSTR TRACEBACK PINNED + FIXED — minic varargs implemented; traceback header now prints `File "<stdin>", in <module>` on the real Victor 2026-06-02. PINNED the §2t cosmetic sub-issue and it was NOT what §2t/§2h guessed (a far-data struct-member-chain read of `constants.source_file`). On-target probe in py/vm.c proved the qstr NUMBERS arrive CORRECT — block_name=0x0007 (`<module>`) and source_file=0x00b8 (`<stdin>`) both MATCH their expected static-pool values. ROOT CAUSE = minic's `<stdarg.h>` was a NO-OP STUB (`va_arg(ap,T)=*(T*)0`), so `mp_printf("%q", qst)` read garbage from address 0 (`File "JOo`); `ValueError` printed fine only because the type name goes through `mp_print_str(qstr_str(...))`, not `%q`. **minic-compiled varargs had NEVER worked** — earlier milestones (printing ints) used non-vararg formatting paths. FIX (all qbe-repo, committed): a real `minic/include/stdarg.h` where `va_list`=`char*`, `va_start(ap,last)`=`((ap)=(va_list)__builtin_va_argptr())`, and `va_arg(ap,T)` is pure pointer arithmetic + a (far) load (reuses well-tested far-ptr-add + far-load codegen — no per-type backend op). `__builtin_va_argptr()` is recognised by NAME in minic.y's `call()` and emits a new i8086 value-producing op `vargp` (ops.h, public block) → backend materialises a pointer to the first variadic arg as SS:(bp+vararg_off) [far-data, DX:AX] / (bp+vararg_off) [near-data, DS==SS, AX]. `vararg_off` = `2*-s` past the named params, recorded by selpar into new `Fn.vararg_off` (all.h). emit.c handler modelled on Omkfar, bracketed with `kl_save_axdx` (the first cut clobbered DX where rega had placed a named `count` param — classic clobber-without-telling-rega bug); the Kl-special switch gate also gets `|| i->op==Ovargp` so the near-data Kw result reaches the handler (else op 100 → omap[] abort). NO grammar change (111 s/r 0 r/r); the typed `va_arg` is a pure-C macro. Probe `vararg_probe.c` (+golden) gated medium+compact+large: all-int loop, the exact `mp_printf`/`%q` shape (ptr fixed param then a 2-byte vararg), and mixed int/long/ptr/int widths — bug-loud on the unfixed compiler. `make check` green, DOS gate **181→184/184**. NOTE found-not-fixed: `extern <type> *f(void);` (pointer return + explicit `(void)` proto params) trips a minic grammar gap — sidestepped (the builtin needs no prototype since call() recognises it by name). MP build: removed `build/mp-spike/stubinc/stdarg.h` so `<stdarg.h>` resolves to the new `minic/include/stdarg.h`; full compact rebuild 106/106, body **823840 B** (+464 over §2t — the va_arg expansion in the 5 va-using TUs: mpprint/objexcept/runtime/warning/vstr), still loads on Victor. ON-TARGET: `C1 C2 C3 C4 D0 D1 D2 D3 DE` / `Traceback (most recent call last):` / `  File "<stdin>", in <module>` / `ValueError: boom` / `C5` — header is now CORRECT. MP-tree state: heap 20480 (restored), py/*.c git-clean EXCEPT py/parse.c §2n align fix KEPT (uncommitted), ports/dos8086/ untracked (main.c = raise-ValueError test). NEXT = push frontiers: float (8087), kwargs, comprehensions, deeper recursion/GC-under-pressure; size headroom is ~tight again (823840 B vs the ~824.x ceiling — 824448 did NOT load). See [[project-minic-vararg-stub]].)

<!-- prior header (§2t) retained below for context -->
# Next session (§2t UNCAUGHT-EXCEPTION TRACEBACK PRINTING WORKS ON THE REAL VICTOR 2026-06-02 — `raise ValueError("boom")` (no try/except) now propagates to do_str's nlr handler and prints a REAL traceback ending **`ValueError: boom`**, then exits clean (`C5`). Full Victor (MAME/SASI) trace: `C1 C2 C3 C4 D0 D1 D2 D3 DE` then `Traceback (most recent call last):` / `  File "", in ` / `ValueError: boom` / `C5`. **§2h "Finding 3" (the exception OBJECT's far pointer loses its SEGMENT on the raise path → mp_obj_print_exception crashes) is STALE/CLOSED** — it was diagnosed back in §2h BEFORE the §2i (wide→narrow), §2o (NULL/0→far-ptr widen) and §2r (extuw unsigned widening) arg-coercion fixes; those already restored full far-pointer propagation through nlr, so the exception object now arrives at the handler intact. NO qbe-repo code change this session (no minic/backend bug to fix); the work was VERIFICATION + a MicroPython-port improvement. CHANGE (separate untracked repo `~/projects/micropython/ports/dos8086/main.c`): do_str's `else` (uncaught) branch now calls the REAL `mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val))` (it previously just printed `DE` and a comment said the printer "crashes under far-data" — no longer true); test program switched to `raise ValueError("boom")`. Rebuilt compact far-data via `tools/recompile-mp-tu.sh main` → body **823376 B**, under the ~824KB ceiling, links clean (mp_obj_print_exception + traceback machinery pulled back in by --gc-sections). KNOWN SUB-ISSUE (cosmetic, NON-blocking, NEW follow-up): the traceback header prints `  File "", in ` — both the file qstr AND block qstr resolve to EMPTY while the static-pool `ValueError` qstr prints correctly. This port has `MICROPY_EMIT_BYTECODE_USES_QSTR_TABLE` OFF, so in py/vm.c:1456-1461 `source_file = code_state->fun_bc->context->constants.source_file` (a qstr reached through a chain of FAR pointers) and `block_name = mp_decode_uint_value(ip)` (decoded from the prelude); both reading empty points at a far-data struct-member-chain read (or a qstr-not-stored issue) in the VM traceback-add path — NOT the printer (mp_obj_print_helper PRINT_EXC works, §2m). To pin: on-target, print the raw `source_file`/`block_name` qstr values (16-bit) just before mp_obj_exception_add_traceback; if they arrive 0/garbage it's a far-data read of `constants.source_file` (expect `<stdin>`/`<module>`). Each Victor run ~1min; deferred as a clean follow-up since the milestone (traceback prints, no crash) is met. NEXT = either pin the empty-qstr traceback header, OR push the remaining frontiers: float (8087), kwargs, comprehensions, deeper recursion/GC-under-pressure. MP-tree state: heap 20480, py/*.c git-clean except py/parse.c §2n align fix KEPT (uncommitted), ports/dos8086/ untracked (main.c now = the raise-ValueError test with the real printer). See [[project-repl-breadth-verified]].)

<!-- prior header (§2s) retained below for context -->
# Next session (§2s TYPEDEF'D FN-PTR ARG COERCION — closed the §2q' gap. A fn-ptr declared via a TYPEDEF (`typedef int (*F)(void*,unsigned,unsigned long,int); F fp;` — and the struct-member form `struct{F cb;}`) now coerces indirect-call arguments to the recorded parameter types, exactly as §2q' did for the plain `T(*fp)(...)` declarator and inline struct fn-ptr members. ROOT of the gap: minic resolves a typedef name to its bare type at the variable/member declaration, so the §2q' proto registry (keyed off the declarator) never saw it; a narrow arg handed to a wide param therefore stayed un-widened (the §2q LOAD_FAST mechanism — a 2-byte push where the callee reads 4 mis-reads the next arg). FIX (`minic/minic.y`, frontend-only, NO backend/grammar-structure change): the typedef table `typh[]` gains an `fpid` field (proto index into the §2q `fpproto[]`); the two fn-ptr-typedef rules (file-scope ~6234 + local ~7476) record it via `fpproto_alloc` + new helper `typhset_fpid`; `typhget` surfaces it into a new global `g_td_fpid` (reset on type keywords, `type '*'` rules, and the declarator-name fallthrough — same leak-prevention discipline as `g_td_arraydim`); the var-decl rules (`F fp;` / `F fp=expr;`) and the struct-member rule (`F cb;`) call `varsetfpid`/`structset_last_fpid` when `g_td_fpid>=0`. The indirect-call coercion at the call sites is the SAME §2q' machinery (`varfpid`/member `fpid`→`coerce_arg`); §2s only fixes the proto TRANSFER through the typedef. VERIFIED: `make` 111 s/r 0 r/r (no new conflicts), `make check` green, DOS gate **178→181/181** (new `typedef_fnptr_probe.c` medium+compact+large; SSA shows all 4 indirect calls emit `extuw` to widen the uint16_t arg to the `unsigned long` param in both compact AND near-data medium; ran compact in DOSBox → `tv0/tv9/tm0/tm7 ok`). COMMITTED to master. This was a pure minic frontend cleanup (no Victor needed). NEXT = the on-target frontiers still open: float (8087), kwargs, comprehensions, deeper recursion/GC-under-pressure, exception TRACEBACK printing (do_str's uncaught path still avoids mp_obj_print_exception under far-data — the §2h objexcept far-ptr-print gap). MP-tree state unchanged from §2r: heap 20480, py/*.c git-clean except py/parse.c §2n align fix KEPT (uncommitted), ports/dos8086/ untracked. See [[project-repl-loadfast-argmix]] + [[project-repl-breadth-verified]].)

<!-- prior header (§2r) retained below for context -->
# Next session (§2r EXCEPTIONS NOW WORK ON THE REAL VICTOR 2026-06-02 — `try: raise ValueError; except ValueError: print("caught"); print("done")` prints **caught**/**done** with a clean `D4 C5` exit. ROOT CAUSE was a GENERAL minic codegen bug: the integer-cast handler (minic.y case 'K', `dst=='l' && src=='w'` widening branch) ALWAYS emitted `extsw` (sign-extend), even for an UNSIGNED source. `(uint32_t)(size_t 0xFFFF)` → `0xFFFFFFFF` instead of `0x0000FFFF`, corrupting MicroPython's `MP_OBJ_FUN_MAKE_SIG(min,max,kw) = ((uint32_t)min<<17)|((uint32_t)max<<1)|kw` (max=0xFFFF=MP_OBJ_FUN_ARGS_MAX): the sign bits bled into the min field, so `mp_arg_check_num_sig` decoded `n_args_min = sig>>17 = 0x7FFF`; `mp_arg_check_num` then saw "0 args given, 32767 required", raised TypeError, whose construction (`mp_obj_exception_make_new`→`mp_arg_check_num`) re-ran the same way → INFINITE RECURSION → stack blowout. This hung EVERY `raise`/try-except (both caught and uncaught stopped at D3). FIX (minic.y): `fprintf(of, " =l %s ", ISUNSIGNED(s0.ctyp) ? "extuw" : "extsw")` — extension chosen by the SOURCE operand's signedness (the existing pattern at minic.y ~2622). `make check` green; DOS gate **178/178** (3 new `uwiden_cast_probe` medium+compact+large; bug-loud unfixed). Localized by on-target marker bisection (R0/R1/X0-looping/no-X1) + static SSA read (`%t19 =l extsw %t21` on unsigned 0xFFFF). COMMITTED to master. MP-tree state: heap 20480, py/{vm,objfun,bc,emitbc,runtime}.c git-clean, py/parse.c §2n align fix KEPT (uncommitted), main.c do_str = try/except test. NEXT = push more frontiers: float (8087), kwargs, comprehensions, deeper recursion/GC-under-pressure, exception TRACEBACK printing (do_str's uncaught path still avoids mp_obj_print_exception under far-data — the §2h objexcept far-ptr-print gap); OR close the typedef'd-fn-ptr coercion gap. See [[feedback-minic-unsigned-widen-extsw]] + [[project-repl-breadth-verified]].)

<!-- prior header (§2q'') retained below for context -->
# Next session (§2q'' REPL BROADLY VERIFIED ON REAL VICTOR 2026-06-02 — after the §2q' indirect-call arg-coercion fix (commit b6edbc9, on master), a sweep of MicroPython REPL features on the real Victor 9000 found ZERO new bugs. All printed correct results via `tools/run-victor-sasi.sh build/mp-link/mpython.exe 60` (compact far-data, heap 20480), each a clean `C1 C2 C3 C4 D0 D1 D2 D3 <out> D4 C5` trace: (1) user fn + positional args `add(3,4)`→**7**; (2) recursion + if/compare + multiply + nested call `fact(5)`→**120**; (3) strings literal/concat/len/print `"ab"+"cd"`→**abcd**, len→**4**; (4) lists construct/`.append` bound-method/subscript/len `[1,2,3].append(4)`→**4**,**4**. The §2q' fix unlocked the whole REPL. Fast loop = edit `ports/dos8086/main.c` do_str only → `tools/recompile-mp-tu.sh main ~/projects/micropython/ports/dos8086/main.c` → Victor run. Image body ~823KB, just under the ~824KB ceiling — SIZE is the practical wall for features that pull new runtime code. MP-tree state: heap 20480, py/{vm,objfun,bc,emitbc}.c git-clean, py/parse.c §2n align fix KEPT (uncommitted); main.c do_str currently = the list test. No new qbe-repo commits this/last session (the fix was already committed). NEXT = push frontiers likely to find NEW bugs: dicts, tuples, exceptions (handler+traceback — beware the objexcept far-ptr-segment-dropped gap from §2o), float (8087), kwargs, comprehensions, GC-under-pressure; OR close the typedef'd-fn-ptr coercion gap if a consumer hits it. See [[project-repl-breadth-verified]] + [[project-repl-loadfast-argmix]].)

<!-- prior header (§2q' DONE) retained below for context -->
# Next session (§2q' DONE — the §2q `def add(a,b)` HANG is FIXED. NOT a backend arg-passing bug: it was a minic FRONTEND gap — an INDIRECT (function-pointer) call did not coerce arguments to the callee's declared parameter types. MicroPython's body-variable LOAD_FAST goes through the bytecode emitter METHOD TABLE (emitcommon.c:131 `emit_method_table->local(emit, qst, id->local_num, kind)`), an indirect call; minic's coerce_arg only fired on DIRECT (name-keyed fnproto) calls, so `id->local_num` (uint16_t, Kw) stayed `w` where the param is `mp_uint_t` (Kl) — the 2-byte push shifted `kind`, which was then read from the wrong slot (arrived 0xb0). §2q's "static asm looked correct" puzzle was because it inspected the DIRECT call sites (compile.c); the real runtime path is the method-table indirect call. FIX in `minic/minic.y`: record each fn-ptr declarator's parameter types (in a new `fpproto[]` registry, with the index stashed in the declarator's varh/Member entry — type-bit stashing was impossible since the type integer is full and the typedef/member tables are 32-bit) and coerce args at BOTH indirect-call paths (member `obj->fn(...)` via a g_callee_fpid stash set in expr() case '.', and `fp(...)`/`(*fp)(...)` via the variable's recorded fpid). Probe `argmix_probe.c` (medium+compact+large, runtime-filled method table so it needs no --far-static-data) — bug-loud (`m7 FAIL 4300`/`fp9 FAIL 6500`, the trailing int dropped) on the unfixed compiler. `make check` green, 111 s/r 0 r/r (no grammar-structure change, only fptpar actions), DOS gate 174/174 (one flaky-DOSBox for_comma_inc_probe re-confirmed passing standalone). KNOWN GAP (noted, not §2q-blocking): a fn-ptr declared via a TYPEDEF (`typedef int (*F)(...); F fp;`) is still NOT coerced — the typedef name is lost at the variable declaration. NEXT = rebuild MicroPython compact far-data + run `def add(a,b): return a+b; print(add(3,4))` on the real Victor (run-victor-sasi.sh, FOREGROUND) — expect the LOAD_FAST `d4 NN`→`b0/b1` fix to let add() execute; restore heap 20480 + revert the MP-tree debug markers (CLEANUP list in §2q below). See [[project-repl-loadfast-argmix]].)

> **§2q' (DONE 2026-06-02) — the §2q user-function-with-args HANG is FIXED in
> the minic frontend.  It was NOT an i8086 backend arg-passing bug (the §2q
> hypothesis) — it was a missing argument coercion on INDIRECT (function-pointer)
> calls.  `minic/minic.y` only; `make check` green (111 s/r, 0 r/r — only the
> fptpar semantic actions changed, no grammar-structure change); amd64/arm64/rv64
> unaffected; DOS gate 174/174.**
>
> ROOT CAUSE (the §2q "static-vs-runtime contradiction", resolved): §2q traced
> the DIRECT call sites in py/compile.c (`EMIT_LOAD_FAST` macro), where the asm
> looked correct — but MicroPython's bytecode-only port (`MICROPY_EMIT_NATIVE`=0)
> routes a function body's variable read through `compile_load_id` →
> `mp_emit_common_id_op` → **emitcommon.c:131** `emit_method_table->local(emit,
> qst, id->local_num, MP_EMIT_IDOP_LOCAL_FAST)` — an INDIRECT call through the
> static method table `mp_emit_bc_method_table_load_id_ops`.  The member type is
> `void (*)(emit_t*, qstr, mp_uint_t local_num, int kind)` = a (l, w, l, w) call;
> `id->local_num` is a `uint16_t` (Kw), but the param is `mp_uint_t` (Kl, 4 bytes
> under far-data).  minic's §2i/§2o `coerce_arg` fires only on DIRECT calls
> (`fnproto`, keyed by the callee NAME); the indirect-call paths (`call()` case
> 'C' fn-ptr branch and `expr()` case 'I') evaluated args with `eval_arg` and
> NEVER coerced.  So `local_num` was pushed as 2 bytes where the callee reads 4,
> shifting `kind` — read from the wrong [bp+off] as 0xb0 — and the LOAD_FAST
> emitter took the 2-byte `MP_BC_LOAD_FAST_N + kind` branch, emitting `d4 NN`
> bytecode the VM landed outside add's body → the hang.
>
> WHY NOT TYPE-BIT STASHING: the fn-ptr type integer encodes only the RETURN
> type (no param list), and is full (recursive `<<3` shifts); the typedef/member
> tables store types as 32-bit `unsigned`, so high proto bits would truncate and
> shifts would corrupt them.  Instead: a name-free, collision-free registry.
>
> FIX (`minic/minic.y`): a new `fpproto[]` table holds each fn-ptr declarator's
> fixed parameter types; the index (`fpid`) is stamped into the declarator's
> `varh`/`Member` entry (new fields, init -1).  The `fptpar0/fptpar1` grammar
> (previously `{ $$ = 0; }`, discarding the params) now builds a `mkptype` type
> chain — same tokens/reductions, so ZERO new conflicts.  Recorded at the struct
> fn-ptr member rule and the local `T (*fp)(...)` decl rules.  Recovered at the
> call: case '.' stashes the member's fpid in `g_callee_fpid` (read by case 'I'
> right after `expr(n->l)`), and case 'C'/'I' fall back to the variable's fpid
> via `varfpid`.  Both indirect-call arg loops now run `coerce_arg` per fixed
> param — the indirect-call analogue of the direct-call fnproto coercion.
>
> PROBE `minic/dos/examples/argmix_probe.c` (+golden, gated medium+compact+large):
> the exact (l, w, l, w) shape with a narrow uint16_t 3rd arg (widened w→l) and a
> trailing `int k`, via a method-table member call (MP shape) AND a directly-
> declared `int (*fp)(...)` variable.  Bug-loud on the unfixed compiler
> (`m7 FAIL 4300`, `fp9 FAIL 6500` — the trailing int read from the wrong slot).
> The method table is filled at RUNTIME (not `static const`) so the probe needs
> no `--far-static-data`: a code symbol in a STATIC data initializer is only
> relocated to a far seg:off under that opt-in (which the MicroPython port
> enables — that is why MP's static method table works on-target); a plain
> `dd _fn` left segment 0 and hung.  Orthogonal to the coercion fix.
>
> KNOWN GAP (noted, NOT §2q-blocking): a fn-ptr declared through a TYPEDEF
> (`typedef int (*F)(...); F fp;`) is still not coerced — minic resolves the
> typedef to its type at the variable declaration, losing the typedef name, so
> the recorded prototype doesn't transfer to the variable.  MicroPython's method
> tables and plain `T (*fp)(...)` declarators are covered; the typedef-variable
> form is left as a documented gap.
>
> NEXT: rebuild MicroPython (`bash tools/build-micropython.sh --model=compact
> --keep-going`) and run `def add(a, b):\n    return a + b\nprint(add(3, 4))` on
> the real Victor (`tools/run-victor-sasi.sh build/mp-link/mpython.exe 60`,
> FOREGROUND).  Expect the LOAD_FAST bytecode to now be `b0 b1 f2 63` (not
> `d4 00 d4 01 ...`) and add() to execute.  BEFORE the run, do the §2q CLEANUP
> (below): RESTORE `ports/dos8086/mpconfigport.h` heap 16384→**20480**, and
> revert the MP-tree debug markers in vm.c/objfun.c/bc.c/emitbc.c (KEEP the
> py/parse.c §2n alignment fix).  See [[project-repl-loadfast-argmix]].
>
> ---
>
# (prior) Next session (§2q IN PROGRESS — BROADENING THE REPL. Confirmed on the real Victor: arithmetic, variables, multi-statement, while-loop, for/range ALL WORK. NEW BUG (unfixed, root-cause LOCALIZED): a user function with args — `def add(a,b): return a+b; print(add(3,4))` — HANGS during execution because add's body bytecode is MIS-EMITTED: each `LOAD_FAST` (should be 1-byte `0xb0+n`) is emitted as 2-byte `0xd4 NN`. Cause: in `mp_emit_bc_load_local` (py/emitbc.c) the 4th param `kind` (an `int`, passed `w 0`) ARRIVES AS 0xb0 instead of 0 — confirmed on-target (`LFb0 00`/`LFb0 01`). The SSA is CORRECT on BOTH sides (caller `call $f(l,w,l,w 0,...)`, callee `(l %t0,w %t1,l %t2,w %t3)`), so it's an i8086 BACKEND arg-passing or param-read bug for the `(l,w,l,w)` mix where the last `w` arg is misread. STATIC analysis of the caller asm looked correct (kind=0 written, frame OK) — the static-vs-runtime contradiction is UNRESOLVED. NEXT = reproduce as a pure minic/i8086 ABI probe (no MicroPython, DOSBox gate) + dump raw callee param bytes to disambiguate arg-passing vs param-read. See §2q below.)

> **§2q (IN PROGRESS 2026-06-02) — broadening the REPL past `print(1+2)`.
> Multiple language features VERIFIED on the real Victor; ONE new bug found,
> root-cause localized but NOT yet fixed.  No qbe/minic/MP code committed this
> session (all changes are debug instrumentation in the MP tree — see CLEANUP).**
>
> WHAT WORKS NOW (all verified end-to-end on the real Victor 9000 via
> run-victor-sasi.sh, each printing correct results):
>  - `print(1+2)` → 3 (baseline, re-confirmed)
>  - arithmetic: `x=10;y=5;print(x*y);print(x-y);print(x+y)` → 50, 5, 15
>    (exercises 32-bit Kl multiply with >16-bit result, assignment, name lookup,
>    multi-statement MP_PARSE_FILE_INPUT)
>  - while loop + comparison: `i=0;while i<3:print(i);i=i+1` → 0,1,2
>  - for/range + iterator protocol: `for i in range(3):print(i)` → 0,1,2
>    (range/iterator code was ALREADY linked via the builtins table — no image
>    growth)
>  Changing the do_str() test STRING does not change linked code footprint (the
>  whole parser/compiler/VM is pulled in regardless); only features needing new
>  runtime fns grow it.
>
> **THE NEW BUG — user function with positional args hangs.**
> `def add(a, b):\n    return a + b\nprint(add(3, 4))` reaches D3 (compile done)
> then HANGS in execution with NO output (no D4, no exception).  The 0-arg module
> function runs fine; the new thing is calling a user bytecode function WITH args
> and a body that uses LOAD_FAST (function locals).  Every earlier test used only
> module-level globals (LOAD_NAME/STORE_NAME), never LOAD_FAST.
>
> ROOT CAUSE (localized by on-target bisection — the decisive method again):
>  1. Instrumented vm.c MAKE_FUNCTION(M1)/CALL_FUNCTION(C0/C1): trace reached
>     `M1 C0` then hung — i.e. the call INTO add never returns.
>  2. Instrumented objfun.c fun_bc_call (F0/Fe/Ff) + bc.c mp_setup_code_state
>     (S0/S1/S2/S9): `F0 S0 S1 S2 S9 Fe` then hang — setup + ALL arg-binding
>     complete; hang is inside `mp_execute_bytecode` of add's body.
>  3. Per-opcode tracer at the vm.c dispatch switch: add's body FIRST opcode
>     read as **0x14** (one heap size) / **0xd4** (another) — should be **0xb0**
>     (LOAD_FAST 0).  The landing opcode CHANGED with heap layout → ip pointed
>     outside add's real bytecode.
>  4. Dumped add's stored bytecode in bc.c (skhex of self->bytecode[0..15] + the
>     decoded n_state/n_pos_args/n_info/n_cell + landing offset).  add's bytecode:
>     `1a 0c | 81 56 81 57 81 58 | d4 00 d4 01 f2 63 ...`
>       - SIG 0x1a → n_state=4, n_pos_args=2 (CORRECT for add(a,b))
>       - SIZE 0x0c → n_info=6, n_cell=0 (n_info=6 is CORRECT: the on-target build
>         encodes the 3 code_info qstrs — simple_name, arg a, arg b — as 2-byte
>         var-uints each = 6 bytes; the .mpy reference uses a qstr-table window so
>         its n_info is smaller.  Do NOT chase the n_info difference; it is a
>         config artifact, not the bug.)
>       - body at offset 8: `d4 00 d4 01 f2 63` — WRONG.  Reference (mpy-cross
>         disasm) body is `b0 b1 f2 63` = LOAD_FAST 0; LOAD_FAST 1; BINARY_OP
>         __add__; RETURN_VALUE.  The `f2 63` tail is right; the two LOAD_FASTs
>         are each emitted as a 2-byte `d4 NN` instead of the 1-byte `0xb0+n`.
>  5. emitbc.c `mp_emit_bc_load_local` (line ~514):
>        if (kind == MP_EMIT_IDOP_LOCAL_FAST && local_num <= 15)
>            emit 1-byte (MP_BC_LOAD_FAST_MULTI + local_num);   // 0xb0+n
>        else
>            emit 2-byte (MP_BC_LOAD_FAST_N + kind, local_num); // 0x24+kind, then n
>     On-target opcode 0xd4 = MP_BC_LOAD_FAST_N(0x24) + 0xb0, i.e. the else branch
>     ran with **kind == 0xb0** (not 0).  Added an LF marker printing kind+local_num
>     at the top of the function: on-target prints **`LFb0 00`** and **`LFb0 01`**
>     (×3, one per compile pass) — so `kind` ARRIVES as 0xb0, local_num arrives
>     CORRECTLY as 0/1.  (NB 0xb0 == MP_BC_LOAD_FAST_MULTI — possibly a
>     coincidence, possibly a clue.)
>
> THE PUZZLE (where it stands — UNRESOLVED): the SSA is CORRECT on both sides.
>   - call (compile.ssa): `call $mp_emit_bc_load_local(l %t131, w %t136, l %t142, w 0, ...)`
>     — coerce_arg correctly WIDENED local_num (a uint16_t field, Kw) to `l` to
>     match the `mp_uint_t` param.  kind passed `w 0`.  (`...` is on EVERY minic
>     call — normal, not variadic-anomalous.)
>   - callee header (emitbc.ssa): `export function $mp_emit_bc_load_local(l %t0, w %t1, l %t2, w %t3)`.
>   Types: `qstr`=`size_t`=2B(Kw); `mp_uint_t`=`uintptr_t`=4B(Kl); `int`=2B(Kw);
>   MP_EMIT_IDOP_LOCAL_FAST=0; id->local_num is uint16_t (2B) → coerced to l.
>   So the IR is a clean (l,w,l,w) call matching a (l,w,l,w) callee, last arg `w 0`.
>   The i8086 BACKEND mis-passes/mis-reads the 4th arg (kind) as 0xb0.
>
>   STATIC asm trace did NOT reveal the fault (this is the unresolved part):
>   - Caller site asm 10627 (in _close_over_variables_etc, frame `sub sp, 156`):
>     writes kind=0 at [bp-152], local_num at [bp-156/-154], qst at [bp-158],
>     emit at [bp-162/-160].  slot() = `-6 - 2*(fn->slot - s)`; prologue pushes
>     bx/si/di (6B) AFTER `mov bp,sp` then `sub sp, 2*fn->slot`, so SP = bp-6-156
>     = bp-162 = arg slot 0 (emit).  Frame is CORRECT (my first "frame too small"
>     read was an arithmetic error — the -6 reg block).  Callee reads (far code →
>     4-byte ret addr → first arg at bp+6): emit@[bp+6], qst@[bp+10],
>     local_num@[bp+12], kind@[bp+16].  Map back: callee kind@[bp+16] == caller
>     [bp-152] == 0.  So STATICALLY kind should be 0.  Runtime says 0xb0.
>     CONTRADICTION not yet explained.
>   - There are TWO call sites: asm 10627 and 41310.  Both look statically correct
>     (41310: kind=0 @[bp-338], qst=`w 10`, local_num @[bp-342/-340], emit
>     @[bp-348/-346]).  Did NOT confirm which one actually runs for add's LOAD_FAST
>     (compile.c:643 EMIT_LOAD_FAST in compile_load_id).
>
> **NEXT — two concrete moves (do the probe FIRST, it's the fast path):**
>  1. **Reproduce as a pure minic/i8086 ABI probe — NO MicroPython, NO Victor.**
>     The whole point: this is a backend arg-passing bug for a `(l,w,l,w)` call
>     whose 4th arg is a `w` constant, under compact far-data (far-call → 4-byte
>     ret addr).  Write `minic/dos/examples/argmix_probe.c`: a function
>     `int f(void *p, unsigned a, unsigned long n, int k)` that returns/prints
>     `k`, called as `f(&x, 1, 0, 0)` (also try `f(&x, 1, 0, 7)`), gated
>     compact+large in tools/test-dos.sh (DOSBox — fast iteration, no SASI loop).
>     If k arrives wrong → bug reproduced in the gate; iterate there.  Mirror the
>     exact widths: p=far ptr(l), a=unsigned(w), n=unsigned long(l, forces the Kl
>     in 3rd position), k=int(w) last.  This is the [[minic-wide-arg-narrow-param]]
>     family — but here the SSA is already correct, so look at i8086/abi.c
>     (selpar param-offset assignment + the max_arg_words/arg_slot_top reservation
>     at i8086_abi line ~358-387) and i8086/emit.c selcall (arg-slot stores) for
>     the `(l,w,l,w)` offset computation.  Suspect: an off-by-one/size error when
>     a Kl arg (n) sits between Kw args and the final Kw arg's slot offset is
>     mis-derived, OR the callee selpar reads the 4th param from a wrong [bp+off].
>  2. **If the probe does NOT reproduce it**, the fault needs the exact MP context
>     — then dump the RAW callee param bytes on-target: in mp_emit_bc_load_local
>     print `*(unsigned char*)((char*)&emit + 16)`-style or, cleaner, read the
>     param stack directly — to learn whether [bp+16] CONTAINS 0xb0 (arg-passing
>     bug) or holds 0 but `kind` (%t3) is mis-bound/clobbered (param-read bug).
>     Also confirm WHICH call site runs (add a distinct marker per site).
>
> **CLEANUP REQUIRED before any milestone run / commit (all in the MICROPYTHON
> tree ~/projects/micropython, uncommitted; the qbe repo has NO changes this
> session except the pre-existing uncommitted asm_to_omf.py TEXT_SEG_BUDGET
> env-override scaffolding, which is harmless):**
>  - `ports/dos8086/mpconfigport.h`: MICROPY_HEAP_SIZE was reduced 20480 → **16384**
>    for instrumentation headroom — **RESTORE to 20480** before any real run.
>  - `ports/dos8086/main.c`: do_str() is set to the add() test — restore to
>    `do_str("print(1+2)", MP_PARSE_SINGLE_INPUT)` (or keep the add test until the
>    bug is fixed — your call).
>  - `py/vm.c`: VMK macro + M1/C0/C1 markers + an unused `vmhex` function — remove.
>  - `py/objfun.c`: FK macro + F0/Fe/Ff markers — remove.
>  - `py/bc.c`: SK macro + `skhex` fn + S0/S1/S2/S9/" Z"/" Y" markers + the
>    self->bytecode[0..15] dump loop — remove.
>  - `py/emitbc.c`: `ebhex` fn + the `LF`+kind+local_num print in
>    mp_emit_bc_load_local — remove.
>  - `py/parse.c`: the §2n 4-byte-alignment fix — **KEEP** (uncommitted, load-bearing).
>  - `mpy-cross/build/mpy-cross` exists; reference disasm recipe:
>    `printf 'def add(a,b):\n return a+b\n' > /tmp/t.py && mpy-cross/build/mpy-cross /tmp/t.py -o /tmp/t.mpy && python3 tools/mpy-tool.py -d /tmp/t.mpy`
>
> **HARNESS NOTES (learned the hard way this session — see
> [[feedback-victor-harness-deterministic]], updated):**
>  - Run via the Bash tool's NATIVE `run_in_background:true`, redirect script
>    stdout to a file (`tools/run-victor-sasi.sh build/mp-link/mpython.exe 60 > /tmp/run.log 2>&1`).
>    Do NOT wrap in `timeout` and do NOT rely on a long foreground call — the
>    harness auto-backgrounds those and the teardown fires the script's
>    `trap cleanup EXIT INT TERM` → `kill -9` MAME before any serial (symptom:
>    0-byte output file + no mame in `ps`).
>  - ALWAYS verify with `ps -eo pid,etime,comm | grep -i mame` — do not claim
>    "it's running" without checking.
>  - For HANG debugging use a SHORT `-seconds_to_run` (60), not 250: a hanging
>    guest runs the FULL N guest-seconds under -nothrottle = HOURS of wall time
>    (a 250s hang ran ~4h).  A successful run halts early.  Read the LIVE serial
>    capture (`$WORK/serial.txt`, path from `ps -ww` of the mame cmdline) to see
>    the trace up to the hang before MAME exits; then `kill -9` mame + sweep
>    orphan `sleep 1120` watchdogs.
>  - Fast inner loop: `tools/recompile-mp-tu.sh <base> <src>` rebuilds ONE TU and
>    relinks (reuses every other .obj + /tmp/mp_objs.txt + --pack-code).
>    Full build: `bash tools/build-micropython.sh --model=compact --keep-going`.
>
> ---
>
# (prior) Next session (§2p DONE — size shrink lever: `omf_link.py --pack-code` coalesces the gc-surviving per-function CODE segments back into a few <=64KB buckets, reclaiming the per-function paragraph padding. Compact far-data mpython image body 542528→537360 B (−5168), so the ~800 B of Victor headroom becomes ~6 KB. Flag-off is BYTE-IDENTICAL to the prior link (default path unchanged); flag wired into build-micropython.sh + recompile-mp-tu.sh. Packed mpython VERIFIED on the real Victor: full trace `C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5` — still prints `3`. No minic/qbe-backend change.)

> **§2p (DONE 2026-06-01) — SIZE SHRINK LEVER: `omf_link.py --pack-code`.**
> The user picked "size headroom" (the recurring wall — §2o shipped with only
> ~800 B under the ~824 KB Victor load ceiling).  This is a capability-free win
> (no heap/feature cut) entirely in the linker.
>
> ROOT OF THE WASTE: `--gc-sections` strips dead code at PER-FUNCTION
> granularity, so `asm_to_omf.py` emits one CODE segment per function and the
> linker placed each at its own paragraph base.  788 surviving CODE segments ×
> up to 15 B of paragraph padding = **5,855 B of inter-segment padding** in the
> core-subset image (measured from the map).
>
> FIX (`tools/omf_link.py`, opt-in `--pack-code`): after gc-sections decides
> liveness, greedily coalesce the live CODE segments (in module/SEGDEF order)
> into <=64 KB buckets (`CODEPACK<n>`), appending each function WORD-aligned
> instead of paragraph-aligned.  Functions are reached by offset within the
> bucket, not by their own segment selector, so paragraph alignment is
> unnecessary; the bucket itself is paragraph-aligned by `_layout_segments`.
> SOUND because every code reference is an offset-aware OMF fixup —
> `_resolve_target` returns `(out_idx, base+disp)` from `seg_map`, the 16-bit
> offset patch computes `tgt_abs_byte - frame_byte` (= the function's offset
> within its bucket), the SEG selector writes the bucket's `para_base`, and the
> far-ptr patch uses `tgt_byte_in_out`.  Self-relative (near) jumps stay
> intra-function, hence intra-bucket, so their displacement is preserved.  The
> DATA path already coalesces exactly this way (`_place_coalesced`).  Bucket cap
> `CODE_BUCKET_MAX = 65500` keeps every offset < 65536.
>
> RESULT: 788 CODE segs → **7 buckets**; inter-seg padding 5,855 → 341 B;
> compact far-data mpython image body **542,528 → 537,360 B (−5,168)**.  Headroom
> under the Victor ceiling goes from ~800 B to ~6 KB.  Relinking the SAME objects
> with the flag OFF is **byte-identical** to the committed `mpython.exe` (proves
> the default path is untouched — stronger than re-running the gate, which only
> ever links flag-off; the DOS gate is therefore green by construction).
>
> VERIFIED ON THE REAL VICTOR (MAME/SASI, packed image): full trace
> `C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5` — parse, compile, `print` emits `3`, module
> call returns clean.  Identical behaviour to §2o, 5 KB lighter.  (NB on a
> harness gotcha — NOT "MAME flakiness": run-victor-sasi.sh is DETERMINISTIC
> (fixed disk copy + `-seconds_to_run` + `-nothrottle`), so a deterministic
> emulator can't be intermittently flaky and a fixed boot can't make the guest
> nondeterministically hang.  My first run produced empty serial because it was
> auto-BACKGROUNDED by the shell tool and then torn down — the script's
> `trap cleanup EXIT INT TERM` (line 80) `kill -9`s MAME on any signal, and
> there was NO `mame` process at all when checked (a hung guest would show a
> live MAME pinning a core).  Run it in the FOREGROUND with an explicit wait and
> it completes every time.  If serial is ever empty, debug it for real — inspect
> the raw `$CAP`, check for the `__V9BEGIN__` sentinel and whether MAME reached
> `-seconds_to_run` — do not just re-roll.)  Flag wired into
> `tools/build-micropython.sh` and
> `tools/recompile-mp-tu.sh`; `tools/test_omf_link.sh` test1 (MZ correctness)
> passes.
>
> FURTHER LEVERS NOT YET TAKEN: the 45 per-module `FAR_DATA` segments are still
> paragraph-distinct (they carry far-addressed static data, each reached by its
> own `seg _sym` selector, so they CANNOT be blindly coalesced like CODE — a
> coalesced one would need every contained symbol re-based, doable but a bigger
> change).  Other directions from §2o remain open: broaden the REPL
> (multi-statement / arithmetic / variables / for — next latent Kl/far bug), and
> Finding 3 (raised-exception-object far ptr loses its SEGMENT on the raise
> path).  Build: `bash tools/build-micropython.sh --model=compact --keep-going`;
> run FOREGROUND: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`.
>
> ---
>
# (prior) Next session (§2o DONE — 🎉 `print(1+2)` PRINTS `3` ON THE REAL VICTOR. The §2n "hangs inside `mp_arg_parse_all`" blocker was a minic call-ABI bug: a NARROW integer literal (`NULL`/`0`, always `w`) handed to a WIDE far-pointer parameter (`l`) was NOT widened, so the 2-byte push shifted every later stack arg. FIXED in `minic.y::coerce_arg`. DOS gate 169→172 green, `make check` green, 111 s/r 0 r/r. THE FIRST PYTHON STATEMENT RUNS END-TO-END ON 1982 HARDWARE.)

> **§2o (DONE 2026-06-01) — 🎉 MILESTONE: `print(1+2)` → `3` on the real Victor
> 9000 (MAME/SASI), reproducible.  The §2n "hangs inside `mp_arg_parse_all`"
> blocker was a minic call-argument-width bug — the REVERSE of the §2h/§2i
> wide→narrow shift.  DOS gate 169→**172**, `make check` green, 111 s/r 0 r/r
> (C-action only).  Fix + probe committed.**
>
> ROOT CAUSE (found by reading the generated SSA/asm — the static method, no
> MAME needed to localize it): `mp_builtin_print` calls
> `mp_arg_parse_all(0, NULL, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args,
> u.args)`.  The 2nd parameter `pos` is `const mp_obj_t *` = a far pointer (`l`,
> 4 bytes under far-data), but minic typed the `NULL` literal as integer `0`
> (`w`, 2 bytes) and emitted `call $mp_arg_parse_all(w 0, w 0, l %t7, w %t8, ...)`
> — a 2-byte push where the callee reads 4.  Every later stack arg shifted 2
> bytes: `n_allowed`, `allowed`, and `out_vals` were all read from the wrong
> slots, so the arg-parse loop ran wild over the 8-byte `u.args` stack union and
> corrupted the return frame → hang/reboot, before `3` could print.  (The
> `n_allowed = MP_ARRAY_SIZE = sizeof/sizeof` was a RED HERRING — it emits
> `%t8 =w div 16, 8` and the asm `idiv` correctly yields 2.)
>
> WHY §2i DIDN'T COVER IT: §2i's `coerce_arg` only fired when BOTH the arg and
> the param were INTEGER scalars (`arg_int && par_int`), to narrow/widen
> int<->int.  A pointer param has `KIND==PTR`, so `par_int` was false and the
> `NULL` arg was left as `w`.  But pointers are integers at the IR level (`w`
> near / `l` far), so a width mismatch against a pointer param shifts the stack
> exactly the same way.
>
> FIX (`minic.y::coerce_arg`): broaden the scalar test from "is an integer kind"
> to "is NOT a float and NOT a by-value aggregate" for both arg and param —
> i.e. treat pointers/functions as width-relevant scalars.  The existing
> widen (`w`→`l`: Con→retype to LNG, else extuw/sext) and narrow (`l`→`w`)
> machinery then matches the arg's IR class to the param's.  `NULL`/`0` to a far
> pointer param now emits `l 0`.  Floats and aggregates still bypass (aggregates
> cross by pointer via eval_arg/emit_arg; float<->int would be a real conversion,
> not a width fix).  Verified: the regenerated SSA is now
> `call $mp_arg_parse_all(w 0, l 0, l %t7, w %t8, ...)`.
>
> PROBE: `minic/dos/examples/nullarg_probe.c` (+golden), gated medium + compact +
> large.  Mirrors the bug shape — `report(0, NULL, 5, 7L)` (NULL as a non-last
> far-pointer arg, scalars after it) and `fill_one(NULL, 42, &dst)` (out-pointer
> last must still land).  Medium is a no-op (near pointers are `w`); compact/large
> are bug-loud without the fix.  This is the reverse companion to
> `argwiden_probe.c`.  See [[minic-wide-arg-narrow-param]].
>
> ON-TARGET RESULT (clean compact far-data build, body 823392 B, under the
> ~824.2KB Victor ceiling, 106/106 + --gc-sections): the full trace is
> **`C1 C2 C3 C4 D0 D1 D2 D3 3 D4 C5`** — parse (D2), compile (D3), then the
> builtin `print` emits **`3`**, then the module call returns clean (D4).
> Reproducible across runs.  **The first Python statement executes end-to-end on
> 1982 hardware.**
>
> NEXT (no blocker — pick a direction):
>  - **Broaden the REPL** to multi-statement / arithmetic / variables / `for`
>    and fix whatever the next latent Kl/far codegen bug is (the fragility notes
>    below still apply — layout shifts can flip a latent bug).
>  - **Size headroom** is the recurring wall (~800 B under the ceiling).  A real
>    shrink lever (dead-code, smaller heap-with-real-gc, code-seg packing) buys
>    room for a bigger feature set / instrumentation.  See §2b/§1z.
>  - Finding 3 (raised-exception-object far ptr loses its SEGMENT on the raise
>    path) is STILL open — needed before any uncaught exception can print
>    correctly; instrument `mp_raise`→`nlr_raise`→`nlr_jump`.
>  - MP tree state: `py/parse.c` = the §2n 4-byte-alignment fix (UNCOMMITTED in
>    the micropython repo, KEEP).  `ports/dos8086/` untracked (main.c C/D phase
>    markers + heap 20480 + real gc_collect).  All other py/*.c CLEAN.
>  - Build: `bash tools/build-micropython.sh --model=compact --keep-going`;
>    run FOREGROUND: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`
>    (re-run on empty serial — MAME flakiness).
>
> ---
>
# Next session (§2n DONE — the §2m "module bytecode never calls print" blocker was TWO 4-byte-ALIGNMENT root causes. (1) parse-node chunk misalignment → compile emitted garbage; (2) const-object data misalignment (asm_to_omf dropped `.balign`) → call dispatch crashed. Both FIXED. `print(1+2)` now compiles CORRECTLY, the VM dispatches the calls, and `mp_builtin_print` is ENTERED. NEW blocker = hangs INSIDE `mp_arg_parse_all`. DOS gate 169/169 green; asm_to_omf fix committed `3f9f8c1`.)

> **§2n (DONE 2026-06-01) — the §2m "module bytecode never calls print" blocker
> was TWO independent 4-byte-ALIGNMENT bugs.  MicroPython's tagged pointers
> (`mp_obj_t` AND parse-node `mp_parse_node_t`) store kind/tag bits in a
> pointer's low 2 bits and REQUIRE every object & parse-node struct to be
> >=4-byte aligned (`(p & 3) == 0`).  On 16-bit (size_t=2, far ptr=4) two places
> violated that; on 32/64-bit the headers/objects are naturally >=4-aligned so
> upstream never hits it.  DOS gate 169/169 green.**
>
> FIX 1 — **parse-chunk header misalignment** (`~/projects/micropython/py/parse.c`,
> KEPT uncommitted in the MP tree).  `mp_parse_chunk_t` header = `size_t alloc`(2)
> + `union{size_t used; chunk* next}`(4) = **6 bytes** on i8086, so `chunk->data`
> — and hence EVERY parse-node struct — landed at offset ≡2 (mod 4).  Every
> struct node was then misclassified as a LEAF (`MP_PARSE_NODE_IS_STRUCT` =
> `(pn&3)==0` failed), so `compile_node(scope->pn)` saw the whole `print(1+2)`
> tree as a single `MP_PARSE_NODE_STRING` leaf → emitted ONE bogus
> `LOAD_CONST_STRING` (+ the module epilogue), never a CALL.  FIX in
> `parser_alloc()`: round each request up to a 4-multiple and align the FIRST
> allocation within each fresh chunk to a 4-byte boundary (subsequent allocs stay
> aligned since all node requests are 4-multiples; the chunk base from m_new is
> already GC-block(16)-aligned).
>
> FIX 2 — **const-object data misalignment** (qbe `tools/asm_to_omf.py`,
> COMMITTED `3f9f8c1`).  asm_to_omf DROPPED `.balign`/`.p2align`, so minic's
> careful 4-/16-byte alignment of file-scope aggregates collapsed to the
> segment's word(2) alignment.  `_mp_builtin_print_obj` landed at off=0x00DE
> (`0xDE & 3 == 2`) → `mp_obj_is_obj(&print_obj)` returned false →
> `mp_obj_get_type` fell to `types[o & 0xf]` and returned the WRONG type →
> `type->slots[slot_index_call-1]` dispatch crashed before reaching the builtin.
> FIX: translate `.balign N`/`.p2align k` to NASM `align` in the data/bss/`_HUGE_`
> buckets (drop only in `.text` — perf-only), and declare the data/bss segments
> `align=16` so the linker paragraph-aligns them and NASM accepts the in-segment
> align directives.  After the fix `_mp_builtin_print_obj` is at off=0x00E8
> (4-aligned).  Gate byte-output unchanged → **169/169 ok**.
>
> HOW FOUND (decisive method = read artifacts + minimal markers): instrumented
> the VM dispatch loop (`py/vm.c`, switch on `*ip`) → executed opcodes were
> `10 51 63` (LOAD_CONST_STRING + LOAD_CONST_NONE + RETURN), NOT the expected
> `11..11..83 34 34 59 51 63`.  Dumped the raw module bytecode (`08 02 07 10 81
> a0 3f 51 63`) and a reference via mpy-cross.  Traced `compile_node` →
> `compile_node(root)` took the `MP_PARSE_NODE_STRING` LEAF branch (root low
> nibble 6) → parse-chunk header math (6 bytes) pinned FIX 1.  After FIX 1:
> `CsCrKK` ×4 (compile correct, both call_function emits) + `F1` (VM dispatches a
> real CALL_FUNCTION) but a CRASH in the call dispatch → linked map showed
> `_mp_builtin_print_obj` at off=0xDE (2 mod 4) → asm_to_omf `.balign` drop → FIX 2.
> After BOTH: trace `C1..D3 FF P0` — compile completes, both calls dispatch, and
> **`mp_builtin_print` is ENTERED** (P0 marker).
>
> **THE NEW blocker — `print(1+2)` hangs INSIDE `mp_arg_parse_all`.**  Trace
> reaches `P0` (top of `mp_builtin_print` in py/modbuiltins.c) but the very next
> call, `mp_arg_parse_all(0, NULL, kw_args, n_allowed, allowed_args, u.args)`,
> never returns (a "Pa" marker right after it never printed; print(3) emits no
> `3`, and after the 2nd call the machine hangs/reboots).  `mp_map_lookup` on the
> empty fixed kw map is safe (linear scan, no `%alloc` div-by-zero — it's
> `is_ordered`).  PRIME SUSPECTS: (a) reading the function-local `static const
> mp_arg_t allowed_args[]` from far data (alignment / far-load of the struct
> array), or (b) a miscomputed `n_allowed = MP_ARRAY_SIZE(allowed_args)` =
> `sizeof(arr)/sizeof(arr[0])` on a local static-const struct array → loop runs
> wild.  NEXT: add a 16-bit marker just inside `mp_arg_parse_all` and one in the
> loop (put any `extern void mp_hal_stdout_tx_strn_cooked(const char*, size_t);`
> at FILE scope — an in-FUNCTION-body prototype made minic parse-error), OR
> verify `sizeof(allowed_args)` / each `allowed[i].flags` read on-target; print
> `n_allowed`.  Suspect a remaining far-load/sizeof miscompile.
>
> **CAVEATS for next session (the build is fragile):**
>  - **Heap**: heap=16384 is TOO SMALL — `mp_compile` exhausts it and the trace
>    STOPS AT D2 (it's OOM, not a codegen bug).  heap=20480 (current
>    mpconfigport.h) compiles fine.  heap=22528 also works but the alignment-fix
>    bloat pushes the body to 824816 (> ~824192 ceiling) → won't load.  Keep heap
>    <= 20480 with the alignment fix.
>  - **Layout fragility**: tiny instrumentation changes flip a LATENT compile-path
>    Kl/far codegen bug — symptoms range from D2-stop to EMPTY serial output.
>    Add markers in MINIMAL increments and re-confirm D3 each time.  (The 3-marker
>    P0/Pa/P1 build compiled; adding 2 more flipped it to D2.)  This still hints
>    more latent Kl/far bugs remain (the D3 success is not robust to layout).
>  - **MAME flakiness**: the SASI harness intermittently produces EMPTY serial
>    output (exit 0, no C1).  Just RE-RUN.  Run FOREGROUND (background runs seem
>    to lose serial more often).
>  - Build: `bash tools/build-micropython.sh --model=compact --keep-going`
>    (full, ~1min, applies alignment everywhere — REQUIRED after touching any TU
>    if you want consistent alignment), or `tools/recompile-mp-tu.sh <base> <src>`
>    (one TU + relink).  Run: `tools/run-victor-sasi.sh build/mp-link/mpython.exe 250`.
>  - MP tree state: `py/parse.c` = the alignment FIX (uncommitted, KEEP).
>    `ports/dos8086/` untracked (main.c has C1..D4/DE phase markers, gc_collect is
>    the real collector, heap 20480).  All other py/*.c reverted CLEAN.
>
> ---
>
# Next session (§2m DONE — the §2l "print emits no visible output" blocker was TWO minic static-data/typing bugs in the int-print SLOT DISPATCH, now FIXED and committed `2fd2f4b`. `make check` green, both proven on the real Victor. NEW blocker = `print(1+2)`'s MODULE BYTECODE never calls `print` at all — a compile/VM bug, NOT an output bug.)

> **§2m (DONE 2026-06-01, committed `2fd2f4b`) — the §2l "print emits no
> visible output" blocker is RESOLVED.  It was TWO minic bugs in how
> `mp_obj_print_helper` reaches the int formatter via the type-slot far
> fn-ptr dispatch.  `make check` green (111 s/r, 0 r/r), amd64/arm64/rv64
> byte-identical.  Both found by on-target bisection on the real Victor.**
>
> HOW FOUND (the decisive method again): added a DISCRIMINATOR in
> `ports/dos8086/main.c` that called `mp_obj_print_helper(&mp_plat_print,
> MP_OBJ_NEW_SMALL_INT(3), PRINT_STR)` DIRECTLY (bypassing the VM/bytecode) —
> it HUNG.  Then instrumented `mp_obj_print_helper` (`Y0`/`Y1`/`Y2`/`Y3`) and
> `mp_obj_int_print` (`Z0`): trace `Y0 Y1 Y2` then hang, NO `Z0` — so
> `mp_obj_get_type` returned (Y1), `HAS_SLOT(print)` was true (Y2), but the
> far slot-dispatch call `MP_OBJ_TYPE_GET_SLOT(type,print)(...)` never reached
> `mp_obj_int_print`.  A DIRECT call to `mp_obj_int_print` (bypass the slot)
> printed `3` fine → the formatter is correct; the BUG is the slot load+call.
> A seg:off dump at the call site showed the returned `type` segment was
> STACK GARBAGE (`c5ea`) not `&mp_type_int` (`c330`), and the loaded slot fn
> ptr had segment `0000`.  Reading the generated asm pinned both root causes:
>
> BUG 1 — **flexible-array-member initializer emitted ONLY its first element.**
> `mp_obj_type_t` ends in `const void *slots[]`.  minic's `agg_emit_struct`
> routed the braced `.slots = {make_new, print, ...}` through
> `agg_emit_value`→`agg_emit_scalar` (a flex member has `count==0`), emitting
> just `slots[0]`.  So `slots[1]` (the `print` fn) read past-the-array garbage
> and the far call jumped wild.  Verified in the emitted data: `_mp_type_int`
> stopped after `dd _mp_obj_int_make_new` (`; end data`).  FIX (`minic.y`):
> detect `m->isflex`, count brace elements (new `agg_brace_count`, honours
> `[k]=v`), emit the whole array via `agg_emit_array`.
>
> BUG 2 — **array-of-pointers subscript used the wrong stride.**
> `mp_obj_get_type` returns `types[(uintptr_t)o & 0xf]` from a static
> `const mp_obj_type_t *const types[]`.  `array_vartyp` registered the array
> with the ELEMENT (pointer) type itself, so `types[i]` scaled by
> `sizeof(*T)` (= 20, `sizeof(mp_obj_type_t)`) instead of `sizeof(T*)` (= 4).
> The asm showed `mov bx,20; imul bx` — `types[7]` indexed at base+140, way
> out of the 16×4-byte array → a wrong-segment type ptr.  FIX (`minic.y`):
> `array_vartyp` always returns `IDIR(elemtyp)` (a C array of T decays to T*;
> `T *arr[]` decays to `T **`).  Byte-identical for scalar/struct-element
> arrays; only pointer-element arrays were mistyped.  Asm after the fix:
> `mov bx,4; imul bx`.
>
> VERIFIED on the real Victor: with both fixes the slot dispatch reaches
> `mp_obj_int_print` and prints `3` (`Y2 c330 c330 5a80 Z0 3 Y3` — returned
> type seg == &mp_type_int seg, slot fn ptr in a real code segment).  Gate
> probe `slotarray_probe.c` (compact + far-static, MicroPython's config)
> exercises BOTH bugs (a flexible fn-ptr-member dispatch + a runtime-indexed
> `int *` array); passes on the Victor via the MAME/SASI harness.
> fnptrprobe re-verified unchanged.  **Full DOS gate GREEN: 168→169/169 ok**
> (the new slotarray_probe row; no regressions from the two minic fixes).
> NOTE: `tools/run-dos-exe.sh` had a macOS launch bug — it ran `open -gjWn -a
> dosbox.app --args …`, but `open -a <bundle> --args` does NOT forward flags
> to DOSBox (every probe reported "no OUT.TXT").  Fixed (committed `78f1cf1`)
> to `open -a <binary> -g -j -W -n --args …` (point at
> .../Contents/MacOS/DOSBox).  The MAME Victor harness (run-victor-sasi.sh /
> run-victor-mame.sh) is the other validation path and works headlessly.
>
> SIZE NOTE: the BUG 1 fix correctly emits all the previously-dropped type
> slots, so the compact far-data mpython body grew 823360 → **824928 B**,
> OVER the ~824.2KB Victor load ceiling.  To get a loadable image for the
> milestone run, `ports/dos8086/mpconfigport.h` `MICROPY_HEAP_SIZE` was
> reduced 24576 → **22528** (still ample for `print(1+2)` with working
> gc_collect; print isn't even called yet so heap is moot for now) → body
> 822880, loads.  This is an UNCOMMITTED micropython-tree change; revisit
> with a real shrink lever (the ceiling is the recurring wall — see §2b/§1z).
>
> **THE NEW blocker — `print(1+2)`'s MODULE BYTECODE never calls `print`.**
> With both fixes + the smaller heap, the full pipeline runs clean:
> `C1..C4 D0 D1 D2 D3 D4 C5` (parse, compile, execute all complete, NO
> exception) — but STILL no `3`.  Since the print OUTPUT path is now PROVEN
> working (the slot dispatch printed `3` from main.c's direct call), the only
> remaining explanation is that the compiled module function doesn't call
> `print`.  CONFIRMED on the Victor: a marker at `mp_call_function_n_kw`
> (py/runtime.c — the universal call dispatcher the VM's MP_BC_CALL_FUNCTION
> at py/vm.c:984 routes through) printing `F<n_args>` fired EXACTLY ONCE as
> `F0` (the outer `mp_call_function_0(module_fun)` from main.c) — there was
> NO second call (no `F1`).  So the module function executes and returns
> cleanly but never dispatches a `CALL_FUNCTION` for `print`.  This is a
> COMPILE or VM bug (the expression statement `print(1+2)` either compiled to
> bytecode with no working CALL, or the VM exits/mis-dispatches before
> reaching MP_BC_CALL_FUNCTION).  NEXT: instrument the VM opcode dispatch loop
> in `py/vm.c` (a minimal per-opcode marker, or specifically the
> MP_BC_CALL_FUNCTION / MP_BC_LOAD_NAME / MP_BC_LOAD_GLOBAL / MP_BC_RETURN
> entries) and/or dump the module function's raw bytecode to see whether the
> CALL opcode is present (compile bug) or present-but-not-executed (VM
> dispatch bug — likely another Kl/far-data miscompile in the
> computed-goto/opcode-fetch).  Use 16-bit-only debug printers (no Kl shift).
> Build: `bash tools/build-micropython.sh --model=compact --keep-going`; run:
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`.  The
> micropython tree is CLEAN except the untracked `ports/dos8086/` port (C/D
> markers in do_str + the 22528 heap).  Finding 3 (raised-exc-obj far ptr
> loses its segment) is STILL open but does NOT block (no exception raised).

> **§2l (DONE 2026-06-01, committed `ed5f35b`) — the §2k "uncaught exception
> during mp_compile" is FIXED.  It was a general QBE i8086 BACKEND codegen bug in
> the Kl (32-bit) bitwise `Oand`/`Oor`/`Oxor` handlers.  DOS gate 166→**168**,
> `make check` green, amd64/arm64/rv64 byte-identical.  All in `i8086/emit.c` +
> a new gate probe.**
>
> ROOT CAUSE: the Kl `Oand`/`Oor`/`Oxor` handlers used `load32_dxax` + the op,
> operating in AX/DX, but NEVER preserved the caller's AX/DX — unlike the Kl
> `Oadd`/`Osub`/`Omul` handlers, which bracket with `kl_save_axdx`/`kl_restore_axdx`
> ([[i8086-kl-add-sub-mul-r1-alias]]).  rega doesn't model the implicit AX/DX
> clobber of these ops, so a live SSA value rega parked in AX/DX across a Kl
> OR/AND/XOR was silently corrupted.  (CLAUDE.md's open [[i8086-kl-shift-clobbers-ax]]
> note was STALE — the Kl SHIFT handlers already had the save/restore bracket; it
> was the logical ops that were unfixed.)
>
> CANONICAL VICTIM (found by on-target bisection on the real Victor, the decisive
> method again): MicroPython's `MP_BC_PRELUDE_SIG_ENCODE` (py/bc.h).  `mp_uint_t`
> is `uintptr_t` = 4 bytes under far-data, so the module prelude's
> `while (S|E|F|A|K|D)` condition lowers to a chain of Kl ORs.  rega kept the
> loop-carried byte `z` (the encoded prelude byte, used by both the loop body's
> `0x80|z` and the post-loop `out_byte(z)`) live in AX across the OR-chain; the
> OR-chain clobbered AX, so the encoder emitted a bogus multi-byte prelude
> (`0x82 0x81 0x00` instead of `0x08`).  Decoded back in `mp_setup_code_state`,
> that read as a module taking 2 positional args + 1 default, so `fun_bc_call`
> raised TypeError on `mp_call_function_0` of `print(1+2)` — the long-standing
> "exception during compile/exec" blocker.
>
> HOW FOUND (one decisive marker run at a time): instrumented `py/compile.c`
> (compile COMPLETED — the exception had MOVED to execution), then `py/objfun.c`
> `fun_bc_call` (raise was between `mp_cstack_check` and `mp_execute_bytecode`,
> in `INIT_CODESTATE`→`mp_setup_code_state`), then `py/bc.c`
> `mp_setup_code_state_helper` (decoded `n_pos_args=2, n_def=1` — wrong; raw
> prelude bytes `0x82 0x81 0x00`), then `py/emitbc.c` `mp_emit_bc_start_pass`
> (encode INPUTS correct: `num_pos_args=0`, `n_state=2`; but `code_base[0]`
> written = `0x82`, should be `0x08`).  Then read the generated SSA/asm:
> `mp_uint_t` ops were typed `l`, the `while`-condition OR-chain (asm lines
> "mov ax,[slot]; or ax,[slot]; mov [slot],ax", NO push/pop) clobbered the `z`
> value rega had loaded into AX before the loop.
>
> FIX (`i8086/emit.c`): bracket each of Kl `Oand`/`Oor`/`Oxor` with
> `kl_save_axdx`/`kl_restore_axdx` and handle the dst-in-DX RTmp case, mirroring
> `Osub` Kl exactly.  Slot operands are bp-relative so the AX/DX push/pop (which
> move SP, not BP) leaves their offsets valid.  Probe `sigencode_probe.c`
> (medium + compact) replicates the encode with `unsigned long` (Kl) inputs;
> bug-loud (`b0=0`) without the fix, `b0=8` with.  Wired into `tools/test-dos.sh`
> RUNTIME_TESTS.  (Note: the isolated probe needs the Kl-OR-chain `while`
> condition + a loop-carried value to trigger — a too-minimal probe folds clean.)
>
> ON-TARGET RESULT (clean full rebuild, body 823360 B, under the ~824.2KB
> ceiling): the trace advanced from `C1..C4 D0 D1 D2 DE` to
> **`C1 C2 C3 C4 D0 D1 D2 D3 D4 C5`** — `mp_compile` returns (D3) AND
> `mp_call_function_0` returns with NO exception (D4).  Confirmed reproducible.
>
> **THE NEW blocker — `print` emits NO VISIBLE OUTPUT.**  `print(1+2)` executes
> cleanly (D3→D4) but the `3` never appears (raw `od -c` of the serial capture
> shows ONLY the C/D markers — no `3`, no `\n`).  `print` routes through
> `mp_plat_print` → `MP_PLAT_PRINT_STRN` → `mp_hal_stdout_tx_strn_cooked` — the
> SAME path the C/D markers use (and those DO appear) — so EITHER (a) the module
> bytecode never actually calls `print` (a remaining VM/bytecode miscompile that
> raises no exception), OR (b) `print` is called but `mp_obj_print_helper(int 3)`
> / `mp_print_int` produces zero output (a far-data int→string bug).
>
> **CAUTION for the next session — razor-thin SIZE margin + instrumentation
> heisenbugs.**  The clean image body is 823360 B; the Victor ceiling is
> ~824192 B (loads) / 824512 B (does not) — only ~830 B of headroom.  Worse, a
> small marker added to `py/modbuiltins.c` (mp_builtin_print entry) regressed the
> trace to `D2` EVEN THOUGH the image still loaded (823824 B < ceiling) — a
> LAYOUT-induced compile-path codegen bug, not a load failure.  So: instrument
> with MINIMAL net size growth, prefer TUs NOT on the compile path, and/or
> temporarily stub something (e.g. `gc_collect` empty) for headroom; re-confirm
> the baseline `D3 D4` after any change.  This fragility also hints MORE latent
> Kl/far-data codegen bugs remain — the D4 success is correct but not robust to
> layout shifts.
>
> NEXT: (1) determine if `print` is CALLED — instrument the VM call dispatch
> (`fun_builtin_var_call` in py/objfun.c, or the `MP_BC_CALL_FUNCTION` opcode in
> py/vm.c) with a MINIMAL marker (watch size).  If NOT called → a bytecode/VM
> miscompile (likely another Kl/far op); bisect the emitted opcodes for the
> module.  (2) If called → trace `mp_obj_print_helper`→`mp_print_int`/the int
> formatter for the lost output.  Use 16-bit-only debug printers (no Kl shift).
> Build: `bash tools/build-micropython.sh --model=compact --keep-going`; run:
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`.  MicroPython tree
> (~/projects/micropython, separate repo) is CLEAN except the untracked
> `ports/dos8086/` port; the C/D phase markers in `main.c::do_str` remain.
> Finding 3 (raised exc obj far ptr loses its segment) is STILL open but does NOT
> block (no exception is raised now).
>
> ---

> **§2k (DONE 2026-06-01) — the §2j' "mp_compile EMIT-pass hang" is FIXED, and the
> §2j' emitbc.c jump-offset/size_t-width hypothesis was WRONG.**  The real bug was a
> general QBE i8086 BACKEND codegen bug in the Kw shift handler.  DOS gate 164→**166**,
> `make check` green.  All in `i8086/emit.c` + a new gate probe.
>
> ROOT CAUSE: the Kw shift handler (`Oshl`/`Oshr`/`Osar`, `cls != Kl`) only
> materialized the VALUE operand `arg[0]` into the destination register when it was an
> `RTmp` the allocator had already placed there.  When `arg[0]` was an `RCon`
> (constant) or `RSlot`, it was NEVER loaded — the shift ran on whatever the dest reg
> held, which for `CONST << count` was the freshly-computed COUNT.  So `1 << count`
> emitted `count << count`, and `1 << 0` → `0 << 0 == 0`.  (minic SSA was correct:
> `%t =w shl 1, %count`.)
>
> CANONICAL VICTIM: `py/gc.c` `gc_alloc`'s ATB head-mark
> `gc_alloc_table_start[b/4] |= (AT_HEAD << (2*(b&3)))` = `1 << (2*(b&3))`.  For a
> block whose index is divisible by 4 the shift is 0, so the mark became `|= 0` — a
> NO-OP.  The block was never recorded used, so the NEXT `gc_alloc` handed out the
> SAME live block and `m_new0` zero-filled it.  In the port this zeroed a freshly-
> allocated `scope`'s `pn` (parse-tree root) — set correctly by `scope_new`, then
> wiped by the immediately-following `raw_code = m_new0(...)` which returned the SAME
> address as `scope`.  `compile_node(scope->pn)` then saw NULL → the CODE_SIZE pass
> emitted nothing → `code_base` was sized at ~2 bytes → the EMIT pass overflowed it
> (~24 bytes) → GC-heap corruption → the observed "EMIT pass size won't settle / 3rd
> pass hangs."
>
> HOW FOUND (on-target bisection, the decisive method again): instrumented
> `emitbc.c`/`compile.c`/`scope.c` with 16-bit-only hex printers over
> `run-victor-sasi.sh`.  Trace showed CODE_SIZE emitted 2 bytecode ops vs EMIT's 12
> (opcodes differed: CODE_SIZE = just the module epilogue), then `scope->pn` read 0 at
> compile but was set correctly in `scope_new`, then `raw_code`'s `m_new0` returned the
> SAME address as `scope` → read the `gc_alloc` marking asm → `shl ax, cl` with `ax`
> holding the COUNT, never loaded with 1.
>
> FIX (`i8086/emit.c`): new `emit_shift_val(reg, r0, fn, f)` materializes the value
> operand (RTmp / RCon incl. CAddr / RSlot) into the shift register, emitted AFTER the
> count is secured into CL so a count register that aliases the destination is read
> before being overwritten.  The `dst==CX` via-BX path orders the value-save vs count-
> load per operand kind.  Probe `shlconst_probe.c` (+golden, medium+compact): the
> `gc_alloc` ATB pattern (`atb[b/4] |= 1<<(2*(b&3))` for 8 blocks → 0x55/0x55) plus
> direct `CONST<<var`; bug-loud (atb garbage) without the fix.  Wired into
> `tools/test-dos.sh` RUNTIME_TESTS.
>
> RESULT: clean compact far-data `mpython.exe` body 820480→**820624 B** (the fix adds a
> `mov reg,const` before some shifts; still well under the ~824.3KB ceiling).  On the
> real Victor (SASI) the EMIT-pass hang is GONE — the trace advanced from `D2`(hang) to
> **`D0 D1 D2 DE C5`**: `mp_parse` returns (D2), `mp_compile` now runs to completion-or-
> raise without hanging, and raises an exception (DE) before D3.
>
> **THE NEW blocker — an uncaught exception DURING `mp_compile`** (trace `D0 D1 D2 DE`,
> DE = `do_str`'s nlr-else branch in `ports/dos8086/main.c`, between parse-done D2 and
> compile-done D3).  This is the SAME shape as the §2i-era compile exception; with
> `gc_alloc` now correct, `mp_compile` is operating on un-corrupted data but still
> raises.  NEXT: instrument `py/compile.c` (and the emit path) to find WHERE in compile
> it raises and WHAT exception (likely a `comp->compile_error` setter, or a real raise
> from a remaining far-data miscompile).  Use 16-bit-only debug printers (no Kl shift).
> Note Finding 3 is STILL open: a raised exception object's far pointer loses its
> SEGMENT on the raise path (offset kept, segment garbage) — so even a legitimate error
> will print wrong / crash `mp_obj_print_exception`; pin that on the raise path
> (`mp_raise`→`nlr_raise`→`nlr_jump`) too.  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`; run: `tools/run-victor-sasi.sh build/mp-link/mpython.exe
> 200`.  The C/D phase markers in main.c::do_str remain; the MicroPython tree is
> otherwise CLEAN (all §2k instrumentation reverted).  fnptr_argwiden_probe.c (ungated)
> documents the still-latent fn-ptr wide→narrow arg-coercion gap (a prime suspect for
> the compile exception if it's an arg-shift through an emit_t-style fn-ptr call).
>
> ---

# (prior) Next session (§2j' DONE — the ~850 B shrink lever landed: emit_clit_aggr now fills local-init dest DIRECTLY, dropping the compound-literal temp + struct copy and fixing far member-init truncation; committed `df3c76a`, DOS gate 164/164, `make check` green, 111 s/r 0 r/r) — the compact far-data mpython image now LOADS on the real Victor (body 825152→820480 B, UNDER the ~824.3KB ceiling) and the §2j compile_error fix is HARDWARE-CONFIRMED (the §2i/§2j bogus `DE` exception is GONE). The `print(1+2)`→`3` milestone is STILL not reached: a NEW blocker is now exposed — `mp_compile` HANGS in the bytecode EMIT pass. NEXT = debug the EMIT-pass hang (precisely localized below; almost certainly an emitbc.c/compile_scope far-data codegen bug, NOT the struct-init change).

> **§2j' (DONE 2026-06-01, committed `df3c76a`) — the ~850 B shrink lever (the
> §2j NEXT) landed and is hardware-verified.**  DOS gate 164/164, `make check`
> green, 111 s/r 0 r/r (C-action only).
>
> WHAT LANDED (all `minic/minic.y`): `emit_clit_aggr` now takes a destination
> ADDRESS operand (any aggregate lvalue — a `%_clit` slot, a local `%var`, a
> `*p` deref temp) instead of only a `%_clit` number, and is FAR-CORRECT (member
> stores are `storef%c` at `=l add` offsets under far-data; the old `%_clit`-only
> path used near `=w add`+`store%c`, which TRUNCATED the segment of a pointer
> member written at offset>0 — a latent miscompile).  New `symb_operand` helper
> formats a Symb's address operand to a string.  The `=` handler special-cases
> `dst = (T){...}` (the desugaring the local-aggregate-init rules emit, and any
> user struct-literal assign): it fills `dst` IN PLACE via `emit_zero_aggr` +
> `emit_clit_aggr`, skipping the compound-literal temp AND the whole-struct copy.
> That drops far more than 850 B (every `S s = {...}` loses its temp alloc +
> ~size/2-word copy) AND fixes far-data member-init truncation for locals (the
> old non-zero path went through `emit_struct_copy`, which for a local aggregate
> computed `dst_far=false` under far-data → near stores into a far stack slot).
> Probe `local_zeroinit_probe.c` gains `init`/`mid2`/`desig` NON-zero cases
> (positional `small_t` + designated `big_t`) that DEREF a pointer member written
> past the first word, so a far-store segment truncation is bug-loud; medium is
> byte-identical (far=0 path unchanged).
>
> RESULT ON HARDWARE: compact far-data `mpython.exe` body 825152→**820480 B**
> (−4672), UNDER the ~824.3KB Victor load ceiling (824192 loads, 824512 doesn't).
> On the real Victor (SASI) the image LOADS and runs through `mp_init` +
> `mp_parse`: trace `C1 C2 C3 C4 D0 D1 D2`.  The §2i/§2j bogus compile-time `DE`
> exception is GONE (the §2j `compile_error` fix is hardware-confirmed; before, it
> was unverifiable because the image was over the ceiling).
>
> **THE NEW blocker — `mp_compile` HANGS in the bytecode EMIT pass.**  Bisected
> on-target with E-markers in `py/compile.c::mp_compile_to_raw_code` (markers
> since REVERTED — tree is clean; re-add via the recipe below).  Trace:
> `D2 E0 E1 E2 E3 Es Ec Ee Ei Ei` then HANG.  Decode:
>  - `E0`→`E3`: scope creation + `MP_PASS_SCOPE` loop + `scope_compute_things` all
>    COMPLETE.
>  - `Es`: `compile_scope(MP_PASS_STACK_SIZE)` ran and RETURNED.
>  - `Ec`: `compile_scope(MP_PASS_CODE_SIZE)` ran and RETURNED.
>  - `Ee`: entered the `while (!compile_scope(comp, s, MP_PASS_EMIT)) {}` loop.
>  - `Ei Ei`: the EMIT `compile_scope` returned FALSE (= `mp_emit_bc_end_pass`
>    reported the emitted size did NOT settle: `bytecode_offset != bytecode_size`
>    or `code_info_offset != code_info_size`, emitbc.c:384) on TWO successive
>    passes, requesting another pass each time.
>  - HANG: the **3rd** `compile_scope(MP_PASS_EMIT)` call never returns (no 3rd
>    `Ei`, no `Ed`/`E4`).  Even at 300s emulation it stays at the hang (true hang,
>    not slow; no reboot — no 2nd `__V9BEGIN__`).
> Bytecode emit normally converges in ONE pass; two non-settling passes is itself
> abnormal, so the bytecode/code-info SIZE is oscillating across EMIT passes —
> classic symptom of a miscompiled jump-offset / size accumulator under far-data
> (`bytecode_offset`/`code_info_offset` are `size_t` = `int` = 2 bytes in minic's
> stddef.h; the jump-encoding at emitbc.c:242-279 computes signed
> `label_offsets[label] - bytecode_offset - 2` and picks 1/2/3-byte encodings —
> a width/sign bug there would shift sizes every pass and never settle, and the
> 3rd pass walking a corrupted offset could loop).
>
> WHY THIS IS ALMOST CERTAINLY NOT THE §2j' STRUCT-INIT CHANGE: STACK_SIZE,
> CODE_SIZE, and TWO full EMIT passes all ran `compile_scope` to completion over
> the SAME struct initializers; a miscompiled local-aggregate init would corrupt
> every pass uniformly, not specifically hang the 3rd EMIT pass on a size-
> convergence path.  (If you want to be 100% sure, the only `S s = {...}`-shaped
> locals on the compile path are the suspect — but the gate's `local_zeroinit_probe`
> init/mid2/desig cases already pass on compact+large.)
>
> **NEXT — pin the EMIT-pass hang.**  (1) Instrument `py/emitbc.c`:
> `mp_emit_bc_end_pass` (print `bytecode_offset`/`bytecode_size`/`code_info_offset`/
> `code_info_size` each EMIT pass — see if they oscillate vs. grow unboundedly)
> and the jump-offset encoder `emit_write_bytecode_byte_signed_label`-style code
> at emitbc.c:242-279 (print `label`, `label_offsets[label]`, `bytecode_offset`,
> the chosen encoding size).  Use 16-bit-only debug printers (NO Kl shift —
> [[i8086-kl-shift-clobbers-ax]]).  (2) Recompile that ONE TU via
> `tools/recompile-mp-tu.sh emitbc ~/projects/micropython/py/emitbc.c` (compact
> far-data, --gc-sections; reuses every other .obj + /tmp/mp_objs.txt), run
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 200`, and watch the offsets.
> (3) Likely fix is in i8086/qbe codegen for the offset arithmetic / size_t
> compares (a width or sign-extension bug), or possibly in how `label_offsets[]`
> (a far array under far-data) is indexed/stored.  RE-ADD the compile.c E-markers
> recipe (if you need to re-confirm the phase): `#include "py/mphal.h"` +
> `#define EMARK(s) mp_hal_stdout_tx_strn_cooked((s),3)` after
> `#if MICROPY_ENABLE_COMPILER`, then `EMARK("E0\n")` at the top of
> `mp_compile_to_raw_code`, `E1` after `emit_bc_new`, `E2`/`E3` around the
> `scope_compute_things` loop, `Es`/`Ec`/`Ee` before the STACK_SIZE/CODE_SIZE/EMIT
> `compile_scope` calls, `Ei` inside the EMIT while-body, `Ed`/`E4` after the loop.
> CAUTION: markers cost image bytes — the clean image is 820480 B with ~3.7KB of
> headroom under the ceiling; a handful of EMARKs fit, but strip them before any
> milestone confirmation run.  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`.
>
> MICROPYTHON TREE STATE (separate repo, ~/projects/micropython, uncommitted):
> `py/compile.c` reverted CLEAN (E-markers removed).  `build/mp-link/mpython.exe`
> rebuilt clean (compile.obj marker-free, body 820480 B).  The C/D phase markers
> in `ports/dos8086/main.c::do_str` remain (D0=lexer, D1=before mp_parse, D2=parse
> done, D3=compile done, D4=call done, DE=exception).  `gc_collect` is the real
> stack-scanning collector.
>
> ALSO STILL OPEN (unchanged): Finding 3 — a raised exception object's far pointer
> loses its SEGMENT on the raise path; needed for any real exception to print, but
> `print(1+2)` raises none, so it does not block the milestone.

> # (prior) §2j — the `mp_compile` exception blocker (FIXED in f8040ee; this is the predecessor of §2j' above)

> **§2j (DONE 2026-06-01, committed `f8040ee`) — the §2i NEW blocker (an
> uncaught exception raised DURING `mp_compile` of the correct parse tree of
> `print(1+2)`) is FIXED.  It was NOT the suspected fn-ptr arg-coercion shift —
> that suspicion was WRONG for this config (MicroPython MINIMUM-ROM has
> `MICROPY_EMIT_NATIVE=0`, so `EMIT_ARG` is a DIRECT `mp_emit_bc_*` call, NOT
> emit_t method-table dispatch — already coerced by §2i).  DOS gate 161→164,
> `make check` green, 111 s/r 0 r/r (C-action only).**
>
> HOW IT WAS FOUND (on-target bisection, the decisive method): injected K0..K3
> phase markers + a Z0/Z1 init probe into `py/compile.c::mp_compile_to_raw_code`
> on the real Victor (SASI).  Trace showed compile ran ALL passes (`K3`) then hit
> `comp->compile_error != NULL` though `SYN` (the only compile_error setter once
> native is off) never printed — and `Z1` fired: `compiler_t comp_state = {0};`
> left the `compile_error` POINTER field (past the first word) as non-NULL stack
> GARBAGE.  So `mp_compile` raised a bogus exception on a perfectly good parse
> tree.  (Lesson reinforced: get on-target DATA before building a big speculative
> fix — the fn-ptr mechanism would have been wasted effort.)
>
> ROOT CAUSE: every minic local-aggregate zero-init looped `j += 4` while
> emitting a 2-byte `storew` (T.wordsz == 2 on i8086), zeroing only HALF the
> bytes — alternating 2-byte gaps of stack garbage; under far-data the near
> `=w add`/store also truncated the segment.  FIVE sites had this (the two
> compound-literal 'L' paths + three bare struct-local decl paths).
>
> FIX (all `minic/minic.y`): new `emit_zero_aggr(addr, s)` — correct full-
> coverage zero-fill (a `memset` call for s>8, mangled to `_far_memset` under
> far-data; tiny aggregates inline storel/storew/storeb with a 4-byte stride);
> all five sites route through it.  Implicit bare `struct S s;` zero-init is now
> gated on the struct actually having a bitfield (`struct_has_bitfield`) — C
> leaves an uninitialized automatic indeterminate, the old half-fill couldn't be
> relied on, and most structs have no bitfield (net size win).  `S s = {0};`
> locals (dcls + stmt rules) now zero the target DIRECTLY via `memset`, skipping
> the compound-literal temp AND the struct copy.  Probe `local_zeroinit_probe.c`
> (+golden), gated medium+compact+large; bug-loud (all members garbage) without
> the fix.  Also added `fnptr_argwiden_probe.c` (UNGATED) documenting the
> separate, still-latent fn-ptr wide→narrow arg gap (fn-ptr TYPE carries no
> param list; not hit by this config).
>
> **THE NEW state — image is ~850 B OVER the Victor load ceiling.**  Correct
> (full) zeroing legitimately costs more code than the buggy half-fill, so the
> compact far-data mpython.exe body grew to **825152 B**, and DOS reports
> "Program too big to fit in memory" on the Victor (observed ceiling: a 824192 B
> body LOADS, 824512 B does NOT — so ~824.3 KB).  Three shrink levers already
> applied this session got it from 828896→825152 (memset-for-big, direct-`{0}`
> on-target, bitfield-conditional bare-decl), but ~850 B remain.  The on-target
> `print(1+2)` → `3` MILESTONE is therefore NOT yet verified — it is blocked ONLY
> by this size wall, not by correctness (the fix is proven by the probe across 3
> models + the Z1 on-target root-cause).
>
> **NEXT — shrink ~850 B, then verify the milestone on the Victor.**  RECOMMENDED
> LEVER (clean, also fixes a latent bug): generalize `emit_clit_aggr` to take a
> destination ADDRESS (like `emit_zero_aggr` now does) and be far-correct
> (it currently uses near `=w add %_clit, off` + `store%c` for explicit members
> at offset>0 — which TRUNCATES the segment under far-data, a latent bug), then
> have the local-aggregate-init rules (dcls `type IDENT '=' '{' … '}'` ~line
> 6755, and the stmt-level one ~line 7360) fill the freshly-alloc'd `%v`
> DIRECTLY — zero it via `emit_zero_aggr("%v", s)` then `emit_clit_aggr("%v", …)`
> — eliminating the compound-literal temp AND the per-init struct copy for ALL
> aggregate initializers (not just `{0}`).  That removes far more than 850 B
> (every `S s = {…};` loses its ~S/2-word copy) AND fixes the far-data member-
> init truncation.  Keep the `%_clit` path for genuine compound-literal VALUES
> (`(T){…}` used as an rvalue).  Extend `local_zeroinit_probe.c` with a non-zero
> `S s = {a, b, …};` case (medium+compact+large) to guard the far-correctness.
> Then: `make check`, `tools/test-dos.sh` (expect 164+), full rebuild
> `bash tools/build-micropython.sh --model=compact --keep-going`, and
> `tools/run-victor-sasi.sh build/mp-link/mpython.exe 220` — expect the trace to
> reach `D3 D4` and print `3`.
>
> MICROPYTHON TREE STATE (separate repo, ~/projects/micropython, uncommitted):
> `py/compile.c` was reverted CLEAN (the K0..K3/Z markers are gone).
> `ports/dos8086/main.c` gc_collect was restored to the real stack-scanning
> collector (it had been stubbed empty for size headroom during the bisection).
> The C/D phase markers in main.c's do_str remain.  Reproduce the bisection if
> needed by re-adding markers; the recipe is `tools/recompile-mp-tu.sh compile
> ~/projects/micropython/py/compile.c` then `tools/run-victor-sasi.sh`.  CAUTION:
> markers cost image bytes against the razor-thin ceiling — temporarily stub
> gc_collect (empty) to make room, as this session did.
>
> ALSO STILL OPEN (unchanged from §2h): Finding 3 — a raised exception object's
> far pointer loses its SEGMENT on the raise path; needed for any real exception
> to print, but `print(1+2)` raises none, so it does not block the milestone.

> **§2i (DONE 2026-06-01, committed `96553e4`) — the wide-arg→narrow-param
> blocker is FIXED; `mp_parse` now runs to completion on the real Victor.**
> DOS gate 158→**161**, `make check` green, 111 s/r 0 r/r (C-action only).
>
> FIX (the §2h Finding 2 "GENERAL/correct" option a, all in `minic/minic.y`):
> minic recorded only a function's RETURN type, so `emit_arg` sized every
> call argument by the argument's OWN type — a wide `l` (4-byte) value handed
> to a narrow `w` (2-byte) parameter was pushed 4 bytes where the callee reads
> 2, shifting every later stack arg.  Now:
>  - New `fnproto[]` table (open addressing, keyed by name) records each
>    function's fixed parameter types + count.
>  - `fnproto_record()` wired into every prototype/definition site:
>    `ansi_proto_register`, `ansi_func_proto`, `emit_knr_func{,_typed}`, the
>    `EXTERN type IDENT(par1);` proto, and the `dcls`-level local proto.
>  - `coerce_arg()` narrows/widens an integer-scalar arg to the declared
>    param width (`=w copy` / `extuw` / Con-aware `sext`); pointer/float/
>    aggregate args untouched.  Fires in the DIRECT-call emit loop, skipping
>    true-vararg args (index ≥ nparam).  Also fixes the symmetric long→int
>    shift latent on medium.
> Probe `argwiden_probe.c` (+golden), gated medium+compact+large; bug-loud
> `r FAIL 99` / `base FAIL 0` (the exact mp_parse_num_base symptom) without
> the fix, all pass with it.  ALSO: `tools/run-dos-exe.sh` now runs DOSBox
> headless (`SDL_VIDEODRIVER=dummy` + `SDL_AUDIODRIVER=dummy`) so gate runs
> open no windows / steal no focus.
>
> VERIFIED ON THE REAL VICTOR (clean compact far-data build, body 824064 B —
> just under the ~824.9KB wall, links 106/106 + --gc-sections): the trace
> advanced from `D0 D1 DE` (ValueError mid-parse) to **`D0 D1 D2 DE`** —
> `mp_parse` now returns a valid parse tree (D2), and the failure has MOVED
> into `mp_compile` (DE appears before D3).
>
> **THE NEW blocker — an uncaught exception during `mp_compile`.**  Trace:
> `C1 C2 C3 C4 D0 D1 D2 DE C5` (main.c driver in ~/projects/micropython;
> D2=parse done, D3 would be compile done, DE=do_str's nlr else branch).  So
> something raises between D2 and D3.  PRIME SUSPECT: the §2i fix coerces only
> DIRECT named calls — minic's function-pointer TYPES encode only the return
> type (`FUNC(rettype)`), no param list, so a wide→narrow arg shift through a
> FN-PTR call is STILL latent, and the compiler is fn-ptr-dispatch-heavy
> (emit_t method tables: `emit->method(...)`).  NEXT: (1) instrument
> `py/compile.c` (checkpoints around the compile passes / scope walk) +
> recompile via `tools/recompile-mp-tu.sh compile ~/projects/micropython/py/
> compile.c`, run `tools/run-victor-sasi.sh build/mp-link/mpython.exe 220`, and
> bisect WHERE in compile it raises and WHAT (use 16-bit-only debug printers —
> [[i8086-kl-shift-clobbers-ax]]).  (2) If it's a fn-ptr-call wide→narrow
> shift, extending coercion needs param types carried in the function-POINTER
> type (a bigger minic change — fn-ptr types currently only hold the return
> type).  (3) Finding 3 (exception object far pointer loses its SEGMENT on the
> raise path: offset kept, segment garbage) is STILL open and needed for any
> real exception to print — instrument the raise path
> (`mp_raise`→`nlr_raise`→`nlr_jump`) to pin the 16-bit truncation.  Build:
> `bash tools/build-micropython.sh --model=compact --keep-going`.
>
> ---
>
> # (prior) §2h diagnosis — wide-arg→narrow-param was diagnosed here (NOW FIXED in §2i above)
>
> The §2g "token-2 INVALID" was an INSTRUMENTATION ARTIFACT (the lexer is provably correct); the REAL blocker is a minic far-data ABI bug: a wide (`l`, 4-byte) argument passed to a narrow (`size_t`/`w`, 2-byte) prototype parameter is NOT narrowed, shifting all later stack args. This makes `mp_parse_num_integer` raise `ValueError("invalid syntax for integer")` on the INTEGER token `1`, aborting `mp_parse`. FIX minic to narrow wide args to the prototype's param width (or type pointer-difference as `ptrdiff_t`/int). A SECOND independent bug: the raised exception object's far pointer loses its SEGMENT on the raise path (offset kept, segment garbage) → `mp_obj_print_exception` crashes.

> **§2h (DIAGNOSIS ONLY — 2026-05-31, NO qbe-repo code change, nothing committed).
> All work was on-target bisection on the real Victor (SASI) + static SSA/asm
> reading. The MicroPython tree (~/projects/micropython, separate repo) has an
> uncommitted lexer-only→full-pipeline debug driver in ports/dos8086/main.c; the
> §2-era instrumentation in py/{lexer,gc,parse,qstr,runtime}.c is STASHED
> (`git stash list`: stash@{0}=lexer, stash@{1}=gc/parse/qstr/runtime) — pop if
> needed, but the CLEAN tree is what proved the lexer correct.**
>
> **FINDING 1 — the §2g "token-2 reads MP_TOKEN_INVALID" was an INSTRUMENTATION
> ARTIFACT, not a real bug.** With clean (un-instrumented) py/parse.c + py/qstr.c,
> the lexer tokenises `print(1+2)` PERFECTLY. Verified two ways on the Victor:
> (a) a lexer-only driver (do_str just calls mp_lexer_to_next in a loop) emits the
> full correct stream `T07 T51 T08 T3c T08 T52 T04 T00` (NAME `(` 1 `+` 2 `)` NEWLINE END);
> (b) under real mp_parse the first three tokens are `T07 T51 T08` (correct). The
> §2g debug prints in parse.c/qstr.c perturbed register allocation and TRIGGERED a
> latent clobber that mis-set `tok_kind`. LESSON: heavy instrumentation can CREATE
> codegen bugs here — keep probes minimal and re-test clean. (Also exhaustively
> verified statically that `tok_enc`, its far-pointer relocation, `is_char`'s
> `ceql`, and `chr0` are all correct — the lexer codegen is sound.)
>
> **FINDING 2 (THE REAL BLOCKER) — minic passes a WIDE arg to a NARROW prototype
> param without narrowing → stack-arg shift → ValueError in integer parsing.**
> `mp_parse` consumes `print` `(` `1`, then `push_result_token` for the INTEGER
> calls `mp_parse_num_integer("1",1,0,lex)`, which calls
> `mp_parse_num_base((const char*)str, top - str, &base)`. ROOT CAUSE (airtight,
> from the generated asm):
>   - Caller SSA: `call $mp_parse_num_base(l %t72, l %t80, l %base)` — `len` (=
>     `top - str`, a FAR-pointer difference) is typed `l` (4 bytes) and pushed as 4 bytes.
>   - Callee asm reads `str` at `[bp+6/8]` (4B), **`len` at `[bp+10]` (2B, it's
>     `size_t`)**, `base` at `[bp+12/14]` (4B). minic's `size_t`/`ptrdiff_t` are
>     `int`=2 bytes (minic/include/stddef.h).
>   - So the 4-byte `len` push shifts EVERY later arg by 2 bytes: the callee reads
>     `&base` from `[bp+12]/[bp+14]` = caller's `len`-high-word(0) + `base`-low-word
>     → a GARBAGE `base` pointer. `mp_parse_num_base`'s (correct) `*base=10`
>     `storefw` writes to that wild address; the caller's real `base` (SS:[bp-226])
>     stays **0**. Then `mp_parse_num_integer`'s digit loop `if (dig >= base) break`
>     is `1 >= 0` → breaks on the first digit → 0 chars parsed → `value_error` →
>     `ValueError("invalid syntax for integer")`.
>   - VERIFIED on-target (probes in mp_parse_num_integer): `str`=valid heap far ptr,
>     `len`=1, `*str`='1' (all inputs CORRECT), but `base`=**0** after
>     mp_parse_num_base (`R0000`), 0 chars parsed (`S0000`). And the created
>     exception type resolved to `mp_type_ValueError`.
> THE FIX (minic): coerce each CALL argument to the callee's declared parameter
> width. minic currently records ONLY the return type (`FUNC(rettype)`); it has NO
> per-function parameter-type table, and `emit_arg`/`eval_arg` (minic.y ~1997-2021)
> size each arg by the ARGUMENT'S own type via `irtyp_ret`. Two fix options:
>   (a) GENERAL/correct: record per-function param-type lists at every
>       prototype/definition (varadd sites ~5358/5397/6264) and, at the 3 call-emit
>       sites (minic.y 2079-2101 fnptr, 2129-2148 named, 2589-2638 inline expr —
>       all share `emit_arg`), narrow/widen each arg to the param class (reuse the
>       `=w copy` truncation idiom at ~3124 and `sext`/`extuw` at ~1490). Also fixes
>       the symmetric `long`→`int`-param shift that is latent on MEDIUM too.
>   (b) TARGETED: type a pointer-pointer DIFFERENCE as `ptrdiff_t` (=`int`/`w`, per
>       minic's stddef.h) instead of `l` in `expr()`'s `-` handling — then `top-str`
>       is a 2-byte `w` matching the `size_t` param. Smaller but only fixes the
>       ptr-diff case (the general wide→narrow shift stays latent).
> EITHER fix MUST be verified: rebuild minic, `make check` (expect 111 s/r 0 r/r),
> `tools/test-dos.sh` (gate ~158), then full MicroPython rebuild + `run-victor-sasi`.
> Fixing this may let `print(1+2)` parse (then compile/exec are the next frontiers).
>
> **FINDING 3 (SECONDARY, independent bug) — a raised exception OBJECT's far
> pointer loses its SEGMENT on the raise path.** `exc` (= nlr.ret_val) reads as
> `<garbage-seg>:0x0180` — the OFFSET is constant/correct but the SEGMENT is stale
> stack garbage (varied c5f9/c5fc/c5fd across runs). `nlr_jump`'s `top->ret_val =
> val` store is a correct `storefl` (4 bytes) and `do_str`'s read is a correct
> `loadfl`, so the segment is dropped UPSTREAM in the raise path
> (`mp_raise…`→`nlr_raise(exc)`→`nlr_jump(MP_OBJ_TO_PTR(exc))`): a far
> `void*`/`mp_obj_t` truncated to 16 bits somewhere. This is WHY
> `mp_obj_print_exception` crashes (the reboot-after-DE) and why the exception type
> read as garbage. Independent of Finding 2; needed for ANY real exception to
> propagate. Next session: instrument the raise path (print exc seg:off at each
> hop from creation to nlr_jump) to pin the truncation, then fix in minic/qbe.
>
> **ALSO learned:**
>  - minic gap: `T a = x, b = y;` (multi-declarator WITH initializers) parse-errors
>    (CLAUDE.md §1j noted it). Hit it in debug code; split into one decl per line.
>    MicroPython core compiles 106/106 without it, so it's not a port blocker.
>  - IMAGE SIZE WALL (precise): a body of ~823.4KB LOADS on the Victor; ~824.9KB+ →
>    "Program too big to fit in memory". Razor-thin (~824KB). Each debug print
>    costs; keep instrumentation tiny or strip working-phase probes (stashed).
>  - The lexer-only `do_str` does NOT shrink the image (the runtime stays reachable
>    via inline-helper / mp_state_ctx fixups; --gc-sections can't prune the
>    connected component). And dropping `mp_init()` crashes the lexer preload.
>    main.c is left with the FULL-pipeline driver + a minimal "DE\n" handler (the
>    real mp_obj_print_exception crashes under far-data — Finding 3).
>  - `gc_collect` is NOT involved (never runs — heap not full; verified with a marker).
>
> **REPRODUCE:** `bash tools/build-micropython.sh --model=compact --keep-going`
> then `tools/run-victor-sasi.sh build/mp-link/mpython.exe 190`. Fast inner loop:
> `tools/recompile-mp-tu.sh <base> <src.c>` (recompiles one TU + relinks; needs
> /tmp/mp_objs.txt). The C/D phase markers in ports/dos8086/main.c are in place.
>
> ---
>
> # (prior) Next session — THREE far-data fixes (§2f/§2g) cleared the file_input loop + qstr-intern hang; parser now consumes "print" and reaches token 2.  NEW blocker = the lexer's SECOND token reads MP_TOKEN_INVALID (1) instead of `(` (0x51) — a latent far-data lexer gap on token 2.  ALSO: the fully-instrumented image is now ~846KB → "too big to fit in memory" on the Victor; strip debug markers (or shrink) to get a loadable image.

> **§2f/§2g (DONE 2026-05-31, committed `014ee64` + `bb61947`) — THREE
> far-data fixes, found by on-target bisection on the real Victor.  DOS gate
> 154→158, `make check` green, 111 s/r 0 r/r.  MicroPython's `print(1+2)`
> parse advanced from an INFINITE LOOP (single_input→file_input→file_input_2→
> file_input_3 forever) all the way through "print" qstr interning and the
> first token, to the SECOND token.**
>
> Each fix has a probe (where a synthetic probe was authorable) and is
> bug-loud-verified.  Decode helpers for the on-target traces: token enum
> (MINIMUM rom level, FSTRINGS+ASYNC OFF): END=0, NEWLINE=4, INDENT=5, DEDENT=6,
> NAME=7, INTEGER=8, DEL_PAREN_OPEN=0x51, OP_PLUS=0x3c; rule-id decode from the
> compiled enum in `build/mp-link/parse.pp.c` (single_input=56=0x38).
>
> FIX 1 — **unsigned char widened to int SIGN-extended** (`minic.y`,
> model-INDEPENDENT).  Every char→int widening emitted `extsb`; a `uint8_t`
> with bit 7 set (0x8D) sign-extended to 0xFF8D (the byte already sits
> zero-extended in a `w` temp — loadub/loadfb clear the high byte — so
> sign-extending the low byte corrupts it).  Fix: emit `extub` when
> ISUNSIGNED(src) at the int=char assignment site + the three prom()
> char-promotion sites.  Canonical victim: py/parse.c `get_rule_arg()` reads a
> `const uint8_t` offset table; a 0x8D offset → 0xFF8D indexed the rule-arg
> table wild → `rule(stmt)`/`rule(simple_stmt)` resolved to rule 0 → the
> file_input loop.  Probe `uchar_widen_probe.c` (medium+compact); bug-loud
> "idx FAIL -115 / sum FAIL -227".
>
> FIX 2 — **pointer MEMBER of a far struct stored NEAR** (`minic.y`).  The
> assignment far-store fired only for ISFAR && KIND!=PTR/FUN, or a DIRECT
> global (FARSTORAGE).  A pointer member of a far struct reaches the store as a
> computed Tmp address (FARSTORAGE false) with a PTR value type (the ISFAR
> clause excludes PTR), so minic emitted a NEAR store (to the DGROUP shadow)
> while the member READ correctly used loadfar of the real far segment — value
> written never reached where the reader looked.  Fix: a storage-far
> side-channel `lval_storage_far`, set by `lval()` (member-of-far-struct /
> far-pointer-deref / far-global-variable) and OR'd into the store's far
> trigger — distinguishing "lives in far storage" from the value-FAR bit
> (which on a PTR means the VALUE is a far pointer in NEAR storage, e.g. a
> local far-pointer variable, which must stay a near store).  Additive: only
> adds far stores where storage is genuinely far.  Canonical victim:
> qstr_init's `MP_STATE_VM(last_pool) = &CONST_POOL` (mp_state_ctx is a far BSS
> struct, last_pool a pointer member) wrote near, qstr_find_strn read far → the
> qstr pool loop saw NULL.  Probe `farstruct_ptr_probe.c` (compact+large,
> far-static); bug-loud "p/next/q/wr FAIL".
>
> FIX 3 — **far-pointer DATA relocation lost its segment** (`tools/asm_to_omf.py`).
> A relocatable pointer initializer in static data was emitted by qbe as
> `.long _sym` → nasm `dd _sym` → OMF loc-9 32-bit OFFSET fixup, segment word
> left 0.  Under far-data a data pointer is a 4-byte seg:off far pointer, so the
> missing segment = wrong-segment deref.  Fix: under far-data, split a
> relocatable `.long _sym[+N]` into `dw _sym+N` (loc-1 offset) + `dw seg _sym`
> (loc-2 segment + runtime reloc); `omf_link` already resolves both.
> Canonical victim: the static qstr pools — each pool's `prev` (and
> `qstrs[]`/`lengths[]`) is a compile-time `&far_static`, so the pool->prev
> chain walked into GARBAGE pools (observed bogus lengths 0x652, 0x9a58, never
> terminating); after the fix the chain reads the real pools (lengths 31 then
> 183) and terminates at NULL.  NO synthetic gate probe — authoring one is
> blocked by ORTHOGONAL minic limits on far arrays-of-pointers (static-array
> element deref + far-pointer equality, both separate gaps); validated by the
> on-target qstr chain + the `.omf.asm` emitting `dw _sym / dw seg _sym`.
> CAVEAT: fix 3 ADDS a relocation per far-pointer data item (5158 relocs now),
> growing the MZ header ~3KB → re-tightens the Victor size wall.
>
> **THE NEW blocker — the lexer's SECOND token is MP_TOKEN_INVALID (1).**  On
> the v9 build (qstr patched) the on-target token trace is exactly `T07`
> (preload NAME "print") then, after the parser matches the NAME atom and calls
> mp_lexer_to_next, `T01` (= MP_TOKEN_INVALID) — it should be `T51`
> (DEL_PAREN_OPEN) for the `(`.  So the SECOND mp_lexer_to_next mis-tokenises
> `(` as INVALID.  The first token has been correct since §2e, so this is a
> latent far-data lexer gap on token N>1, only now reachable (the parser never
> got past token 1 before).  NEXT: instrument `py/lexer.c::mp_lexer_to_next`
> (next_char chr0/chr1/chr2 cache, the token-classification `tok_enc` table walk
> — those tables are far static data; also is_char/is_char_or etc.) to see what
> the lexer reads for `(` and where it decides INVALID.  Use 16-bit-only debug
> printers (no Kl shift — [[i8086-kl-shift-clobbers-ax]]).
>
> **FIRST, get a loadable image.**  The full rebuild + the debug instrumentation
> now in the micropython tree (qstr.c/lexer.c/parse.c/runtime.c/gc.c K*/F*/T*/
> rXX markers — UNCOMMITTED, separate repo) totals ~846KB and DOS reports
> "Program too big to fit in memory" on the Victor.  Strip the noisy markers
> (keep a minimal targeted set for the token-2 probe), and/or revisit the
> shrink levers (dead-strip is on; MICROPY_CONFIG trim, smaller link subset —
> see the §2b/§1z blocks).  Build: `bash tools/build-micropython.sh
> --model=compact --keep-going`; run: `tools/run-victor-sasi.sh
> build/mp-link/mpython.exe 240`.  The C/D/E/Q phase markers in main.c +
> lexer.c + runtime.c + gc.c are still in place.
>
> ---
>
> # (DONE §2e) mp_parse HANG CLEARED (two far-data codegen fixes); blocker was parser ends with non-END token (NOW understood: the file_input loop, fixed in §2f)

> **§2e (DONE 2026-05-31, committed `29e225a`) — the mp_parse HANG is FIXED.
> Two far-data codegen bugs, both with probes, DOS gate 150→154, `make check`
> green, 111 s/r 0 r/r (C-action edits only), amd64/arm64/rv64 byte-identical.**
>
> Found by on-target bisection on the real Victor via a NEW harness
> `tools/run-victor-sasi.sh` (the SASI hard-disk sibling of run-victor-mame.sh —
> boots `victor_python.img` partition 0, NO floppy, `-scsi:0 harddisk -hard1`;
> needed because mpython.exe is >612KB and a Victor floppy can't hold it).
>
> BUG 1 — **bitfield WRITE through a far address** (`minic.y` case `'='` on a
> `.` member).  The bitfield read-modify-write computed a far storage-unit
> address (`ptyp=IDIR_FAR`, `base_far` true under any far-data model) but then
> emitted a NEAR `load`/`store%c` — a near store of a far Kl address uses only
> the offset against DS, so the write hit the wrong segment and the bitfield
> value never reached its real (far) home.  The bitfield READ path already used
> `loadfar`; only the WRITE was wrong.  Now both use `loadfar`/`storefar` when
> `base_far`.  Canonical victim: py/parse.c's `rule_stack_t { size_t rule_id:8;
> … }` in the GC heap — `push_rule` wrote `rule_id` to the wrong segment,
> `pop_rule` always read 0, and `mp_parse` spun forever (rule stack never
> emptied → the observed hang).  Probe `bitfield_far_probe.c` (compact+large);
> bug-loud specifically on the HEAP-pointer case (a stack local's far segment
> happens to be DS-reachable so it masks the bug — the GC-heap case is the true
> exposure, matching the on-target hang).
>
> BUG 2 — **return-value type coercion missing** (`minic.y` `Ret`).  `return
> expr;` emitted `ret <x>` WITHOUT coercing `x` to the function's declared
> return type.  A narrow (INT/CHR) value returned from an `l` function reached
> `ret %tN` as a `w` temp; selret never widened it to DX:AX, so the function
> returned stale AX:DX.  py/lexer.c `next_char` does `mp_uint_t chr2 =
> lex->reader.readbyte(lex->reader.data)` where `mp_uint_t`==`uintptr_t` (32-bit
> under far-data) and `mp_reader_mem_readbyte` does `return *cur;` (byte→Kl):
> the byte was read CORRECTLY (cur/end/0x70 verified on-target inside readbyte)
> but the caller got ~`0x00000001`, which the lexer mapped to
> `MP_LEXER_INVALID_BYTE` (`'\1'`) → every source byte mis-tokenised → first
> token became 0x2f instead of NAME.  `Ret` now runs the assignment converter
> (LNG widen via `sext` / LNG narrow / float) against `curfntyp`.  Probe
> `fnptr_klret_probe.c` (compact+large); bug-loud (`c0 FAIL 3a80001`).  NOTE:
> the earlier passing `ret_byte` masked this because its EXPLICIT `(unsigned
> long)` cast already produced the widening; only the IMPLICIT return coercion
> was missing.  (While debugging, also re-confirmed the latent
> [[i8086-kl-shift-clobbers-ax]] — a 32-bit `>>=4` loop in a debug helper hung;
> avoid Kl shifts in on-target debug printers, use 16-bit word aliasing.)
>
> VERIFIED on the real Victor (clean build, instrumentation removed): the lexer
> now produces correct tokens and **`mp_parse` completes its rule loop in 11
> iterations with NO hang** (was: spun forever).
>
> **THE NEW blocker — parser ends with a NON-END token → `syntax_error`.**  On
> the clean build the trace reaches `D1` (enter mp_parse), the loop runs 11
> iters and exits normally, but at the end-of-parse check `lex->tok_kind` is
> `0x07` (NOT `MP_TOKEN_END`=0), so `lex->tok_kind != MP_TOKEN_END` is true and
> the `syntax_error:` path is taken (never reaches `D2`).  For `print(1+2)`
> SINGLE_INPUT, 11 rule iterations looks LOW — the parser likely stopped early
> (matched a shorter production / consumed too few tokens), leaving a token
> unconsumed.  So this is almost certainly ANOTHER far-data codegen gap, in
> either (a) the incremental `mp_lexer_to_next` calls DURING parse (a later
> token mis-read — the FIRST token is now correct, but token N may not be), or
> (b) the parser's token-match / push_result / result-stack logic.  NEXT:
> re-add the per-rule token trace (loop-top `pdbg_hex(rule_id)` + `t<tok_kind>`)
> AND a `mp_lexer_to_next`-site trace, recompile `parse` (+`lexer`) via
> `tools/recompile-mp-tu.sh`, run via `tools/run-victor-sasi.sh
> build/mp-link/mpython.exe 240`, and see WHICH token first goes wrong / where
> the parse stops short.  Use 16-bit-only debug printers (no Kl shift).  First
> determine the exact token enum values for THIS config (FSTRINGS/ASYNC may be
> on — check MICROPY_CONFIG_ROM_LEVEL) so 0x07/0x2f decode correctly.  Build:
> `bash tools/build-micropython.sh --model=compact --keep-going`.  The
> C/D/E/Q/A phase markers in main.c + lexer.c + runtime.c + gc.c (the
> ~/projects/micropython tree, NOT the qbe repo) are still in place.
>
> ---

# (DONE §2d) Next session — struct pass-BY-VALUE ABI landed; MicroPython lexer crash CLEARED on the real Victor; NEW blocker = a HANG in mp_parse (after the lexer, before parse completes)

> **§2d (DONE 2026-05-31) — minic now passes STRUCTS BY VALUE as arguments; the
> deterministic lexer reboot on the real Victor is FIXED.  MicroPython now runs
> through mp_init AND the whole lexer, into `mp_parse`.**  DOS gate **148→150**,
> `make check` green, 111 s/r 0 r/r (no grammar change — C-action edits only),
> amd64/arm64/rv64 byte-identical.
>
> ROOT CAUSE (found by serial-checkpoint bisection on-target, then reading the
> generated SSA): minic had struct *return*-by-value (§1d) but NOT struct
> *pass*-by-value.  A by-value aggregate ARGUMENT was emitted as a SINGLE scalar
> word (`%t20 =w loadw %reader; call ...(w %t20)`), and the callee declared a
> one-word param + stored only that word into its N-byte local — so every member
> past the first was uninitialised.  The canonical victim: the lexer's
> `mp_lexer_new(qstr, mp_reader_t)` takes a 12-byte `mp_reader_t` (far fn-ptr
> `readbyte` at offset 4 + far `data` + `close`) by value; only the first word
> arrived, the callee read a garbage far fn-ptr, and `next_char`'s first indirect
> `call far` jumped to nowhere → a hard reboot ~4 allocs into compiling
> `print(1+2)`.  (The "24KB heap exhaust" framing in the §2c note below was WRONG
> — there was an earlier deterministic crash; `gc_collect` never even ran because
> `gc_init` sets the alloc threshold to `(size_t)-1`, so collection only fires on
> heap-full, which `print(1+2)` never reached.)
>
> FIX — by-value aggregate crosses the call boundary as a POINTER to its storage
> (mirrors the §1d sret design; type-driven on both ends so it agrees across
> separate compilation).  All in `minic/minic.y`:
> - new helpers `is_aggr`, `eval_arg` (caller yields the aggregate's ADDRESS, not
>   a truncated load — re-derives via `lval` for V/@/. lvalues; C/I/L expr()
>   already yield a slot address), `emit_arg` (emits a struct arg as a
>   `DATAPTR_T()` pointer), `bind_param` (callee: alloc the local + `emit_struct_copy`
>   the pointed-to struct in; far loadfw/storefw under far-data).
> - wired into all 3 call-emission paths (direct, fn-ptr-var, indirect `(*fp)()`),
>   the ANSI function-def param signature + binding, and both K&R emit paths.
> Probe `structarg_probe.c` (+golden): struct-with-far-fn-ptr-member passed by
> value, callee calls through the member; C copy semantics (callee mutation
> doesn't touch caller); struct arg between scalars; pass-through; indirect call
> with a struct arg.  Gated **compact + large** (the far-data MicroPython target).
> VERIFIED on the real Victor: serial trace now shows `C1 C2 C3 Q0..Q3 C4 D0
> E0..E5 D1` (mp_init, then `mp_lexer_new` fully through `mp_lexer_to_next`,
> returning into `do_str`) with NO reboot — the lexer is sound.
>
> **MEDIUM is intentionally OMITTED from the probe** — it trips a SEPARATE,
> pre-existing backend bug: the callee's by-value copy is near `storew`s, which
> QBE load.c forwards + reconstructs into the member `loadl` (4-byte far code
> ptr) via Kl `shl/and/or`; on i8086 that reconstruction CLOBBERS a live value
> rega parked in AX (the call's `data` arg), returning garbage.  That is the
> [[i8086-kl-shift-clobbers-ax]] / [[qbe-loadc-wordsize-i8086]] family (Kl ops
> not declaring their AX/DX clobber to rega), independent of this ABI and not hit
> under far-data (which uses opaque `loadfw`/`storefw`, no slice reconstruction).
> Fixing it (mirror §2c's `Target.divclob`: add a Kl-arith clobber to rega's
> avoid mask for the reconstruction ops) would let the probe cover medium too.
>
> ALSO this session (in the ~/projects/micropython tree, a SEPARATE repo — NOT
> committed with the qbe change): `ports/dos8086/main.c` `gc_collect` is now a
> real conservative C-stack scan (`[sp, stack_top)`, scanned at BOTH even
> alignments since 8086 slots are 2-byte but a far ptr is 4 bytes; no register
> spill needed because the Kl-slot-resident invariant keeps every live 4-byte
> mp_obj_t in a stack slot; mp_state roots are already traced by
> `gc_collect_start`).  NOT yet exercised (the run hasn't reached heap-full).
> The heap STAYS 24KB: bumping it to 56KB made the image 863KB and DOS reported
> "Program too big to fit in memory" — the Victor leaves essentially no headroom
> over the ~831KB that loads, so a working gc_collect (not a bigger heap) is the
> reclaim path.  The gc.c per-alloc/`S:` serial checkpoints are now SILENCED
> (`GCK` no-op); the C*/D*/E*/Q* phase markers remain.
>
> **THE NEW blocker — a HANG in `mp_parse`.**  On-target the trace stops at `D1`
> (lexer done, entering `mp_parse`) and never reaches `D2` (parse done) in ~150s
> of 1.5×-speed emulation, with NO reboot (so not a crash/OOM — a hang or
> infinite loop).  `print(1+2)` parses trivially, so this is almost certainly
> another far-data codegen gap (likely another struct-by-value or far-ptr shape
> the parser hits — `mp_parse` threads `parser_t`/`mp_parse_node_t` structs).
> NEXT: add checkpoints inside `py/parse.c::mp_parse` (and `push_result_*` /
> `pop_result`), recompile via `tools/recompile-mp-tu.sh parse
> ~/projects/micropython/py/parse.c`, and bisect on the Victor (SASI recipe
> below).  Reproduce: full build `bash tools/build-micropython.sh --model=compact
> --keep-going`, inject onto `victor_python.img:0:\PROG.EXE`, run via MAME SASI
> (see the §2b block).  The C/D/E/Q checkpoints in main.c + runtime.c + lexer.c
> are still in place.

# (prior) Next session — MicroPython runs through mp_init() + into compiling print(1+2) on the real Victor; NEW blocker = 24KB heap exhausts during compile (gc_collect is a no-op stub)

> **§2c (DONE 2026-05-31, committed `d70c280`) — the gc_alloc hang is FIXED.  It
> was a 16-bit div/mul AX:DX register-clobber codegen bug, NOT a far-data pointer
> bug.  Verified ON THE REAL VICTOR: mpython now runs through `mp_init()` (C4) and
> deep into compiling `print(1+2)`, with gc_alloc succeeding repeatedly.**
>
> ROOT CAUSE: 8086 idiv/imul/div are fixed-register (dividend in AX, DX:AX the
> implicit pair, DX clobbered).  The i8086 backend emits them in-place instead of
> precoloring TMP(RAX)/TMP(RDX) in isel (amd64 does), so rega kept a value live
> ACROSS such an op in AX/DX and the next div/mul destroyed it.
> `gc_setup_area` computes `gc_alloc_table_byte_len = (24576-1)/(1+8/2*(4*4)) =
> 24575/65 = 378`, but the outer dividend (in AX) was zeroed by the inner
> idiv(8/2)/imul → came out **0** → 0 heap blocks → first gc_alloc finds nothing,
> returns NULL, caller retries forever → hang at `mp_init`'s first
> `mp_obj_dict_init`.  Localized with serial checkpoints (S: layout dump in
> gc_setup_area showed `total=0x6000 table=0x0000 pool=0x0000`; after the fix
> `table=0x17a=378 pool=0x5e8=1512`).  See [[feedback_i8086_div_divisor_axdx_clobber]].
>
> FIXES (DOS gate **147→148**, `make check` green, amd64/arm64/rv64 byte-identical):
> - **spill.c** + new `Target.divclob` field (i8086 = `BIT(RAX)|BIT(RDX)`, 0
>   elsewhere): for div/mul/rem, OR divclob into the live-across avoid mask,
>   mirroring caller-save-across-call, so rega keeps cross-living temps out of AX/DX.
> - **i8086/emit.c**: Kw idiv/div backstop — stage a divisor that still lands in
>   AX/DX into BX before the DX:AX setup (xchg-ax-bx subcase when dividend in BX).
> - **libstub.asm**: `__builtin_clzl` (32-bit CLZ), now referenced once uintptr_t
>   widened to 32-bit.
> - **build-micropython.sh / recompile-mp-tu.sh**: pass `-DFAR_DATA=1` to the C
>   preprocessor for far-data models.  `stdint.h` gates intptr_t/uintptr_t width on
>   `FAR_DATA`; without it `mp_uint_t`/`mp_obj_t` tagging was 16-bit and truncated
>   far pointers' segments (a real bug, but NOT the hang — the hang persisted after
>   this until the div fix).
> - **divreg_probe.c** (+golden, medium gate): nested div/mul in div operands;
>   bug-loud (`t1=0`) without the fix, `t1=378 t2=4607 t3=3 t4=-500 t5=0` with it.
>
> **THE NEW blocker — the 24KB heap fills during compilation of `print(1+2)` and
> `gc_collect` is still a NO-OP STUB (main.c), so nothing is reclaimed → a gc_alloc
> eventually returns NULL → the program crashes/reboots (a second `__V9BEGIN__`
> appears in the serial capture).**  The on-target capture shows: `C4` (mp_init
> done), then dozens of successful `A0 A1 A2 Af Am Ar` gc_alloc cycles as the
> lexer/parser/compiler allocate, then an `A0 A1 A2 A2` (gc_collect retry → still
> NULL), then a reboot — never reaching `C5`/`3`/`__V9END__`.  **NEXT MOVES (pick):**
> (1) **bump `MICROPY_HEAP_SIZE`** in ports/dos8086/mpconfigport.h (it's 24KB; the
> far-data model leaves lots of conventional RAM — try 64–128KB) as the cheap first
> test; (2) **implement a real `gc_collect`** (stack scan; now that setjmp/longjmp
> work it can spill callee-saved regs and scan [SP, stack_top) — see
> [[minic-setjmp-longjmp]]); (3) confirm whether the reboot is pure OOM or a
> separate crash (add an OOM checkpoint in m_malloc_fail / gc_alloc's NULL return).
> The serial checkpoints (C*/Q*/S:/A* in main.c, runtime.c, gc.c — in the
> ~/projects/micropython tree, NOT the qbe repo) are STILL IN PLACE; strip the
> noisy gc.c A*/S: ones once the heap path is solid (they print per-alloc).
> SASI harness recipe unchanged (below).  Reproduce: `bash
> tools/build-micropython.sh --model=compact --keep-going` then inject + run via
> the SASI disk (see the §2b block).
>
> ---
>
> # (DONE §2b) — `--gc-sections` dead-strip + first on-target execution of MicroPython
>
> **What landed (committed `70e9c8a`): `omf_link.py --gc-sections`** — segment-
> granular dead-code elimination via FIXUPP reachability from `_start` (standard
> linker --gc-sections model).  BFS the fixup graph (each fixup's TARGET + FRAME —
> segment / group-members / external-symbol→defining-segment — marks its segment
> live) to a fixpoint; drop segments nothing reachable points at.  Sound here
> because every cross-segment dependency (call, data-table fn-ptr, `seg sym`
> selector) is an OMF fixup; the only hand-asm (crt0/libstub) uses nasm fixups too.
> Opt-in flag, default OFF → gate byte-identical (DOS pipeline **147/147**, `make
> check` green, a no-flag MicroPython relink is byte-for-byte identical to the
> prior mpython.exe).  `build-micropython.sh` passes it.  **Result: 208 segments
> stripped, CODE 861KB→675KB, image 971KB→781KB (footprint 928.7KB→745.9KB) — now
> UNDER the ~896KB Victor ceiling.**  Correctness is structurally proven: the link
> succeeds, and `_apply_fixups`/`_resolve_target` hard-index `seg_map`, so any
> kept→stripped reference would KeyError-crash the link.
>
> **THE SASI HARNESS WORKFLOW (reusable — this is how to run a >612KB .EXE on the
> Victor; the floppy can't hold it and `vtg_image_util` caps single-file floppy
> writes ~440KB).**  Use the dedicated bootable SASI hard disk
> `~/projects/qbe/victor_python.img` (Victor 9000 Hard Disk, FAT12, partition 0 =
> boot drive A:/C:, ~9.5MB free; the user fixed a vtg bug so partition 0 is now
> WRITABLE).  Boot params: `-scsi:0 harddisk -hard1 <img>` and **NO `-flop1`** (with
> a floppy present the Victor always prefers floppy boot).  Inject onto partition 0
> with `vtg_image_util copy <exe> <img>:0:\\PROG.EXE` + an `AUTOEXEC.BAT` (`echo off`
> / `portset a 9600 none 1 8` / `ctty seriala` / `echo __V9BEGIN__` / `prog` / `echo
> __V9END__`).  Capture serial via `-rs232a null_modem -bitbanger <cap>`, ~200-240s
> (boot ~50s).  **Verified end-to-end: boot → AUTOEXEC → serial → mpython LOADS and
> runs on real hardware.**  (TODO: fold a SASI path into `tools/run-victor-mame.sh`
> + a `VICTOR_TESTS` entry once `print(1+2)` actually works.)
>
> **THE next blocker — `gc_alloc` hangs (mp_init's FIRST heap allocation).**  With
> serial checkpoints added to `main.c` (C1..C5) and `py/runtime.c::mp_init` (Q0..Q3),
> the on-target capture is exactly: `__V9BEGIN__ C1 C2 C3 Q0 Q1 Q2` — then NOTHING.
> So: `main()` entered (C1), stack set (C2), `gc_init()` returned (C3), `qstr_init()`
> returned (Q1), reached just before the first `mp_obj_dict_init` (Q2) — and
> **`mp_obj_dict_init(&mp_loaded_modules_dict, …)` never returns (no Q3)**.  That
> call is `mp_obj_dict_init` → `m_malloc` → **`gc_alloc` (py/gc.c)**.  Hypothesis:
> a far-data pointer-arith/comparison bug in gc.c's block/ATB scan (heap is a far
> `static char heap[24576]` in its own FAR_DATA segment; `gc_init(heap, heap+sizeof)`
> does far-ptr arithmetic; gc_alloc scans the heap+alloc-table with far pointers) —
> likely an infinite loop in the free-block finder, OR `gc_init` set the pool/ATB
> pointers wrong so the scan never terminates.  **NEXT: add checkpoints inside
> `gc_alloc`/`gc_init` (py/gc.c), recompile that one TU via
> `tools/recompile-mp-tu.sh gc ~/projects/micropython/py/gc.c` (the in-place
> per-TU recompile+relink helper committed this session — compact far-data,
> --gc-sections; needs `/tmp/mp_objs.txt` = the link object list, regenerated by a
> full build-micropython run or the glob in this note), and bisect.**  The
> checkpoints in main.c + runtime.c are STILL IN PLACE for this.  This is a real
> QBE-toolchain far-data gap to FIND AND FIX (the project principle), not a
> dead-strip regression (closure proof above).
>
> Build/run recipe used this session (reproduce):
> `bash tools/build-micropython.sh --model=compact --keep-going` (full), or
> `tools/recompile-mp-tu.sh <base> <src.c>` (one TU, fast), then the SASI harness
> above.  NOTE: the checkpoint helper reads `/tmp/mp_objs.txt` (the ordered link
> object list: crt0_exe.obj + every built TU .obj + libstub_exe.obj); regenerate
> it with the same source glob `build-micropython.sh` uses if it's been cleared.

---

# (DONE §2b) Original §2a-followup — SHRINK mpython.exe under the ~896KB Victor ceiling, then run print(1+2) on-target via the new MAME harness

> **§2a (DONE 2026-05-30) — the MAME Victor 9000 headless harness is BUILT,
> VALIDATED, and GATED.**  DOSBox emulates a 640KB IBM PC (wrong machine); the
> on-target / >640KB path now runs under MAME machine `victor9k` (~896KB RAM).
> New, all committed:
> - **`tools/run-victor-mame.sh`** — the `victor9k` analog of `run-dos-exe.sh`.
>   Takes a built `.EXE` + optional `seconds_to_run` (default 90); `cp`s the
>   base `python.img` to a scratch `run.img`; injects the EXE as `PROG.EXE` +
>   a generated `AUTOEXEC.BAT` via `vtg_image_util copy`; runs MAME headless
>   (`SDL_VIDEODRIVER=dummy mame victor9k -ramsize 896K -flop1 run.img -video
>   none -sound none -nothrottle -skip_gameinfo -seconds_to_run N -rs232a
>   null_modem -bitbanger <cap>`, in a `-homepath` sandbox so it never touches
>   the user's `~/.mame`); streams the serial capture (CR/0x1A-stripped,
>   trimmed between `__V9BEGIN__`/`__V9END__` sentinels) to stdout.  **Exit 77
>   (skip-not-fail)** if MAME / its roms / `$VICTOR_DISK` / `vtg_image_util` is
>   missing.  Env overrides: `$VICTOR_DISK` (default `~/Desktop/randos/
>   python.img`), `$MAME`, `$MAME_ROMS`, `$VTG_IMAGE_UTIL`, `$VICTOR_RUN_SECS`.
> - **`tools/test-victor.sh`** — a SEPARATE gate (each MAME run boots MS-DOS
>   3.1 from floppy, ~45-60s wall-clock, so it's kept off the fast DOSBox
>   gate).  `VICTOR_TESTS` array (`<src>:<golden>:<model>`), same run/skip/diff
>   shape as `test-dos.sh`.  Currently 1 entry: `cprobe` (the harness smoke
>   test).  **Passes 2/2 (build + cprobe).**
> - **`minic/dos/tests/cprobe.golden.txt`** — golden for the smoke test.
>
> **The TWO gotchas found and fixed (so you don't re-hit them):**
> 1. **`flop1` not `hard1`** — the base is a bootable FLOPPY; `-flop1 run.img`
>    (myfreedos's `-hard1` was for a hard-disk image).
> 2. **`echo off`, NOT `@echo off`** — MS-DOS 3.1 predates the `@` line prefix,
>    so `@echo off` is parsed as an unknown `@echo` command and leaves echo ON,
>    which leaks the `A:\>PROG.EXE` prompt+command into the serial capture.
>    Bare `echo off` gives clean program-only output.
>
> Confirmed working: `tools/run-victor-mame.sh build/examples/cprobe/cprobe.exe`
> → exactly `x=42 (want 42)\nx=99 (want 99)`, stable across repeated runs.  The
> serial-capture path (`ctty seriala` → MAME `-bitbanger`) is proven end-to-end
> independent of mpython's size.  No minic/qbe/backend change this session —
> just three new files — so the DOSBox gate + `make check` are unaffected.
>
> **THE NEXT MOVE — shrink `mpython.exe` (footprint 928.7KB, ~33KB over the
> ~896KB Victor ceiling) so it loads, then run `print(1+2)` → `3` on the Victor
> via this harness** (add it as a `VICTOR_TESTS` entry).  Shrink levers, rough
> payoff order: (1) **dead-strip unreferenced functions/segments in
> `omf_link`** (reachability from `_start`/`_main` through PUBDEF/EXTDEF/FIXUPP;
> now that big TUs split per-function-group the granularity is finer — biggest
> lever, `print(1+2)` touches maybe 10-20% of the linked core); (2) **trim
> `MICROPY_CONFIG`** (fewer builtins/modules, smaller qstr set); (3) **curate a
> smaller link subset**.  See the §1y/§1z block below + [[project-victor9000-target]],
> [[project-minic-far-setjmp-and-size-wall]].

---

# (DONE §2a) Original spec — BUILD A MAME VICTOR 9000 HEADLESS TEST HARNESS (kept for reference)

> **WHY THIS WAS THE NEXT SESSION (user direction 2026-05-30):** the real target
> is the **Victor 9000 / Sirius 1** (~896KB RAM, non-IBM memory map), NOT the
> 640KB IBM PC that `tools/run-dos-exe.sh` drives under DOSBox.  DOSBox was only
> ever a convenient stand-in; it emulates the WRONG machine, so it can neither
> load a >640KB image nor exercise Victor-specific hardware.  Before we chase
> the mpython.exe shrink (or anything else "does it run on target"), we need a
> **MAME-based `victor9k` harness** that runs a built program headlessly and
> captures its output for golden-diff assertions — the same way
> `~/projects/myfreedos` and `~/projects/newlibc` already test on this platform.
> Build the harness FIRST, validate it with a TINY program, then point the gate
> (or a new victor-gate) at it.  See [[project-victor9000-target]].
>
> **THE PROVEN PATTERN (reverse-engineered from myfreedos + newlibc — reuse it,
> don't reinvent):**
> - **MAME**: binary at `~/projects/mame/mame`, machine `victor9k`, `-ramsize 896K`.
> - **Headless flags**: `-video none -sound none -nothrottle -skip_gameinfo
>   -seconds_to_run <N>` with `SDL_VIDEODRIVER=dummy` in the env.  (`-rompath
>   ~/projects/mame/roms` if roms aren't on the default path.)
> - **Output capture — TWO mechanisms, pick the serial one for line-oriented
>   stdout:**
>   1. **Serial → host file** (simplest; this is what `myfreedos/boot/victor/
>      test_mame.sh` uses): `-rs232a null_modem -bitbanger /tmp/cap.txt`.  The
>      `AUTOEXEC.BAT` does (EXACT sequence, user-confirmed) `portset a 9600 none
>      1 8` then `ctty seriala`, redirecting DOS CON (handle 1 = stdout) to
>      serial port A.  **9600 is the CEILING — MAME's serial timing breaks above
>      it** (the image's CONFIG.SYS defaults porta to 1200; `portset` bumps it to
>      9600 at runtime).  Our qbe programs already write stdout via INT 21h
>      AH=40h to handle 1, so `ctty seriala` routes that to `/tmp/cap.txt` with
>      no program change.  `portset` syntax:
>      `PORTSET <A|B> <baud> <parity> <stopbits> <bits>`.
>   2. **Screen-RAM dump** (alternative; `newlibc/phase3_newlib/run_test.sh`
>      `--auto`): a MAME `-autoboot_script` Lua dumps screen RAM at `0xF0000`
>      (4000 B = 80×25×2), decoded by `phase3_newlib/tools/decode_victor_screen.py`
>      (char glyph = low byte − 0x60).  Heavier; only needed if we test the
>      Victor text screen directly rather than stdout.
> - **Boot/program disk (USE THE STABLE MS-DOS 3.1 FLOPPY, not myfreedos):** the
>   base is `~/Desktop/randos/python.img` — a bootable **Victor MS-DOS 3.1
>   single-sided floppy** (MSDOS.SYS + COMMAND.COM + the PORTA/PORTB/PORTSET
>   serial utils; CONFIG.SYS already `device=porta.exe`/`portb.exe`).  Chosen
>   over the myfreedos FreeDOS image because myfreedos is itself under test — MS
>   DOS 3.1 is the stable reference OS.  Mount it in MAME as a **floppy**:
>   `-flop1 <img>` (NOT `-hard1`).
> - **File injection — `vtg_image_util` (on PATH; CONFIRMED working):** it
>   reads/writes Victor FAT12 floppies.  `vtg_image_util copy <localfile>
>   <img>:\\NAME.EXT` writes (OVERWRITES an existing name; there is no `-f` for
>   copy — that flag is `create`-only).  `copy <img>:\\NAME .` reads back;
>   `list`/`info`/`delete` as expected.  **ALWAYS operate on a COPY of
>   python.img — never mutate the user's master.**  So the harness: `cp
>   python.img run.img`; `vtg_image_util copy <prog>.exe run.img:\\PROG.EXE`;
>   write an `AUTOEXEC.BAT` (`@echo off` / `portset a 9600 none 1 8` /
>   `ctty seriala` / `PROG.EXE` / a sentinel echo) and `vtg_image_util copy
>   AUTOEXEC.BAT run.img:\\AUTOEXEC.BAT`.
> - **Exit detection**: fixed `-seconds_to_run` (myfreedos uses 150; tune down),
>   or a `PASS:`/`FAIL:` sentinel regex over the captured output.
> - **Interactive debugging (NOT for the gate, but invaluable when a run
>   misbehaves)**: the **`mame-victor-test` skill** + MCP server at
>   `~/projects/Victor9000-Development-Private/mame/mame-mcp-server/` exposes
>   `mame_read_screen_text`, `mame_read_memory`, `mame_get_registers`,
>   breakpoints, single-step, etc.  myfreedos's CLAUDE.md marks it MANDATORY for
>   ad-hoc MAME work ("DO NOT run MAME directly" — for interactive sessions).
>
> **CONCRETE DELIVERABLES for the session:**
> 1. **`tools/run-victor-mame.sh`** — the `victor9k` analog of `run-dos-exe.sh`:
>    takes a built `.EXE`, `cp`s the base `python.img` to a scratch `run.img`,
>    injects the EXE + a generated `AUTOEXEC.BAT` (`@echo off` /
>    `portset a 9600 none 1 8` / `ctty seriala` / `PROG.EXE` / a sentinel echo
>    so we know it finished) via `vtg_image_util copy`, runs MAME headless
>    (`SDL_VIDEODRIVER=dummy mame victor9k -ramsize 896K -flop1 run.img
>    -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run <N>
>    -rs232a null_modem -bitbanger <cap>`), then streams the serial capture
>    (CRLF/0x1A-stripped, à la `run-dos-exe.sh`) to stdout, trimmed to between
>    the sentinel markers.  Exit 77 (skip) if `mame`, its roms, or
>    `$VICTOR_DISK` are missing — so the gate degrades gracefully on machines
>    without the Victor MAME setup.  Base image path overridable by env
>    `$VICTOR_DISK` (default `~/Desktop/randos/python.img`), mirroring `$DOSBOX`.
> 2. **File injection uses `vtg_image_util` directly** — no custom FAT writer
>    needed (it already does Victor FAT12 read/write).  Just `cp` the master to
>    scratch and `vtg_image_util copy` the EXE + AUTOEXEC.BAT in.  The one open
>    question to settle empirically on the first MAME run: confirm the serial
>    capture actually fills (porta = MAME `-rs232a` port A; baud held at 9600).
> 3. **VALIDATE THE HARNESS WITH A TINY PROGRAM FIRST** — e.g. the existing
>    far-data "halprobe" that prints `3`, or a 1-line hello `.EXE`.  This proves
>    the serial-capture path end-to-end INDEPENDENT of mpython's size, and
>    becomes the harness's own smoke test / golden.  (mpython.exe is 928.7KB —
>    ~33KB over 896KB — so it still needs the shrink before IT runs; do that
>    AFTER the harness exists, in a later session.)
> 4. **Gate wiring** — add a `victor` runtime path to `tools/test-dos.sh` (a new
>    RUNTIME-style array gated on `$VICTOR_DISK`/`mame` being present), or a
>    sibling `tools/test-victor.sh`.  Keep the DOSBox path for the small
>    near/far probes it already validates (it's faster and needs no Victor
>    image); use MAME for the on-target / >640KB / Victor-hardware cases.
>
> **RESOURCES (paths):** MAME `~/projects/mame/mame` (machine `victor9k`); **base
> boot floppy `~/Desktop/randos/python.img` (Victor MS-DOS 3.1)**; **file-inject
> tool `vtg_image_util` (on PATH)** — `copy`/`list`/`info`/`delete` Victor FAT12;
> harness exemplars `~/projects/myfreedos/boot/victor/test_mame.sh` (the serial
> `-bitbanger` pattern) + `~/projects/newlibc/phase3_newlib/{run_test.sh,
> tools/decode_victor_screen.py}` + `~/projects/newlibc/MAME_DEBUG_GUIDE.md`;
> MCP/skill
> `~/projects/Victor9000-Development-Private/mame/mame-mcp-server/` (`mame-victor-test`);
> Victor HW docs `~/Documents/Victor9k Stuff/Manuals/{subsystem-docs,GPTFiles}`;
> full Victor FreeDOS port `~/projects/myfreedos`; OEM **MS-DOS 3.1 sources**
> `~/projects/myfreedos/Victor Vintage Software/MS-DOS 3.1 Sources`.  There is a
> `victor9000-engineer` agent for Victor hardware/MS-DOS-internals questions.
>
> **STILL DEFERRED (user):** `~/projects/newlibc` (the real Victor-targeted libc)
> integrates at a LATER stage — keep the libstub path for now.  The mpython
> shrink (omf_link dead-strip / MICROPY_CONFIG trim) is the move AFTER the
> harness exists.  See [[project-victor9000-target]],
> [[project-minic-far-setjmp-and-size-wall]].

# (DONE §1y/§1z) MicroPython LINKS under compact far-data; image size is the wall (now measured vs the ~896KB Victor ceiling, not 640KB)

> **§1y (commit `76c69eb`) — FAR_SETJMP_EXE: far-data setjmp/longjmp.**  New
> `_far_setjmp`/`_far_longjmp` in `tools/libstub_to_exe.py` (4-byte far env
> ptr via ES:BX; longjmp's `val` at `[bp+10]`).  Resume-SP arithmetic is
> IDENTICAL to the medium SETJMP_EXE (`lea [bp+6]`) — args sit above the
> 4-byte CS:IP return address regardless of width.  Appended only under
> far-data models.  `minic.y`: `setjmp`/`longjmp` added to `far_stdlib[]` →
> mangled to `_far_setjmp`/`_far_longjmp` under compact/large/huge.
> `setjmp_probe` now gated medium + compact + large (full NLR round-trip,
> byte-identical golden across all three).  Gate 145→**147**, `make check`
> green, 111 s/r 0 r/r (no grammar change).
>
> **§1z — MicroPython LINKS under `--model=compact` far-data, and the
> remaining blocker is IMAGE SIZE, not the toolchain.**  `tools/build-
> micropython.sh --model=compact` now: compiles 106/106 TUs (0 fail) with
> `--far-static-data` + `-DFAR_DATA`/`-DDOS_FAR_DATA`, then LINKS to
> `build/mp-link/mpython.exe` (108 modules; 861KB far code; 43KB far data
> OUTSIDE DGROUP; only 37KB in DGROUP — the §1r DGROUP-overflow hang is GONE).
> Two real gaps fixed to get there:
> 1. **>64KB CODE segment** (`compile.obj` was 78KB — far-data codegen ~2x's
>    code size, pushing MicroPython's biggest TU past the 64KB real-mode
>    segment cap).  nasm emitted a `SEGDEF2` (32-bit, 4-byte length) +
>    `LEDATA32`; `omf_link._handle_segdef` always read a 2-byte length →
>    misparse → "bad LNAMES index 0".  FIX (two parts): (a) `asm_to_omf.py`
>    now SPLITS a TU's `.text` across multiple `<BASE>_TEXT`/`_TEXT1`/`_TEXT2`
>    CODE segments at FUNCTION boundaries (qbe emits a `.text` directive
>    before every function) when the estimated size exceeds `TEXT_SEG_BUDGET`
>    (56KB; `est_line_bytes`≈4×, ~2x margin over the measured ~2.1 B/line).
>    Far calls resolve cross-segment via symbol fixups (already how
>    cross-module calls work) and each function stays wholly in one segment so
>    intra-function near jumps remain segment-local.  `omf_link` places every
>    CODE-class segment distinctly (`_place_distinct`), so N per module just
>    works.  (b) `omf_link._handle_segdef` now reads the 4-byte length for
>    `SEGDEF2` and HARD-REJECTS any USE16 segment >64KB with a clear message
>    (defensive: a real-mode segment can't exceed 64KB).  Single-segment TUs
>    are byte-identical; no probe is big enough to split.
> 2. **`mphalport.c` console HAL was medium-only** (near-data ABI: `str` at
>    `[bp+6]` near, `len` at `[bp+8]`, INT 21h via DS:DX).  Under far-data
>    `str` is a 4-byte far ptr (`[bp+6]`/`[bp+8]`), `len` at `[bp+10]`, and the
>    buffer is OUTSIDE DGROUP so DS must be set to `str.seg`.  Made it
>    `#if DOS_FAR_DATA` (build-micropython passes `-DDOS_FAR_DATA=1` under far
>    models).  **VERIFIED CORRECT** by a standalone far-data probe that prints
>    "3" — so the console path works; the HAL is NOT the blocker.
>
> **THE remaining blocker — IMAGE SIZE (but only modestly over).**  TARGET IS
> THE **VICTOR 9000 / Sirius 1**, which has up to **~896KB** of contiguous
> conventional RAM (NOT the IBM-PC 640KB — its non-IBM memory map allows more).
> `mpython.exe`'s loaded footprint is **928.7KB** (body 951024 B, minalloc 0)
> → only **~33KB over the 896KB raw ceiling** (more once DOS + PSP + heap/stack
> headroom is counted, but the same order of magnitude — NOT the wild overage a
> 640KB ceiling would imply).  This is NOT a codegen/link defect — we link the
> WHOLE curated core (omf_link has no dead-code elimination) while `print(1+2)`
> touches a small fraction.  **Next-move options (rough payoff order):**
> 1. **Dead-strip unreferenced functions/segments in `omf_link`** — mark from
>    `_start`/`_main` through PUBDEF/EXTDEF/FIXUPP reachability, drop unreached
>    CODE segments.  Now that big TUs split per-~function-group the granularity
>    is finer; biggest lever (print(1+2) needs maybe 10-20% of the core, so this
>    likely shaves FAR more than the ~33-100KB needed).
> 2. **Shrink `MICROPY_CONFIG`** — fewer builtins/modules, smaller qstr set,
>    trim the compiler.  Even a small trim likely closes a 33KB gap.
> 3. **Curate a smaller link subset** — only modules transitively needed for
>    lexer→parse→compile→`mp_call_function_0`+print.
>
> **TEST-ENVIRONMENT CAVEAT:** the `tools/run-dos-exe.sh` / DOSBox path emulates
> a 640KB IBM PC, so a >640KB image won't load THERE regardless of the Victor
> ceiling.  Validating `print(1+2)` on the real target needs a Victor 9000
> emulator/hardware path (there is a `victor9000-engineer` agent + a Victor
> codebase in `~/projects/newlibc`).  Small far-data probes (setjmp_probe,
> halprobe printing "3") DO run under DOSBox and prove the toolchain end-to-end.
>
> **PLANNED LIBC (deferred, user direction):** `~/projects/newlibc` is a real
> Victor-9000-targeted C library; the plan is to integrate it at a LATER stage
> (it replaces the current libstub.asm / minic/include stopgaps).  Do NOT wire
> it in yet — the current libstub path is the bring-up vehicle.
> See [[project-minic-far-setjmp-and-size-wall]], [[minic-far-data-segment]].

# (DONE §1y/§1z) Prior next-session note — toolchain gaps: long const/struct + huge ptrdiff FIXED (§1w/§1x); MicroPython far-data compiles clean (§1v); next = FAR_SETJMP_EXE then link far

> **PRINCIPLE (reaffirmed): the goal is to FIND AND FIX QBE-toolchain gaps;
> running MicroPython is the vehicle, not the prize.  When a probe trips a real
> codegen defect, FIX THE GAP — don't scope the probe around it.**
>
> **§1w (commit `0eec5f4`) — three model-INDEPENDENT `long` truncation gaps**
> (i8086 `int` is 16-bit; bit under medium too):
> 1. `minic.y sext()` sign-extended a COMPILE-TIME CONSTANT via `=l extsw`,
>    which on i8086 keeps only the low 16 bits → `long x = 555666L` became
>    31250 (and bit-15-set 40000 went negative).  Fix: retype the Con LNG
>    directly (its full value is already known), no extsw.
> 2. Integer literals were always typed INT and the L/l suffix discarded, so a
>    `long` literal > 16 bits passed to an `l` parameter or a `%ld` vararg went
>    out as a 16-bit word.  Fix: new `Node.nlong` (lexer sets it on L/l suffix
>    or value > 0xFFFF), `'N'` case types it LNG.
> 3. `load.c def()` reconstructing a 4-byte slice (a `loadl` from two 2-byte
>    `storew`s — struct-copy of a returned `long` member) used class Kw because
>    the width test hardcoded `sl.sz > 4`; `high << 16` then shifted a 16-bit
>    temp to 0 and lost the high word.  Fix: `sl.sz > T.wordsz`
>    (**target-general**; no-op on amd64/arm64/rv64 where wordsz==4).
> Probe `longconst_probe.c`.  Closes [[qbe-loadc-wordsize-i8086]] residual,
> the struct-return-long limit, and [[minic-long-literal-as-int-vararg]].
>
> **§1x (commit `ef870bd`) — huge ptr-MINUS-ptr.**  Two normalised far pointers
> into one object can sit in different segments, so a flat 32-bit `sub` of their
> seg:off words gave (Δseg<<16)+Δoff not the true Δseg*16+Δoff
> (`&a[20]-&a[3]` ≠ 17).  Routed MHuge ptr-ptr through the existing-but-dead
> `_qbe_huge_cmp` helper (returns signed linear(p)-linear(q)); the element-size
> `div` post-step still scales it, so int*/long* diffs are correct too.  Flat
> sub stays for compact/large/near; huge comparison stays flat (normalisation
> makes seg:off monotonic).  `farlocal_probe` now covers huge as well.
> FOOTGUN hit: `int*/long*` in a minic.y action-body comment closes the block
> comment (`*/`).  See [[long-and-huge-ptrdiff-gaps]].
>
> `make check` green throughout; compact MicroPython sweep stayed 106/106; DOS
> gate 142→**145**.

# Next session — far-data MicroPython core COMPILES clean (compact+large); next = FAR_SETJMP_EXE then link under far placement (post §1v)

> **§1v — the "27 far-data TU compile fails" are CLEARED (commit `75bf7d0`).**
> The curated MicroPython core now compiles **106/106 TUs** minic→qbe(-t i8086)
> under `-m compact` AND `-m large`, 0 fail (was ok=79 / minicfail=2 /
> qbefail=25).  Sweep harness: `bash build/mp-far-probe/sweep.sh compact`
> (and `large`) — re-run to reproduce.  `make check` green, DOS gate **140→142**,
> 111 s/r 0 r/r (no grammar change — fix #3 only edits action bodies).
>
> Three independent root causes, three fixes (all far-data; NEAR_DATA models
> byte-identical for #2/#3; #1 model-independent):
> 1. **i8086/isel.c** — the fast-alloc slot loop scanned only `fn->start`, so a
>    constant-size `alloc4` in a NON-entry block (a block-scoped local declared
>    inside a loop/if body — py/bc.c's `mp_bytecode_get_source_line` lineinfo
>    buffer is the canonical case) reached emit as `Oalloc4 cls Kl` and died
>    ("unsupported 32-bit op 81 (cls Kl)").  Now slots constant allocs in EVERY
>    block (C block-scoped locals reuse one frame slot; real `alloca` is routed
>    to the GC heap by `MICROPY_NO_ALLOCA`, so no dynamic alloc survives — the
>    simple fixed-slot fix beats amd64's salloc/Osalloc dynamic path and dodges
>    far-pointer-to-SS:sp).  Cleared 23 TUs.
> 2. **minic.y** — member-base address of a LOCAL aggregate under far-data
>    emitted `=w add %localKl, off`, truncating the Kl slot address
>    (`ALLOC_T()` is 'l'); the following far `loadfX` then read the wrong place,
>    and the const-fold case tripped gvn `assoccon`'s `KWIDE` assert
>    (parse.c, compile.c).  `base_far` now includes `|| !NEAR_DATA()` at all
>    three member-address sites (expr read, bitfield store, lval addr) — under
>    far-data every object address is a far Kl pointer.
> 3. **minic.y** — the `type '*'` declarator rule did `IDIR_FAR($1 & ~FAR)`,
>    stripping the pointee's FAR bit, so `char **` was built as far-ptr-to-NEAR-
>    char*.  `*pp` then came out near, making `q - *pp` a near-vs-far
>    "non-homogeneous pointers in subtraction" error (bc.c, objint.c) and a
>    silent miscompile elsewhere.  Fix: keep `$1`'s FAR (`IDIR_FAR($1)` —
>    IDIR_FAR shifts it to the inner-far position, bit 27).  3 sites
>    (`*`, `* CONST`, `* VOLATILE`).
>
> New probe `farlocal_probe.c` (+golden), wired compact+large in `test-dos.sh`.
> Huge is omitted ONLY for the pointer-MINUS-pointer (`q - *pp`) case — that
> needs seg*16+off linearization the backend doesn't do (a separate pre-existing
> huge gap); the alloc/member/struct-return cases all pass under huge too.
>
> **THE next moves (unchanged goal — get MicroPython data out of DGROUP):**
> 1. **FAR_SETJMP_EXE** — a far-data setjmp/longjmp variant (4-byte env ptr +
>    ES), gated by far_data_model() the way FAR_STDIO_EXE is.  The medium
>    SETJMP_EXE (§1r, in `tools/libstub_to_exe.py`) reads a 2-byte near env ptr;
>    under compact/large the env arg is a 4-byte far ptr.  Mirror FAR_STDIO_EXE's
>    ES handling; extend `setjmp_probe.c` to compact/large.
> 2. **Link MicroPython under far placement.**  Parametrize
>    `tools/build-micropython.sh` to take `--model=compact` (or large) and set
>    `QBE_FAR_STATIC_DATA=1` (so each module's statics go to its own FAR_DATA
>    segment outside DGROUP, freeing DGROUP for heap+stack — the §1r runtime-hang
>    fix).  The compile step is now clean (this session); expect to surface
>    link-layer gaps (far-data far_stdlib mangling already exists) and then a
>    runtime attempt at `print(1+2)` → `3`.  `gc_collect` is still a no-scan STUB.
> See [[minic-far-data-segment]], [[minic-setjmp-longjmp]].

# (DONE §1v compile) Next session — far-data DONE for opt-in; either flip placement to default or build MicroPython under far placement (post §1u)

> **§1u — FARSTORAGE landed (commit `cfde49b`): direct far-GLOBAL access
> (load/store/member/struct-copy/++/pointer-global) now works under far
> placement.  Gate 137→140 green, `make check` green, 111 s/r 0 r/r.**
>
> New `FARSTORAGE(s)` predicate (true for a Glo/Ext symbol under a far-data
> model — a STORAGE-location property, distinct from ISFAR's value-type bit;
> NO PTR/FUN exclusion since a global pointer's 4-byte value also lives far).
> Threaded through `load()` (delegates to loadfar), the assignment +
> prefix/postfix inc-dec STORE conditions (`|| FARSTORAGE`), the three
> member-address sites (Kl `=l add` + FAR propagation when base_far), and
> `emit_struct_copy` (far per-word path when either side is a direct global).
> KEY finding while verifying: the i8086 backend's IMPLICIT far-lowering of a
> near `storew/loadw $sym` already covered SIMPLE scalar global access (so most
> cases "worked"), but FAILED `emit_struct_copy` and some RMW — FARSTORAGE makes
> minic emit the explicit reliable storefX/loadfX so it's correct everywhere.
> Bug 1 (§1t far-store AX/DX bracket) is a prerequisite (the storefw-to-CAddr
> path it enables).  Probe `farglobal_probe.c` (compact/large/huge, built with
> `QBE_FAR_STATIC_DATA=1` so globals sit at offset 0 of their own FAR_DATA
> segment); verified bug-loud without FARSTORAGE ("ptcopy FAIL 5764",
> "g_i_rw FAIL 23").  NEAR_DATA models byte-identical (predicate false there).
>
> **Placement is still OPT-IN** (`QBE_FAR_STATIC_DATA=1` / `--far-static-data`).
> With FARSTORAGE done, far placement + far globals now work TOGETHER, so the
> two honest next moves are:
> 1. **Build the MicroPython subset under far placement (compact/large)** — the
>    actual goal: `tools/build-micropython.sh` with `QBE_FAR_STATIC_DATA=1` and
>    `-m compact`/`-m large`, freeing DGROUP for heap+stack.  Needs a far-data
>    setjmp/longjmp variant (`FAR_SETJMP_EXE`, 4-byte env ptr + ES — mirror
>    FAR_STDIO_EXE gating), and likely surfaces the 27 far-data TU compile
>    failures noted at §1s (`gvn.c:210` KWIDE assert + minic "non-homogeneous
>    pointers in subtraction").
> 2. **Flip placement to DEFAULT under far-data** (drop the `--far-static-data`
>    gate).  ONLY blocker now is neutralizing caddr_cmp_probe's k_lo cases (the
>    segmented-semantics non-bug from §1t — `&g-1` offset-wrap; keep that probe's
>    symbol in DGROUP or drop the k_lo asserts when far-placed).  Then re-run the
>    whole far-data gate with placement on for all probes.
> See [[minic-far-data-segment]].

# (DONE §1t/§1u) Next session — finish far-data: re-apply FARSTORAGE (far-GLOBAL direct access), then decide default vs opt-in (post §1t)

> **§1t — backend bug 1 FIXED & committed (`2e76a99`); "bug 2" DEMYSTIFIED as a
> segmented-pointer semantic limit, NOT a codegen defect.  FARSTORAGE NOT yet
> re-applied (deferred by user choice — "stop here for now").**
>
> **Bug 1 (DONE): far-store AX/DX save bracket.**  `Ostorefb/Ostorefh/Ostorefw`
> saved ES/BX/CX but not AX/DX; when the dest address is an RCon CAddr (far store
> to a constant global address, e.g. `arr[CONST]=v`), `load_farptr_con`'s
> `mov ax, seg sym` clobbered an AX-resident live temp (return value).  Fix:
> `kl_save_axdx`/`kl_restore_axdx` bracket, mirroring `Oloadf*`/`Ostorefl`.  Probe
> `caddr_store_probe.c` (compact/large/huge) — verified bug-loud ("ret_w FAIL 908"
> = `seg arr` leaked into AX).  Gate 134→137, `make check` green.
>
> **Bug 2 (NOT A BUG — do not try to "fix" cmp32).**  Reproduced via
> `QBE_FAR_STATIC_DATA=1 caddr_cmp_probe` (g_long at off=0 of its far segment):
> ltu_sym/leu_sym/gtu_lo/geu_lo FAIL.  ROOT CAUSE: QBE folds `&g_long - 1` into a
> CAddr `$g_long + (-1)`; on i8086 far that −1 addend WRAPS the 16-bit offset
> (0→0xFFFF) WITHOUT borrowing into the segment word, so k_lo materializes as
> `S:0xFFFF` (asm literally `mov ax, _g_long+-1`), not flat `(S-1):0xFFFF`.  cmp32
> then faithfully compares the wrapped representation — it is CORRECT.  The
> probe's k_lo assertions assume FLAT 32-bit pointer arithmetic, which segmented
> far pointers don't honor (`&g-1` is UB; far ptr ±n is offset-only in
> compact/large; there is no single seg:off that is "the byte before a paragraph
> base" without normalization).  So the cmp32 path needs NO change.
>
> **What this means for the default-flip:** the original step (c) "fix bug 2"
> dissolves.  To make far-static-data the DEFAULT you must instead NEUTRALIZE
> caddr_cmp_probe's k_lo cases under far-segment-offset-0 placement (keep that
> probe's symbol in DGROUP, or drop the k_lo cases when far-placed) — they test
> ill-defined cross-segment-boundary far-pointer ordering, not codegen.
>
> **REMAINING WORK — re-apply FARSTORAGE (the real far-global-access codegen).**
> This is the actual prize and is needed for MicroPython data regardless of the
> default decision.  Open question (asked, user chose to defer): wire it as an
> OPT-IN far-globals mode (gated behind a minic flag mirroring the
> `--far-static-data` opt-in; default gate stays byte-identical) vs UNCONDITIONAL
> under far-data + flip placement to default.  Opt-in is lower-risk and still
> unblocks MicroPython (it needs far placement + far globals together anyway).
> Reconstruction recipe is below (the prototype was correct in direction).
> Verify with the all-on experiment: with bug 1 fixed, the ONLY all-on failures
> should be caddr_cmp's k_lo cases (the segmented-semantics non-bug above).
> See [[minic-far-data-segment]].

# (superseded) Next session — finish far-data: make direct global access far so far-static-data can be the default (post §1s)

> **§1s — additional far data segment(s): the placement INFRASTRUCTURE is in
> and proven, landed OPT-IN.  The remaining work is the minic/qbe far-GLOBAL
> direct-access codegen so it can become the default and unblock MicroPython.**
>
> **What landed (opt-in, gate green at 132→… with `fardata_probe`):**
> Under a far-data model (compact/large/huge), passing
> `asm_to_omf.py --far-static-data` routes a module's statics into its OWN far
> segment `<BASE>_DATA`/`<BASE>_BSS` (class FAR_DATA/FAR_BSS) placed by
> `omf_link.py` DISTINCTLY, OUTSIDE DGROUP.  Each segment has its own `seg sym`
> selector (the same mechanism `_HUGE_<sym>` arrays already use), so **total
> static data can exceed the single 64 KB DGROUP** — DGROUP is left holding only
> the hand-asm crt0/libstub near data + the stack.  No 64 KB bin-packing needed:
> each module gets its own segment.  Proven by `fardata_probe.c` (medium-…er,
> compact/large/huge): **48 KB of statics in a far segment, read back correctly**
> (`big[0]`/`big[6000]`/`big[11999]`, a strided sum, an initialized `seed[]`/`tag[]`)
> — a link that overflows 64 KB under the old all-in-DGROUP scheme.
> `build-example.sh` opts in via env `QBE_FAR_STATIC_DATA=1`; `test-dos.sh`
> sets it for `fardata_probe` only (basename-gated).  Default OFF, so every
> existing compact/large/huge probe is byte-identical (DGROUP, near) and the
> gate stays green.  KEY ENABLER discovered: qbe ALREADY addresses every global
> far under far-data (`mov ax, seg _sym; mov es,ax; es:[bx]` — never assumes
> DGROUP), and the linker already resolves `seg sym` for non-DGROUP segments —
> so ACCESS needs no codegen change, only PLACEMENT.  See [[minic-far-data-segment]].
>
> **Why it's OPT-IN, not default — the far-GLOBAL-access gap (THE next task).**
> Turning placement on for ALL probes surfaced that minic emits **near**
> load/store for DIRECT global access (`g`, `the_thing.v`, `g = x`, `g++`) under
> far-data — it only ever worked because globals lived in DGROUP (=DS).  Array
> subscript (`arr[i]`) already goes far (Kl pointer arith), which is why
> `fardata_probe` passes without any minic change.  I prototyped the fix — a
> `FARSTORAGE(s) = (!NEAR_DATA() && (s.t==Glo||s.t==Ext))` predicate threaded
> through `load()`, the store sites, and member-access (clean storage-vs-value
> separation, handles scalar AND pointer globals, no type pollution) — and it
> took the all-on gate from **12 → 6** failures (fixed extern_struct, tentative_def,
> const_init, phase_bprime, storefl).  **REVERTED** it because it exposed TWO
> more latent backend bugs it doesn't itself fix, and shipping a half-far
> global model would be worse than opt-in:
> 1. **`storefw`/`store*` to a CAddr (`$g_sink`) destination corrupts a live
>    slot** — surfaced in `farretprobe` `two_live_a` (the `g_sink` write made the
>    `p`-return path return garbage).  A far store whose DEST is a global symbol
>    address (not a register far pointer) mis-targets / clobbers.  Likely an
>    i8086 `Ostoref{b,h,w}` RCon-CAddr-dest register-save gap (cf. caddr_arith).
> 2. **Segment-boundary unsigned compare vs a CAddr** — `caddr_cmp_probe`
>    `ltu_sym`/`leu_sym`: `(k-1) < &g_long` where `&g_long` now has off=0 in its
>    own far segment, so `k-1` borrows into the segment word; the cmp32 CAddr
>    unsigned high-word path gives the wrong order.  Only reachable once a global
>    sits at offset 0 of a far segment (which far placement makes common).
>
> **Plan to make far-static-data the DEFAULT (and unblock MicroPython data):**
> (a) re-apply `FARSTORAGE` in minic (the prototype was correct in direction;
> reconstruct from this note / git reflog), (b) fix bug 1 in `i8086/emit.c`
> (`Ostoref*` with RCon CAddr dest — push/pop the scratch regs, mirror the load
> path), (c) fix bug 2 (cmp32 CAddr unsigned high-word ordering at a segment
> boundary), (d) then flip placement on by default under far-data models and
> drop the `--far-static-data` gate.  Each bug wants its own probe.  THEN
> MicroPython under `large` needs its 27 far-data TU compile failures fixed
> (separate: `gvn.c:210` KWIDE assertion + minic "non-homogeneous pointers in
> substraction") before it links far.  See [[minic-far-data-segment]].

# (prior) Next session — MicroPython port: MicroPython LINKS but HANGS — DGROUP is too small; move static data to far segments (post §1r)

> **§1r — medium-model `setjmp`/`longjmp` landed; MicroPython now LINKS to a
> complete `mpython.exe`, but it HANGS at runtime.**  The setjmp/longjmp link
> blocker is CLOSED and runtime-verified by a real NLR round-trip probe.  The
> link advanced through it (and through the next wall) to produce — for the
> first time — a complete MicroPython .EXE.  The new frontier is a RUNTIME hang
> rooted in the medium model's single 64 KB DGROUP.
>
> **setjmp/longjmp (so you don't redo it):** new `SETJMP_EXE` in
> `tools/libstub_to_exe.py` (added to `build_epilogue`, unconditional), written
> directly in FAR form (4-byte CS:IP, `retf`; longjmp `mov sp,[bx+2]` + push
> CS:IP + `retf` synthesizes the far jump).  `jmp_buf` is `int[8]`; 7 words used:
> [0] caller BP, [2] resume SP (= setjmp's `bp+6`; the i8086 ABI passes args in
> caller-reserved slots and does NOT clean them, so resume SP == caller SP just
> before `call far`), [4] SI, [6] DI, [8] caller BX, [10] ret IP, [12] ret CS.
> New `minic/include/setjmp.h`.  **The bug that bit:** the first cut clobbered
> **BX** (used as the env pointer) without restoring it — BX is callee-saved
> here (qbe puts locals in BX/SI/DI), so a 2nd setjmp whose env arg lived in BX
> (`nlr_push(&middle)` right after `nlr_push(&outer)`) got a garbage pointer →
> wild longjmp → nondeterministic hang/crash.  Fix: `mov bx, dx` restore before
> `pop bp; retf`.  Probe `setjmp_probe.c` + golden (gate, **medium-only** — the
> far helper reads a 2-byte near env ptr): real nlr_buf_t chain, nlr_push=setjmp,
> nlr_jump=longjmp; covers direct=0, val, 0→1, deep 3-frame unwind, callee-saved
> guard survival, chained-buffer pop.  Gate **130→131**, `make check` green, no
> minic/qbe change (111 s/r 0 r/r unchanged).  See [[minic-setjmp-longjmp]].
>
> **THE new blocker — 64 KB DGROUP overflow (medium model).**  At the default
> port config (`MICROPY_HEAP_SIZE`=24576, `--stack-size 8192`) the link fails:
> `DGROUP + stack overflows 64KB (sp=87184)`.  MicroPython's static data (qstr
> pools, ROM const tables, mp_state BSS) is ~55 KB, and in the medium model
> _DATA + BSS + heap + stack ALL share one 64 KB DGROUP.  I confirmed shrinking
> to `MICROPY_HEAP_SIZE`=7168 + `--stack-size 3072` DOES link →
> `build/mp-link/mpython.exe` (452 KB; 108 modules; 370 KB far code across many
> segments; 61.5 KB data) — but it then **HANGS at runtime** (only ~4 KB DGROUP
> left for the stack ⇒ near-certain parser/compiler stack-starvation; could also
> be a codegen bug only this large multi-segment binary exercises).  I reverted
> both shrinks (they don't yield a working binary; the default config is the
> honest signal).
>
> **The real fix is NOT shrinking — it's getting MicroPython's static data OUT
> of DGROUP.**  Two paths:
> 1. **Build the MicroPython subset under the far-data model (compact or
>    large).**  Then _DATA pointers are 4-byte far and the linker can place
>    const/ROM tables in their own far segments, freeing DGROUP for heap+stack.
>    This is the architecturally-correct path and reuses the existing
>    `_far_X` libstub family + `far_stdlib[]` mangling.  Cost: every TU
>    recompiled `-m compact/large`; setjmp/longjmp needs a far-data variant
>    (4-byte env ptr + ES) — write a `FAR_SETJMP_EXE` gated by
>    `far_data_model(model)` (mirror how FAR_STDIO_EXE is gated).  qstr ROM
>    tables and `MP_ROM_*` const pools are the bulk to relocate.
> 2. **Aggressive data reduction** (smaller `MICROPY_CONFIG`: fewer builtins,
>    smaller qstr set, `MICROPY_ENABLE_COMPILER` trimmed) to get static data
>    well under ~50 KB so heap+stack fit in medium.  Cheaper to try first as a
>    smoke test, but a dead end for any real program.
>
> Milestone unchanged: `print(1+2)` → `3` in DOSBox (Phase 4).  `main.c` already
> does `do_str("print(1+2)", MP_PARSE_SINGLE_INPUT)`.  `gc_collect` is still a
> no-scan STUB (needs a real stack scan, now that setjmp works it can spill
> callee-saved regs) and `alloca` is routed to `m_malloc` via
> `MICROPY_NO_ALLOCA` — fine for `print(1+2)` but replace before non-trivial
> programs.  See `MICROPYTHON_PORT.md` and [[minic-setjmp-longjmp]].

---

# (DONE §1r) Next session — MicroPython port: implement medium-model setjmp/longjmp (the LAST link blocker) (post §1q)

> **§1q (build bring-up step 3): FIRST REAL LINK of the curated core subset.**
> The whole MicroPython core (104 curated py/*.c + 2 port glue TUs) now
> compiles to OMF objects (106/106, 0 failures) and **links cleanly except for
> ONE remaining undefined symbol: `setjmp`/`longjmp`** (the NLR primitive).
> Everything else — duplicate-symbol collisions, `__builtin_clz`, `memmove`,
> `__builtin_expect`/`unreachable`, `gc_collect`, `alloca` — is resolved.
>
> New canonical harness `tools/build-micropython.sh` (committed): per-TU
> `clang -E` → minic -m medium → qbe -t i8086 → asm_to_omf → nasm, then
> crt0_exe + all .obj + libstub_exe → omf_link → `build/mp-link/mpython.exe`.
> Re-run: `bash tools/build-micropython.sh --keep-going`.
> New port glue (in the micropython tree): `ports/dos8086/main.c` (a
> `do_str("print(1+2)")` entry — the Phase-4 milestone path, avoids pulling in
> pyexec/readline so the subset stays py-core-only) and `ports/dos8086/mphalport.c`
> (INT 21h AH=40h console output).  Gate **128→130/130**, `make check` green,
> 111 s/r 0 r/r (no grammar change).
>
> **The fixes this session (so you don't redo them):**
> 1. **`static` functions were exported as public OMF symbols** (the link wall:
>    `duplicate public symbol '_utf8_get_char'`).  C `static` = internal
>    linkage; `static inline` helpers in shared headers (MicroPython's
>    `utf8_get_char` in py/misc.h, etc.) were defined-and-exported by every TU
>    that included them → duplicate publics.  TWO-part fix:
>    (a) **minic** (`minic.y`): emit QBE module-local `function` (not `export
>    function`) for a `static` function.  New `pending_static` flag, set/cleared
>    in the `yylex()` wrapper (lexer-level — set on a top-level `STATIC` token,
>    cleared at the function-body-closing `}` and at a top-level `;`), read at
>    all 8 function-header emit sites via the new `fn_export_kw()` helper.
>    Lexer-level (not grammar) keeps conflicts at 111 s/r 0 r/r.
>    (b) **`tools/asm_to_omf.py`**: stop auto-promoting CODE labels to publics.
>    It used to promote EVERY `_xxx:` label because minic didn't mark file-scope
>    *data* as exported.  Now it tracks `defined_text` (labels in a `.text`
>    section) and auto-promotes only NON-text (data/bss) labels; code labels are
>    public iff minic emitted `.globl` (i.e. `export function`).  Static data is
>    still auto-promoted (minic still doesn't `export data` — a separate, not-yet-
>    blocking gap; revisit if static-data duplicates ever surface).
>    Pinned by `static_linkage_probe.c` (medium + large): static fns reachable
>    via the far-call path (direct, nested static->static, recursion, and a
>    function pointer to a static fn), plus a non-static `exported_double` that
>    must stay exported.
> 2. **libstub helpers** (`minic/dos/libstub.asm`, near form — libstub_to_exe.py
>    shifts `[bp+N]→[bp+N+2]` and `ret→retf` for the .EXE): `___builtin_clz`
>    (16-bit CLZ, loop — 8086 has no BSR), `_memmove` (overlap-safe, near-data
>    offset compare picks direction), `___builtin_expect` (returns arg0),
>    `___builtin_unreachable` (bare ret).  All NEW additive symbols (no gate
>    test referenced them); placed before the prune skip region.
> 3. **`gc_collect`** — bring-up STUB in `main.c` (`gc_collect_start();
>    gc_collect_end();`, no root scan).  `print(1+2)` allocates far below the
>    24 KB heap so no collection triggers.  **Must be replaced with a real
>    stack scan** (needs working setjmp to spill callee-saved regs) before any
>    non-trivial program.
> 4. **`alloca` eliminated via config, not codegen** — `MICROPY_NO_ALLOCA=(1)`
>    in `ports/dos8086/mpconfigport.h` routes `alloca(x)`→`m_malloc(x)` (GC
>    heap).  True alloca needs a stack-frame-extending builtin minic doesn't
>    have, and a far-called libstub helper can't grow the *caller's* frame —
>    so config is the right call.
>
> **THE remaining blocker — `setjmp`/`longjmp` for the medium model.**  This is
> the NLR keystone: `nlr_push`/`nlr_pop` (py/nlrsetjmp.c) and the whole
> exception/unwind path depend on it, so NOTHING runs until it works.  It is
> **NOT mechanically convertible** from a near-form libstub stub: the
> medium-model far-call frame has a 4-byte return address (CS:IP), needs `retf`,
> and longjmp must do a FAR jump to restore CS:IP — the `[bp+N]+2` / `ret→retf`
> rewrite in libstub_to_exe.py cannot synthesize that.  So write it directly in
> the **far form** in `tools/libstub_to_exe.py`'s EPILOGUE (alongside
> FAR_STDIO_EXE etc.), or as a model-specific asm.  `jmp_buf` is `int[8]`
> (16 bytes); save BP, SP-at-resume (= lea bp+6 in the far frame), SI, DI, BX,
> and the return CS:IP; longjmp restores them and `jmp far` to CS:IP with the
> value in AX (longjmp(env,0) must yield 1).  **Add a real NLR runtime probe**
> (nlr_push/nlr_raise/nlr_pop round-trip — not just a setjmp smoke test) since
> the ABI is subtle; verify unwinding across a nested call.  Then
> `build-micropython.sh` should produce `mpython.exe` — try `print(1+2)` → `3`
> in DOSBox (Phase 4 milestone).  Expect to then hit codegen/stack/heap runtime
> bugs (the gc_collect stub, far-code segment-count limits, etc.).

# Next session — MicroPython port: py/*.c ASM->OBJ-clean (132/132 to OMF object) (post §1p)

> **§1p (build bring-up step 2): all 132 py/*.c now survive asm->obj** — each
> per-TU i8086 `.asm` (from §1o's `cg/<base>.asm`) goes through the real build's
> `asm_to_omf.py` wrap + `nasm -f obj` and produces an OMF object file.  New
> harness `build/mp-spike/run-asmobj.sh` (committed; the other spike scripts are
> not).  First run: 13 OK, 119 NASM_FAIL — but only **3 distinct root causes**,
> all fixed; second run **132/132 OK**.  `make check` green, 111 s/r 0 r/r, gate
> **125→128**.
> Re-run: `bash build/mp-spike/run-asmobj.sh $(cut -f1 build/mp-spike/codegen.tsv)`
> (needs §1o's `run-codegen.sh` to have produced `cg/*.asm` first).
>
> **The three §1p fixes (so you don't redo them):**
> 1. **`asm_to_omf.py` missed multi-underscore externs** (118 of 132 files).
>    `__builtin_clz` is mangled by minic to `___builtin_clz` and called via
>    `call far ___builtin_clz`.  `collect_referenced_syms`'s regex
>    `\b(_[A-Za-z]…)` can't match it — the word boundary sits before the FIRST
>    underscore, which is followed by `_` not a letter, so the symbol was never
>    added to the `extern` set and nasm failed "symbol not defined".  Fix:
>    `\b(_+[A-Za-z][\w]*)`.  (NB: `___builtin_clz` itself still has no runtime
>    impl — that's a libstub/link-layer gap for later; the per-TU object just
>    needs the extern declared.)
> 2. **C labels collided across functions** (py/runtime.c).  Two functions each
>    with a `too_short:` C label both emitted the flat `@user_too_short` block
>    → one asm symbol `user_too_short:` defined twice → nasm "inconsistently
>    redefined".  C labels are function-scoped.  Fix in `minic/minic.y`: a
>    per-function counter `cur_fn_labelid` (bumped at all 4 function-body emit
>    starts) suffixes every user label `@user_<name>_F<id>` at the Goto/Label
>    emit sites.  These labels aren't exported, so cross-module is already safe;
>    only the intra-module collision needed fixing.  Pinned by `dup_label_probe.c`.
> 3. **16-bit Ocopy of a relocatable address into a slot dropped the size**
>    (py/mpprint.c `_pad_common+17`, py/objstr.c `__str_uni_strip_whitespace`).
>    `=w add $sym, off` folds to a copy; when rega lands it in a slot the
>    generic `{Ocopy,Ki,"mov %=, %0"}` template emitted `mov [bp-N], _sym+off`
>    with no `word`, so nasm's OBJ writer rejected the relocation ("OBJ format
>    can only handle 16- or 32-bit relocations").  Fix in `i8086/emit.c`: an
>    early special-case for `Ocopy Kw && to=RSlot && arg[0]=RCon` emits
>    `mov word [bp-N], <imm/addr>` (no scratch reg, rega unaffected).  The Kl
>    Ocopy path already sized CAddr→slot correctly.  Pinned by `caddr_slot_probe.c`
>    (medium-only: far/Kl pointers route through the already-correct Kl path).
>
> Probes: `dup_label_probe.c` (medium+large), `caddr_slot_probe.c` (medium).

# Next session — MicroPython port: py/*.c CODEGEN-clean (132/132 to i8086 asm) (post §1o)

> **§1o (build bring-up step 1): all 132 py/*.c now survive the FULL codegen
> pipeline** (`minic | qbe -t i8086 -m medium` → i8086 asm), not just the
> parse+SSA step the old spike measured.  New harness
> `build/mp-spike/run-codegen.sh` runs each preprocessed TU through minic→qbe
> and tallies OK / MINIC_FAIL / QBE_FAIL / ASM_STUB.  First run: 124/132 OK, 8
> QBE_FAIL — all 8 were **minic SSA-emission bugs the parse-only spike could not
> see** (qbe validates the SSA; minic alone does not).  Three fixes flipped all
> 8 → **132/132 OK**.  `make check` green, 111 s/r 0 r/r, gate 123→125.
> Re-run: `bash build/mp-spike/run-codegen.sh $(ls -1 ~/projects/micropython/py/*.c | sed 's|.*/||;s|\.c$|.pp.c|;s|^|build/mp-spike/pp/|')`
> (needs the .pp.c files from run-spike.sh first).
>
> **The three §1o minic fixes (so you don't redo them):**
> 1. **Sub-word arithmetic result class** (`minic.y` irtyp→irtyp_ret at 3 emit
>    sites: general binop ~3155, inc/dec ~3070, float→int cast ~2745).
>    `uint16_t+uint16_t` / `uint8_t+uint8_t` where both operands share the
>    narrow type made `prom()` return that type, so the add result temp was
>    `=h`/`=b` — invalid QBE temp class (only w/l/s/d).  `irtyp_ret()` widens
>    char/short→`w` (also C-correct: integer promotion).  Flipped
>    emitbc/gc/objringio/ringbuf ("invalid class specifier").
> 2. **Seq fall-through termination with a trailing goto-label** — `stmt(Seq)`
>    returned `r1||r2`, so an earlier `return` masked a textually-last labeled
>    block that falls through; minic skipped the synthetic trailing `ret` →
>    qbe "last block misses jump".  New `contains_label()` helper; a Seq whose
>    tail contains a label now reports the tail's termination alone (mirrors the
>    existing `contains_case_label` logic in genswitchbody).  Flipped
>    compile/objstr/parsenum.
> 3. **goto Label dropped between switch cases** — `genswitchbody` short-circuited
>    past a Seq tail when the prior case body terminated (`break`) and the tail
>    held no *case* label, dropping a plain goto target sitting between cases →
>    qbe "block @user_X is used undefined".  Now goto labels are kept too (the
>    same `contains_label` check).  Flipped runtime (`power_overflow:` in
>    `mp_binary_op`).
>
> Probe `codegen_term_probe.c` (medium + large) pins all three.

# Next session — MicroPython port: py/*.c DONE (132/132); extmod/shared widened (post §1n)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **py/*.c spike now 132/132 OK** — the old `stream` fail was a harness gap
> (`SEEK_SET` undefined) and was closed by adding `SEEK_SET` to
> `build/mp-spike/stubinc/unistd.h`.  §1n then **widened the spike to
> extmod/*.c + shared/**\*.c (96 files)**: 90 OK, 4 MINIC_FAIL, 2 CPP_FAIL.
> Re-run with
> `bash build/mp-spike/run-spike.sh ~/projects/micropython/extmod/*.c $(find ~/projects/micropython/shared -name '*.c')`
> then `grep -E 'MINIC_FAIL|CPP_FAIL' build/mp-spike/summary.tsv`.
> Gate **121→123/123** (+extern_array_expr_probe medium+large). 111 s/r, 0 r/r.
> `make check` green.
>
> **The 4 remaining extmod/shared MINIC_FAILs are NOT minic grammar bugs** (all
> harness/arch artifacts — minic correctly rejects undefined symbols):
> - `sys_stdio_mphal` — `MP_QSTR_readlines` is not in the spike's generated
>   qstr enum (the qstrdefs only cover qstrs seen in py/*.c).  A real build's
>   QSTR generation would emit it.  Same class as the old `stream`.
> - `softtimer` — `MICROPY_PY_PENDSV_EXIT;` is an undefined port macro (left as
>   a bare-identifier statement → "undefined variable").
> - `import` — `mp_import_stat_t` is an undefined typedef (py/lexer.h not pulled
>   in by the spike's minimal include set for this TU).
> - `gchelper_generic` — `const register long x19 asm ("x19");` is the GCC
>   named-register-variable extension on an ARM code path the spike's cpp defines
>   wrongly selected; irrelevant to the i8086 port (which supplies its own
>   gchelper).  (CPP_FAILs `semihosting_rv32`/`semihosting_arm` are missing
>   `<stdnoreturn.h>` / unknown-arch — also not minic.)

## What changed §1n (so you don't redo it)

**One real grammar gap fixed — extern array with a constant-EXPRESSION
dimension.**  `extern char buf[(32) + 1];` parse-errored while
`extern char buf[2];` parsed.  The `EXTERN type IDENT '[' NUM ']' ';'` rule was
the lone array-decl holdout still pinned to `NUM`; changed it to
`'[' expr ']'` (line ~5313 in `minic/minic.y`).  An extern allocates no storage
here, so the folded size is discarded.  0 new conflicts (still 111 s/r 0 r/r).
Flipped extmod/network_ppp_lwip.c (its `mod_network_hostname_data[(…)+1]`).
Probe `extern_array_expr_probe.c` (medium + large).

**Pre-existing gap found, NOT fixed (didn't block any real consumer):**
file-scope sized char array initialised from a string literal —
`char g[5] = "abcd";` parse-errors even with a plain literal dim (brace init
`char g[5] = {'a',…};` and unsized `char g[] = "abcd";` both work).  The probe
sidesteps it with brace init.  Fix later only if a consumer needs it.

## What changed §1m (so you don't redo it)

Four grammar/codegen wins, all in `minic/minic.y` (+ gate wiring), no i8086/QBE
backend changes, **no new conflicts (still 111 s/r, 0 r/r)**, `make check` green.
**Flipped binary, objlist, modbuiltins, objtype, parse** (126→131).

1. **Anonymous struct/union as a type** (flips binary, objlist; half of
   modbuiltins) — `struct { … }` / `union { … }` can now be used directly as a
   `type` (in a cast `(struct{…}*)0`, a local decl `struct{…} v;`, a typedef
   `typedef struct{…} T;`, or a struct member `struct{…} name;`).  The §1k
   attempt (`type: typedefstructstart smembers '}'`) gave **76 r/r** because
   `STRUCT '{'` then had TWO empty marker reductions reachable inside a struct
   body: `typedefstructstart` (anon typedef) and `nested_s_begin` (nested anon
   member).  **Fix = UNIFY them.**  There is now exactly ONE marker for
   `STRUCT '{'` / `UNION '{'` — `nested_s_begin` / `nested_u_begin` (always
   pushes the enclosing `curstruct`, or -1 at top level, onto `structstk`).
   `type: nested_s_begin smembers '}'` pops it and returns `(idx<<3)+STRUCT_T`.
   The former dedicated *named*-nested member rules (`nested_s_begin smembers
   '}' IDENT ';'`) were **removed** — `struct{…} name;` now flows through the
   existing `smembers type IDENT ';'` (its `type` reduces the anon aggregate,
   popping structstk back to the parent first).  `typedef struct{…} T;` flows
   through `TYPEDEF type IDENT ';'`.  `typedefstructstart`/`typedefunionstart`
   are now **tagged-only** (`STRUCT IDENT '{'`) and still back the tagged
   `typedef struct Tag{…} T;` path.  Anon-hoist (`struct{…};` no name) keeps its
   `nestedagg: nested_s_begin smembers '}' ';'` rule.  Probe `anon_aggr_probe.c`.
2. **Function-local + inner-block anonymous enum** (other half of modbuiltins)
   — `enum { A, B, C };` as a statement.  Added `dcls: dcls enumstart enums '}'
   ';'` (function-body top) AND `stmt: enumstart enums '}' ';'` (inner block),
   both mirroring file-scope `edcl` (constants registered by the `enums` rule;
   no storage).  Covered by `anon_aggr_probe.c` cases b/c.
3. **Compound literal with NESTED brace, incl. through a deref** (flips
   objtype) — `*o = (T){{a}, b, c};` (py/objtype.c's `mp_obj_super_t`, whose
   first member is a sub-struct filled by `{…}`).  `inititem` now accepts
   `'{' initlist '}'` and `.field = '{' initlist '}'`.  The expr() and lval()
   compound-literal paths previously had DUPLICATE inline member-fill loops;
   both now call one shared recursive `emit_clit_aggr(clitnum, base_off, sidx,
   init)` that descends into a sub-struct/union member on a nested-brace item.
   The lval() path matters because a struct compound literal on the RHS of
   `*p = …` is re-materialised via lval() to get its address for the struct
   copy.  Probe `nested_clit_probe.c`.
4. **Cast to a function-pointer type** (flips parse) — `(RET (*)(PARAMS)) expr`
   (py/parse.c: `ctx.func = (void (*)(void *))(mp_lexer_free);`).  New
   `pref: '(' type '(' '*' ')' '(' fptpar0 ')' ')' pref` reusing the existing
   `fptpar0` param-type list; the cast type is `IDIR(FUNC($2))`, reinterpreting
   the operand.  Distinguished from the plain cast / compound literal by the
   token after `type` (`(` vs `)`).  Probe `fnptr_cast_probe.c`.

Three probes added (each medium + large): `anon_aggr_probe.c`,
`nested_clit_probe.c`, `fnptr_cast_probe.c`.  Gate **115→121**.

## What changed §1l (so you don't redo it)

**for-init inner-block scope** — closed compile.c's sibling for-loop double
definition. The three C99 for-init rules share a `forinit_var: type IDENT '='`
nonterminal; the state after `type IDENT =` is a single-action state miniyacc
**default-reduces without lexing lookahead**, so the rename binding is
established before the test/increment/body uses are lexed.  Probe
`for_init_scope_probe.c`.  The apostrophe-in-action-comment footgun was also
fixed (commit `a4a1fe7`): `cpycode` in `minic/yacc.c` is comment-aware, so
action comments can use `'`/`"`/braces freely.

## Scope for next session — build bring-up, the next layer down the pipeline

All 132 py/*.c now go C→preprocess→minic(SSA)→qbe(i8086 asm)→asm_to_omf+nasm
cleanly (§1o codegen, §1p asm→obj).  The next layers toward a runnable REPL,
in increasing cost:

1. **DONE (§1p): asm→obj per TU.**  `build/mp-spike/run-asmobj.sh` wraps each
   `cg/<base>.asm` with `asm_to_omf.py` + `nasm -f obj`; 132/132 produce OMF
   objects.  Three gaps fixed (multi-`_` externs, per-function label
   uniquification, 16-bit Ocopy-CAddr→slot size) — see §1p above.

2. **First real LINK of a curated core subset** (NOW the cheapest next signal).
   The dos8086 port does NOT
   need all 131 host objects — drop the other-arch `asm*`/`emitn*`/`nlr*`
   (keep `nlrsetjmp`).  Needs: (a) genhdr headers (already generated at
   `~/projects/micropython/ports/minimal/build/genhdr/` — point `-I` at it or
   regenerate for dos8086), (b) `ports/dos8086/main.c` + `mphalport.c`, (c) a
   `tools/build-micropython.sh` that compiles the subset + crt0 + libstub and
   `omf_link`s them.  Expect: multi-segment far-code link limits (~50+ code
   segments), and `setjmp`/`longjmp` (NLR) — `jmp_buf` is an array typedef;
   real medium-model setjmp/longjmp is still a Phase-2 libc gap.  Milestone:
   `print(1+2)` → `3` in DOSBox (Phase 4).

3. **Widen the codegen spike to extmod/shared** (optional de-risk) — the parse
   spike already cleared them (90/96, rest harness/arch); running them through
   qbe would surface any remaining backend gaps cheaply.

Master staging plan + phase table: `MICROPYTHON_PORT.md`.

## How to find the true site (lag-proof technique, unchanged)
minic's reported error line is the parser's *lookahead* line and lags the real
construct.  Read the real message by running minic directly on
`build/mp-spike/pp/<file>.pp.c` (not the lagged summary.tsv line).
Forward-bisect on column-0 `}` boundaries with brace auto-balancing (a small
python `head -n CUT` + append `}`×(open-count) reproduces far enough into a
function body); the FIRST cut whose prefix errors brackets the construct.  This
session that pinned the fnptr-cast at line 2718 of parse.pp.c in seconds.

## Guardrails (unchanged)
- Rebuild with `cd minic && make minic`; local `yacc` prints conflict counts
  (now **111 s/r, 0 r/r**). Justify any new shift/reduce; **no new
  reduce/reduce**. miniyacc is picky: no `/* … */` between a production head and
  its `:` (this bit twice this session — keep standalone comments OUT of the
  space between a `;` and the next rule head; put them inside the action body
  instead, where `cpycode` is now comment-aware).
- Run `tools/test-dos.sh` (must stay **128/128**) and `make check` (SSA, "All
  is fine!") at the **repo root** (not minic/). Add or extend a probe per
  runtime-bearing feature; the gate runs ~5 min in DOSBox — run it in the
  background and wait.
- Spike harness uses **`clang -E`** (the build-example.sh path uses `cpp`).
- DOSBox capture is occasionally flaky. If a `--model=large` probe diff fails
  once, re-run.

## Orthogonal pre-existing limits (don't chase unless a real consumer needs them)
- **Two divisions feeding one call** — i8086 div AX/DX clobber, `[[i8086-two-div-one-call-clobber]]`.
- **Far-data static pointer relocation** (`l $sym` → far seg:off) — `&global`
  data items are near-only, so probes that take a static address are medium-only.
- **Bare file-scope scalar pointer initializer** — `static int *p = &g;` parse-errors.
- **File-scope sized char array from a string literal** — `char g[5] = "abcd";`
  parse-errors (brace init `{'a',…}` and unsized `char g[] = "abcd";` work).
  Found §1n; not fixed (no consumer blocked).
- **Inline `100000L` literal** — lexer drops the `L`; build from small-literal arithmetic.
- **Deep block-scope shadow of an already-renamed name** — §1k's alpha-renaming
  handles sibling blocks, single-level shadow, and inner-then-function-scope
  collisions; a *declarator* lexed while an outer rename of the same name is
  active (double shadow) can mis-stamp.  See `[[minic-inner-block-scope]]`.
- **Compound literal is evaluated twice on `*p = (T){…}`** — the struct-copy
  assignment path runs expr() (materialise + load) then lval() (materialise +
  address) on the same 'L' node, emitting the literal into two `_clit` slots.
  Correct, just wasteful; not worth fixing unless it shows up hot.
