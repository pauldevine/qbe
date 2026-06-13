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

# Next session (§6r — continue Phase 6.  §6q [2026-06-13, this session] ran the UNMODIFIED upstream `sasi_sector_test` raw-block probe **BARE-METAL on the REAL `-scsi:0` Victor disk through bm_testhost + the full bm_stdio/block/SASI stack, with ZERO compiler/toolchain/build-script changes — battery 29/29 → 30/30.**  This is the read-only block-layer counterpart to §6i's hand-mirrored `sasi_bm` minic TU and to the §6p SASI-FAT family: it runs the upstream test ITSELF (the §6j/§6p philosophy applied one layer below FAT).  The test registers the SASI block device, `block_init`s the controller, reads LBA 0 twice with a `block_cache_invalidate` between, verifies the two checksums match (0x8DDD), dumps the first 32 bytes (the `tandon_703_mame` volume label — LBA 0 has no boot-sector signature), and prints the SASI bus/diagnostic state at each phase (geometry 59058 sectors × 512 bytes, flags=0x1).  **FIRST-RUN PASS** against a scratch copy of `victor_30mb.img` (the harness `hd` field → §6i `V9K_HARD_DISK` scratch-copy attach; read-only, so the base image is never touched anyway).  **Model lesson re-confirmed (the §6k/§6l/§6p 64 KB `_TEXT` ceiling, read path AGAIN):** a "read-only" test is NOT automatically small-model — `sasi_sector_test` is 65,577 B code in small, just **41 bytes** over the 65,536 single-`_TEXT` ceiling, so the small image wraps and would hang (the §6p `sasi_fat_dir_test` symptom); it builds MEDIUM at 70,944 B multi-CS and runs clean.  It reads LBA 0 only (no Phase-8 multi-cluster write), so a modest **60-emulated-second** budget suffices despite medium.  Like the §6p SASI tests this can ONLY run bare-metal (the DOS host has no raw SASI), so its golden `minic/dos/tests/sasi_sector_test.golden.txt` is captured from a clean bare-metal MAME run, not diffed against a DOS golden.  **This session needed NOTHING new** — §6p already widened `build-newlibc-baremetal.sh`'s SASI TU probe from `bm_sasi\.h` to `sasi\.h` (this test `#include`s both `block.h` and `sasi.h`, and `bm_sasi.c` is the only SASI impl we link), and bm_testhost test-host mode + the bm_stdio/block/SASI stack already cover the path.  The lone changes are one battery entry (`sasi_sector_test:60:::hd:medium`) + one golden; the other 29 entries are byte-unaffected, and with no compiler/toolchain source touched there is **no emit audit and no MP byte-compare**.  Verified `tools/test-newlibc.sh sasi_sector_test` → **[ok]** end-to-end (medium build + disk scratch-copy + golden diff).  Next: `fat_victor_label_test` is the remaining FAT-label test but it is RAM-disk style (already covered-style, no `hd`); the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6q session notes (2026-06-13)

### Nothing new needed — pure battery plumbing (one entry + one golden)
- `sasi_sector_test` `#include`s `block.h` + `sasi.h`; §6p already widened
  the build-script SASI TU probe to `sasi\.h`, so `bm_sasi.c` links with no
  change.  bm_testhost mode + the bm_stdio/block/SASI stack already cover
  the path.  The only diff is `tools/test-newlibc.sh` + the new golden.

### Model lesson (the 64 KB _TEXT ceiling, read path, AGAIN)
- 65,577 B code in small — 41 B over 65,536.  Small wraps and hangs (the
  §6p `sasi_fat_dir_test` symptom).  MEDIUM: 70,944 B multi-CS, runs clean.
- "read-only" ≠ "small-model".  Third confirmation (`sasi_fat_dir_test`
  was the second).  When a bare-metal newlibc test pulls more than the
  minimal block/FAT/VFS stack, expect to need medium.

### Bare-metal-only (no DOS golden)
- Needs raw SASI; the DOS host (dos_shim → INT 21h) has no `-scsi:0`.
  Golden captured from a clean bare-metal MAME run; read-only + a fixed
  disk label (`tandon_703_mame`) → deterministic (checksum 0x8DDD repeats).

### Open tracks (carried)
- `fat_victor_label_test` — the RAM-disk-style label test (no `hd`, the
  RAM-volume style); low value to add but available.
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6p and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
