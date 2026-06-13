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

# Next session (§6t — continue Phase 6.  §6s [2026-06-13, this session] gated the UNMODIFIED upstream `block_test` BOTH DOS-hosted AND bare-metal through bm_testhost + the bm_stdio/block stack, with ZERO compiler/toolchain/build-script changes — **test-dos 294/294 → 295/295, battery 31/31 → 32/32** (as light as §6r: the test resolves through `build-newlibc-test.sh` and `build-newlibc-baremetal.sh` unchanged, no probe widen even needed).  `block_test` exercises the **block-device layer DIRECTLY, one level below FAT** — the first deterministic golden for the block layer in isolation (the FAT tests reach it transitively but never assert its cache/error semantics): `block_register_ramdisk` + `block_init`, single- and multi-sector `block_read`/`block_read_sector`/`block_write_sector`, the write-through cache + `block_cache_invalidate` refresh path, `block_get_info`/`block_status`/`block_cache_flush`, and the three error paths (out-of-range read → `-EINVAL`, invalid device → `-ENODEV`, read-only write → `-EROFS`).  Like §6r's `fat_victor_label_test` it is **RAM-disk style** (`block_register_ramdisk`, no `-scsi:0`), so it runs BOTH DOS-hosted (added to the `NEWLIBC_TESTS` array in `tools/test-dos.sh`, golden `minic/dos/tests/newlibc_block_test.golden.txt`, CRLF-stripped and verified byte-exact through the full run-dos-batch path at 295/295) AND bare-metal (battery entry `block_test:60:::`, golden `minic/dos/tests/block_test.golden.txt` with the bm_testhost `pic+timer`/`tty+sti`/`vfs` preamble), and the bare-metal body is **line-identical** to the DOS golden between the preamble and the `test returned 0` result line (verified by diff; the §6j RAM-disk pattern).  It builds **SMALL in BOTH hosts** (DOS 51,811 B code; bare-metal 60,505 B, under the 64 KB single-`_TEXT` ceiling — no medium, contrast the §6p/§6q SASI read-path tests that pulled dirent.c/block over the ceiling), and emits 18 output lines so a 60-emulated-second budget is ample.  **FIRST-RUN PASS** on MAME (`tools/test-newlibc.sh block_test` → **[ok]**) and through the DOS gate (`newlibc small (block_test)` → **[ok]**).  The other 31 battery entries and 294 DOS entries are byte-unaffected (each harness change is one array entry), and with no compiler/qbe/emit/build-script source touched there is **no emit audit and no MP byte-compare**.  Next: the RAM-disk-style FAT/block family is now exhausted (block_test was the last clean both-hosts RAM-backed test); remaining ungated phase-3 tests are driver/hardware tests (font/keyboard/serial/interrupt — most already covered by the bm_* ports) and the §6p/§6q-style real-SASI read tests; or the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6s session notes (2026-06-13)

### Nothing new needed — pure battery + DOS-gate plumbing (one entry each + two goldens)
- `block_test` is a RAM-disk test (`block_register_ramdisk`, no `-scsi:0`),
  so it runs BOTH DOS-hosted (added to `NEWLIBC_TESTS` in `test-dos.sh`)
  and bare-metal (added to `NEWLIBC_BM_TESTS` in `test-newlibc.sh`).
  build-newlibc-test.sh and build-newlibc-baremetal.sh already resolve it
  (the test `#include`s only `block.h`, in the portable subset); no
  build-script or compiler change — not even the §6q-style SASI probe
  widen, since it touches no SASI.
- DOS golden captured via run-dos-exe (CRLF-stripped) and verified
  byte-exact through the actual run-dos-batch path (full test-dos.sh →
  295/295) before trusting it.
- Bare-metal golden captured from a clean MAME run; body diff-identical to
  the DOS golden between the testhost preamble and `test returned 0`.

### What this test covers that nothing else did
- The block-device layer in isolation: register/init, single- and
  multi-sector read, write-through cache + invalidate refresh, and the
  `-EINVAL`/`-ENODEV`/`-EROFS` error paths.  The FAT tests use the block
  layer transitively but never assert its cache or error semantics; this
  is the first deterministic golden for them.

### Model: SMALL both hosts (no medium)
- DOS 51,811 B code; bare-metal 60,505 B — under the 64 KB `_TEXT` ceiling.
  Contrast §6p `sasi_fat_dir_test` / §6q `sasi_sector_test`, whose
  dirent.c/block pulls put them over → medium.  A RAM-disk block-layer
  test is a lean TU set (block.c + stdio, no FAT/VFS/SASI).

### Open tracks (carried)
- The RAM-disk-style FAT/block family is now exhausted.  Remaining
  ungated phase-3 tests are driver/hardware (font/keyboard/serial/
  interrupt — mostly covered by the bm_* ports) or §6p/§6q-style
  real-SASI read tests.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6r and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
