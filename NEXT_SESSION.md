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

# Next session (§6q — continue Phase 6.  §6p [2026-06-13, this session] ran **three UNMODIFIED upstream SASI-backed FAT tests BARE-METAL on the REAL `-scsi:0` Victor disk through bm_testhost + the full bm_stdio/VFS/FAT stack, with ZERO compiler/toolchain changes — battery 26/26 → 29/29.**  Where §6l's `fatwrite_bm` was a hand-mirrored minic TU, this session runs the upstream tests THEMSELVES (the §6j philosophy applied to the disk/write family): `sasi_fat_smoke_test` (read CONFIG.SYS read-only), `sasi_fat_dir_test` (root + subdir iteration via `vfs_mount_victor_fat`), and the headline `sasi_fat_write_test` — create/write 2000 bytes across clusters / read-back / append / unlink on the real disk via `vfs_mount_victor_fat_rw` + SASI WRITE(6), CONFIG.SYS checked intact before AND after.  All three were **FIRST-RUN PASS** on MAME against a scratch copy of `victor_30mb.img` (the harness `hd` field → `V9K_HARD_DISK` scratch-copy attach, §6i).  These tests can ONLY run bare-metal — the DOS host has no raw SASI — so their goldens (`minic/dos/tests/sasi_fat_{smoke,dir,write}_test.golden.txt`) are captured from the bare-metal run, not diffed against a DOS golden (unlike the §6j RAM-disk tests).  **The single toolchain-adjacent change was a build-script probe:** the upstream tests `#include "sasi.h"` (the upstream API header) while `build-newlibc-baremetal.sh`'s SASI TU probe keyed only on `bm_sasi.h`; widening the `grep` pattern to `sasi\.h` (which matches both, since `bm_sasi.h` is a byte-for-byte API-compatible port and `bm_sasi.c` is the only SASI implementation we ever link) makes the unmodified upstream tests pull `bm_sasi.c` — no compiler/qbe/emit source touched.  **Model lesson (the §6k/§6l 64 KB `_TEXT` ceiling, re-confirmed on the read path):** `sasi_fat_smoke_test` fits small (64,771 B code, just under 65,536), but `sasi_fat_dir_test` adds `dirent.c`/`opendir`/`readdir` and overflows small (66,435 B → wraps → hung after `tty+sti`); it builds MEDIUM (71,819 B, multi-CS).  `sasi_fat_write_test` pulls `fat_write.c` (88,797 B) so it is MEDIUM like `fatwrite_bm`, and its Phase-8 multi-cluster SASI write on the 5 MHz 8088 dominates the budget (240 emulated seconds, the §6f slowness rule).  Battery entries `sasi_fat_smoke_test:60:::hd`, `sasi_fat_dir_test:90:::hd:medium`, `sasi_fat_write_test:240:::hd:medium`; verified `tools/test-newlibc.sh sasi_fat_smoke_test sasi_fat_dir_test sasi_fat_write_test` → **[ok] [ok] [ok]** end-to-end through the battery harness (golden-diff, model + hd + budget fields all exercised).  The other 26 entries are byte-unaffected (the probe change only adds `bm_sasi.c` to sources that include `sasi.h`, none of them newly), and with no compiler source touched there is **no emit audit and no MP byte-compare**.  Next: the remaining read-only FAT family on the real disk if wanted (`sasi_sector_test` raw block, `fat_victor_label_test` is RAM-disk so already-style covered); the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6p session notes (2026-06-13)

### One build-script probe; everything else was already in place
- Upstream SASI tests `#include "sasi.h"`; `bm_sasi.h` is a byte-for-byte
  port (same struct layout/names/constants), and `bm_sasi.c` is the only
  SASI implementation we link.  `build-newlibc-baremetal.sh`'s TU probe
  keyed on `bm_sasi.h`; widening it to `grep -q 'sasi\.h'` (matches both)
  links `bm_sasi.c` for the unmodified upstream tests.  No compiler change.
- `#include "sasi.h"` resolves to upstream `$NL/drivers/sasi.h` (no shim);
  API-compatible with `bm_sasi.h`, so cross-TU linking is sound.
- bm_testhost test-host mode (§6j) + the bm_stdio/VFS/FAT/block stack +
  the §6i `bm_sasi.c` + §6k `fat_write.c` support already cover these;
  the runner already scratch-copies `V9K_HARD_DISK` to `-scsi:0`.

### Model selection (the 64 KB _TEXT ceiling, read path too)
- `sasi_fat_smoke_test`: small, 64,771 B code — JUST under 65,536.
- `sasi_fat_dir_test`: adds dirent.c/opendir/readdir → 66,435 B small,
  OVER the ceiling → small image wraps and HUNG after `tty+sti` (printed
  the preamble, never reached `vfs`).  MEDIUM: 71,819 B, first-run PASS.
  Lesson: a "read-only" test is not automatically small-model.
- `sasi_fat_write_test`: pulls fat_write.c → 88,797 B → MEDIUM like
  `fatwrite_bm`; default 8 KB stack fits (data+bss 52,208).

### These tests are bare-metal-only (no DOS golden)
- They need raw SASI hardware; the DOS host (dos_shim → INT 21h) has no
  `-scsi:0`.  So unlike the §6j RAM-disk tests (line-identical to a DOS
  golden), their goldens are captured from a clean bare-metal MAME run.
  Output is deterministic (fixed PASS lines, fixed sizes), reproducible.

### Open tracks (carried)
- The rest of the real-disk FAT family if wanted: `sasi_sector_test` (raw
  block read, small, hd), `fat_victor_label_test` (RAM-disk — already the
  RAM-volume style, no hd).
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6o and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
