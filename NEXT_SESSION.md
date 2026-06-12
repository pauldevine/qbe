# Next session (§6i — continue Phase 6.  §6h [2026-06-12, this session] completed **step 4e: stdio runs through the REAL newlibc stack on the bare machine — printf()/fgets() with no DOS underneath.**  The seam-shape question is ANSWERED: **newlibc's VFS `/dev/console` routing moved bare-metal**, NOT a libstub-level swap (the upper stack was already proven under this toolchain DOS-hosted, `bm_tty` was built to the exact `console_dev_read/write` contract in §6g, and a libstub-level swap would invest in the thing being retired).  New `minic/dos/newlibc/bm_shim.c` is the bare-metal counterpart of `dos_shim.c` — the SAME newlibc layering (`printf_wrappers → syscalls → vfs`, fds 0/1/2 = `/dev/console`) with the bottom device ops routed to bm_tty instead of INT 21h, the timer surface to bm_timer, the display surface to bm_display, plus the POSIX unprefixed aliases, `_impure_ptr`/heap link satisfaction, and the dos_shim-shaped minimal FILE layer; `bm_stdio.h` declares `bm_stdio_init()` (= `vfs_init()`) and is the build probe — `build-newlibc-baremetal.sh` links the full portable-subset TU set (printf/scanf wrappers, syscalls, reent_stubs, dirent, unlink, vfs, fat, block — same set as the DOS-hosted build) when a program includes it.  New battery test `stdio_bm` (113,904-byte image, the whole newlibc stack aboard): printf format sweep (`%d %u %04x %s %c`, return-value check), `write(1)` direct, fputc/fputs to stdout+stderr, `fprintf(/dev/null)` ret 9, ramfs `fopen+fread` of `/ram/readme.txt`, `isatty(0)=1 isatty(1)=1`, then the harness types **`vx\b9k\nz`** and `fgets(stdin)` hands back exactly `"v9k\n"` — keyboard ISR → bm_tty cooked read (real Backspace edit) → `console_dev_read` → `vfs_read(0)` → `_read` → `read` → fgets — then `getchar()` → `'z'` and `times()` advancing.  **FIRST-RUN PASS, zero compiler changes** (fourth driver/stdio session in a row riding the §6d ISR ABI).  Also this session: `~/projects/newlibc` moved again (3 commits past `5727ffb`: FAT WRITE support + SASI WRITE(6), medium-model default, vshell write commands) — all 11 DOS-hosted gated tests still PASS with NO golden refresh (the write support is dispatch-table-based: `vfs.c` only holds a `fat_write_ops` pointer installed at runtime by `fat_write.o`, which we don't link; read-only paths unchanged).  Gates: test-newlibc **11/11**, test-dos **289/289**, test_omf_link all pass; NO toolchain change so no emit audit / MP byte-compare triggered.  Next: **step 4f candidates** — block/SASI driver port (MAME `-scsi:0 harddisk`) for bare-metal FAT, now directly useful since upstream just grew FAT WRITE + SASI WRITE(6) to port against; newlibc-tests (snprintf/stdio_route/…) re-run BARE-METAL through bm_stdio (many should be near-free now); scanf-over-cooked-tty when a consumer appears; run-dos-exe.sh stdin redirect for the 3 remaining DOS-hosted tests.)

## §6h session notes (2026-06-12)

### The seam decision (libstub swap vs VFS routing — VFS routing won)
- dos_shim.c and bm_shim.c are deliberate parallels: same extern surface
  (console_dev_*/tty_dev_*, timer_*, display_*, POSIX aliases, FILE
  layer, _impure_ptr, __heap_start/__heap_end), different bottom —
  INT 21h there, bm_tty/bm_timer/bm_display here.  A future model only
  needs a third shim.
- bm_stdio_init() is just vfs_init(); driver bring-up stays explicit in
  the program (the interrupt-window ordering — interrupts_init, timer,
  tty, sti — is the program's to own, per the §6d/§6g rules).
- The newlibc stack TUs have ZERO `#ifdef DOS` — identical sources
  compile bare-metal; only the shim differs.  No -Dmain rename, no
  HALT2DOS (syscalls.c `_exit`'s hlt loop is bare-metal-correct).
- libstub --no-stdio still provides the `_stdin/_stdout/_stderr`
  sentinels (FILE._file = 0/1/2) that printf_wrappers' stream_fd()
  expects — `fgets(stdin)` routes to fd 0 because `_stdin_file` holds 0.

### stdio_bm test
- Same keypost as tty_bm ("vx\b9k\nz") but the line returns through
  fgets(stdin) — the full newlibc read path over the cooked console.
- printf returns 47 for the format-sweep line (golden-locked); the
  echo bytes (raw 0x08s in `v9k> vx\b \b9k`) live in the golden,
  deterministic as in tty_bm.
- 45-second budget is ample (run completed well inside it; the 113 KB
  image's Lua load is the main extra cost over tty_bm's 30 s).

### Upstream drift check (the §6g TRAP, exercised again)
- 3 new commits (fbd8dd5/8dfd23e/5f8996b): FAT write + SASI WRITE(6),
  medium default, vshell RW.  vfs.c +252 lines compiled clean under
  minic; all 11 DOS-hosted goldens UNCHANGED because fat_write is a
  runtime-installed dispatch table (`vfs_set_fat_write_ops`), not a
  link dependency — read-only mounts never touch it.
- The uncommitted working-tree changes in ~/projects/newlibc also flow
  into our builds (we compile the tree as-is) — same drill: TEXT-diff
  drift = source movement, not a toolchain regression.

### Open tracks (new + carried)
- newlibc step 4f: block/SASI bare-metal driver port (MAME -scsi:0
  harddisk) for bare-metal FAT — upstream's new FAT WRITE + SASI
  WRITE(6) is the natural porting target; re-run DOS-hosted newlibc
  tests bare-metal through bm_stdio (near-free candidates: snprintf,
  stdio_route, ramfs); scanf-over-tty when a consumer appears; serial
  TX ISR when a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

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
Older session headers (§6e and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
