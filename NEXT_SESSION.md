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
Older session headers (§6g and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
