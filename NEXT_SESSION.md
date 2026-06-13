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

# Next session (§6s — continue Phase 6.  §6r [2026-06-13, this session] gated the UNMODIFIED upstream `fat_victor_label_test` BOTH DOS-hosted AND bare-metal through bm_testhost + the bm_stdio/VFS/FAT/block stack, with ZERO compiler/toolchain/build-script changes — **test-dos 293/293 → 294/294, battery 30/30 → 31/31** (lighter even than §6q, which still needed a build-script probe widen; this session needed NOTHING new — the test resolves through `build-newlibc-test.sh` and `build-newlibc-baremetal.sh` unchanged).  Unlike the §6i `sasi_bm` / §6p `sasi_fat_*` tests (raw `-scsi:0` SASI, bare-metal-only), this is **RAM-disk style** — it hand-builds a Victor drive-label + volume-label + FAT12 in a `media[]` array via `block_register_ramdisk` (no disk), so it ALSO runs DOS-hosted, and its bare-metal serial output is line-identical to the DOS golden between the bm_testhost preamble (`pic+timer`/`tty+sti`/`vfs`) and the `test returned 0` result line (the §6j RAM-disk pattern).  It is the **first deterministic golden for the Victor drive-label → volume-label → relative-data-start parse path** (`fat_mount_victor` / `vfs_mount_victor_fat`): the existing RAM-disk FAT tests (fat_bpb/chain/root/dir/file/vfs) all use the standard BPB mounts (`fat_mount`/`vfs_mount_fat`), and the Victor-label path was previously covered ONLY bare-metal-on-real-SASI (§6i, §6p).  It builds SMALL in BOTH hosts (DOS 52,003 B code; bare-metal 60,697 B, under the 64 KB single-`_TEXT` ceiling — no medium, contrast the §6p/§6q read-path tests that pulled dirent.c/block over the ceiling), and bare-metal it emits 9 output lines so a 60-emulated-second budget is ample.  DOS gate: added to the `NEWLIBC_TESTS` array in `tools/test-dos.sh` (golden `minic/dos/tests/newlibc_fat_victor_label_test.golden.txt`, captured CRLF-stripped and verified byte-exact through the run-dos-batch path).  Bare-metal battery: entry `fat_victor_label_test:60:::` (RAM-disk → no `hd`, small, 60 s; golden `minic/dos/tests/fat_victor_label_test.golden.txt` with the testhost preamble), **FIRST-RUN PASS** on MAME, verified `tools/test-newlibc.sh fat_victor_label_test` → **[ok]** end-to-end.  The other 30 battery entries and 293 DOS entries are byte-unaffected (each harness change is one array entry), and with no compiler/qbe/emit/build-script source touched there is **no emit audit and no MP byte-compare**.  Next: the remaining FAT family is now exhausted for the easy wins (`fat_victor_label_test` was the last RAM-disk-style test); the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6r session notes (2026-06-13)

### Nothing new needed — pure battery + DOS-gate plumbing (one entry each + two goldens)
- `fat_victor_label_test` is a RAM-disk test (`block_register_ramdisk`,
  no `-scsi:0`), so it runs BOTH DOS-hosted (added to `NEWLIBC_TESTS` in
  `test-dos.sh`) and bare-metal (added to `NEWLIBC_BM_TESTS` in
  `test-newlibc.sh`).  build-newlibc-test.sh and build-newlibc-baremetal.sh
  already resolve it (the test `#include`s block.h/fat.h/vfs.h, all in the
  portable subset); no build-script or compiler change.
- DOS golden captured via run-dos-exe (CRLF-stripped) and verified
  byte-exact through the actual run-dos-batch path before trusting it.
- Bare-metal golden captured from a clean MAME run (testhost preamble +
  body + `test returned 0`); first-run PASS.

### What this test covers that nothing else did
- The Victor drive-label parse path (`fat_mount_victor` /
  `vfs_mount_victor_fat`): drive-label sector → volume-label sector →
  data-start RELATIVE to the label sector, FAT12 selected by cluster
  count.  The other RAM-disk FAT tests use standard BPB mounts; the
  Victor-label path was bare-metal-on-real-SASI only (§6i `sasi_bm`,
  §6p `sasi_fat_*`).  Now it has a deterministic DOS-hosted golden too.

### Model: SMALL both hosts (no medium)
- DOS 52,003 B code; bare-metal 60,697 B — under the 64 KB `_TEXT`
  ceiling.  Contrast §6p `sasi_fat_dir_test` / §6q `sasi_sector_test`,
  whose dirent.c/block pulls put them over → medium.  RAM-disk +
  read-only Victor-label parse is a lean TU set.

### Open tracks (carried)
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6q and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
