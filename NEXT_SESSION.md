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

Older session headers (§6d and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
