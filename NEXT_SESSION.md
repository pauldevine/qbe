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

# Next session (§6p — continue Phase 6.  §6o [2026-06-13, this session] drove the two keyboard-input newlibc tests **BARE-METAL through the cooked `bm_tty` console, with ZERO compiler/toolchain changes — `stdin_test` (getchar/fgets) and `scanf_test` (scanf) are now standing bare-metal battery entries, 24/24 → 26/26.**  §6n had just gated these same two tests DOS-hosted via a `< IN.TXT` redirect (raw, no echo); this session runs them on the bare machine where they read CON through the interrupt-driven keyboard (IR6) and the cooked console ECHOES the input — so the goldens are necessarily different files from §6n's.  The path is `getchar`/`fgets`/`scanf` → `_read(0,…)` → `console_dev_read` → `bm_tty_read`: in the `bm_shim` FILE layer `fgetc`/`getchar` do `_read(fd,&c,1)`, and `bm_tty_read(buf,1)` returns after exactly ONE keystroke (no Enter needed, the `i==count` loop bound), while `fgets`/`scanf` consume up to the echoed Return/whitespace; all input arrives as a single `V9K_KEYPOST` natkeyboard burst that the keyboard ISR queues in the IR6 ring, so keypost-vs-program timing is irrelevant.  **The one real gotcha (a re-confirmation of the §6h `stdio_bm` flush lesson, NOT a new bug): a `\n` at the very END of a natkeyboard post is not committed to the ring before the program blocks reading it — a throwaway char AFTER the final `\n` is required.**  The first attempt (`Ahello\n`) hung in fgets having echoed `hello` but no newline; `Ahello\nz` (the `z` unused by the test) made it a FIRST-RUN PASS — getchar='A', fgets="hello\n".  `scanf_test` keypost `victor 42\nz` → `%15s`="victor", `%d`=42 (the `\n` ends `%d`, the `z` flushes it), also FIRST-RUN PASS.  Both build small-model (114 KB raw images; no `fat_write.c` bulk, so no medium needed unlike §6k–§6m).  Battery entries `stdin_test:35:Ahello\nz::` and `scanf_test:35:victor 42\nz::`; goldens `minic/dos/tests/{stdin,scanf}_test.golden.txt` captured from clean MAME runs.  Verified `tools/test-newlibc.sh stdin_test scanf_test` → **[ok] [ok]**; the other 24 battery entries are byte-unaffected (only `test-newlibc.sh` + the two new goldens changed), so battery is **26/26**, and with no compiler source touched there is **no emit audit and no MP byte-compare**.  Next: the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; `fat_write` over the real `-scsi:0` disk for the six bare-metal FAT tests (bm_sasi WRITE(6) + `vfs_mount_victor_fat_rw` proven); or pick from the carried open tracks below.)

## §6o session notes (2026-06-13)

### Pure harness plumbing — no compiler change (again)
- `stdin_test`/`scanf_test` build small-model via test-host mode
  (`-Dmain=newlibc_test_main` + `bm_testhost.c`), which already wires up
  `bm_tty_init()` (display + IR6 keyboard) and `bm_stdio_init()` (VFS fds
  0/1/2 → /dev/console).  No build-script or driver change was needed —
  only two new `NEWLIBC_BM_TESTS` entries + two goldens.
- The bare-metal goldens ECHO the typed input (cooked console), so they
  differ from §6n's DOS `< IN.TXT` redirect goldens (raw, no echo).  Both
  are correct for their host; keep them as separate files.

### The flush gotcha (re-confirmed, not new)
- A `\n` at the END of a `V9K_KEYPOST` natkeyboard post is NOT committed
  to the IR6 ring before the program blocks reading it: `Ahello\n` hung in
  fgets having echoed `hello` with no newline.  A throwaway char AFTER the
  final `\n` flushes it — `Ahello\nz`.  This is the §6h `stdio_bm` lesson
  (its keypost was `vx\b9k\nz`, the `z` after the `\n`).  Applies to any
  future keypost-driven test whose last needed byte is the Return.

### Buffering semantics that set the keypost
- `getchar`/`fgetc` (bm_shim) do `_read(fd,&c,1)` → `bm_tty_read(buf,1)`,
  which returns after ONE non-backspace keystroke (the `i < count` loop
  ends at i==1), no Enter required — so getchar consumes exactly one char.
  `fgets`/`scanf` read on until the echoed `\n`/whitespace.  `Ahello\nz`:
  getchar='A', fgets reads "hello\n", `z` left unused.  `victor 42\nz`:
  `%15s`="victor" (stops at space), `%d`=42 (stops at `\n`).

### Open tracks (carried)
- newlibc-under-far-DATA-models (compact/large) stdio — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- `fat_write` over the REAL `-scsi:0` disk read-WRITE for the six
  bare-metal FAT tests (bm_sasi WRITE(6) + `vfs_mount_victor_fat_rw`
  proven).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6n and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
