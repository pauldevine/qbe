# Next session (§6k — continue Phase 6.  §6j [2026-06-12, this session] completed **step 4g: the ten portable DOS-hosted newlibc tests now run UNMODIFIED on the bare machine through bm_stdio — battery 12/12 → 22/22.**  The mechanism is a **test-host mode** in `tools/build-newlibc-baremetal.sh`: a source path that resolves into newlibc's `tests/` directory (`$NL/tests/*.c`) is recognized as an upstream test TU and gets the SAME `-Dmain=newlibc_test_main` rename the DOS-hosted gate (`build-newlibc-test.sh`) uses — with new `minic/dos/newlibc/bm_testhost.c` linked as `main()`: it does the §6d-ordered driver bring-up (`bm_interrupts_init` → `bm_timer_init` → `bm_tty_init` → `sti` → `bm_stdio_init`/vfs), calls `newlibc_test_main()`, then prints a `bm_testhost: test returned N` line and `__V9END__`.  Test-host mode auto-pulls the full bm_stdio TU set (bm_shim + bm_tty + drivers + printf/scanf wrappers + syscalls + vfs + fat + block).  The ten tests are snprintf_test, stdio_route_test, the six FAT/VFS tests (fat_bpb/chain/root/dir/file/vfs), terminal_meta_test, and ramfs_test; each one's bare-metal serial output is **line-identical to its existing DOS-hosted golden** between the testhost preamble and the result line — the ten new goldens (`minic/dos/tests/<name>.golden.txt`) were verified by `diff` against `newlibc_<name>.golden.txt` before locking, and all ten were **FIRST-RUN PASS with zero compiler changes** (sixth straight session on the §6d ISR ABI).  `bss_test` deliberately stays DOS-hosted only (display-only output ending in a `hlt` idle loop, no serial/stdio path; `memory_bm` already covers bare-metal RAM write-readback).  Harness: `tools/test-newlibc.sh` grew the ten entries (budgets follow output length per the §6f scroll lesson — most 45–90 s, but ramfs_test's 103 output lines need the **300-emulated-second** budget; the 180 s first try truncated mid-line, slowness not a hang).  Upstream `~/projects/newlibc` moved again since §6i — the FAT-write work plus mkdir/rmdir/rename merged through PR #19 (HEAD `16d54ac`) — with NO golden impact: `tools/test-dos.sh` (which rebuilds the moved tree) is still **289/289**, because the write support stays a runtime-installed dispatch table (`fat_write_ops`) that read-only mounts never touch and we never link.  Gates: test-newlibc **22/22**, test-dos **289/289**; NO toolchain change (new TU + build-script logic + harness only) so no emit audit / MP byte-compare triggered.  Next: port the upstream FAT-WRITE path (`vfs_mount_victor_fat_rw` + the `fat_write.c` dispatch install) — bm_sasi WRITE(6) is proven hardware under it, and the six bare-metal FAT tests could then run read-WRITE against the real `-scsi:0` disk; `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`); scanf-over-cooked-tty when a consumer appears; newlibc-under-far-models stdio story.)

## §6j session notes (2026-06-12)

### Test-host mode (build-newlibc-baremetal.sh)
- A source resolving to `$NL/tests/*.c` flips `TESTHOST=1`; everything in
  `minic/dos/newlibc/*.c` (the hand-written bare-metal tests) keeps the
  old bm_crt0 `start()`-calls-`main()` arrangement with no rename.
- `bm_testhost.c` is the bare-metal seat of dos_shim's main(): it owns
  the interrupt-window bring-up order (PIC re-init BEFORE sti, §6d), runs
  `vfs_init()`, calls `newlibc_test_main()`, prints the result line
  through the very stack the test exercised, then `__V9END__`.
- Test-host mode implies the full bm_stdio TU set even without the
  program `#include`-ing bm_stdio.h (the rename means the test's own
  includes are just `<stdio.h>` etc.).
- The preamble (`bm_testhost: pic+timer / tty+sti / vfs`) prints through
  the POLLED serial console (bm_puts) before vfs is up; the test body and
  result line go through the newlibc stack.

### Golden equivalence (the load-bearing claim)
- Each bare-metal golden, stripped of the 3 preamble lines and the
  trailing result+`__V9END__`, is `diff`-identical to the DOS-hosted
  `newlibc_<name>.golden.txt`.  Same newlibc sources, same minic, only
  the bottom shim and the host differ — exactly the §6h parallel.
- Locked the goldens by copying the verified `build/nl-bm-golden/*.out`
  captures (full, including preamble + result line) to
  `minic/dos/tests/<name>.golden.txt`.

### Budgets / harness facts
- ramfs_test: 103 output lines ⇒ 300 s emulated budget (display scroll
  dominates 8088 time; the §6f lesson, now the longest battery entry).
- terminal_meta_test (36 lines) 90 s, the FAT tests (≤20 lines) 60 s,
  snprintf/stdio_route 45 s — all comfortable.
- No new harness env vars; the ten entries use empty keypost/serial/disk
  fields (`<name>:<secs>:::`).

### Open tracks (new + carried)
- Port upstream FAT WRITE: `vfs_mount_victor_fat_rw` + `fat_write.c`'s
  runtime dispatch install (`vfs_set_fat_write_ops`) — then the six
  bare-metal FAT tests can run read-WRITE against the real `-scsi:0`
  disk instead of RAM/label fixtures.
- run-dos-exe.sh stdin redirect (unlocks `stdin_test`, `scanf_test`).
- scanf-over-cooked-tty; serial TX ISR — both when a consumer appears.
- newlibc-under-far-models stdio story (when a far consumer appears).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

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

Older session headers (§6h and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
