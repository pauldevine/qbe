# Next session (§6o — continue Phase 6.  §6n [2026-06-13, this session] added **DOS stdin redirect to the test harness and gated the two keyboard-input newlibc tests DOS-hosted — `stdin_test` (getchar/fgets) and `scanf_test` (scanf); test-dos 291/291 → 293/293, with ZERO compiler/toolchain changes.**  These were the two tests parked since §6k/§6l ("`run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`)") because the harness had no way to feed a program input — every prior gate test was output-only.  Both build clean **small-model** (97 KB / 98 KB images; portable stdio, none of the `fat_write.c` bulk that forced medium in §6k), so the only work was plumbing a deterministic input channel through the three harness layers.  The newlibc stdin path is `getchar`/`fgets`/`scanf` → buffered FILE `stdin` → `read()` → VFS → `/dev/console` → `dos_shim.c`'s `console_dev_read` → **INT 21h AH=3Fh on handle 0**; under a DOS `< IN.TXT` redirect that reads the file's bytes raw (no echo, unlike the cooked CON device on real hardware), making the run fully deterministic.  **Three-layer plumbing, all backward-compatible:** (1) `tools/run-dos-exe.sh` gained a `$DOS_STDIN` env var (env, not a positional — the many `run-dos-exe.sh foo.exe [secs]` callers are untouched) that copies the host file in 8.3-safe as `IN.TXT` and rewrites the autoexec to `PROG < IN.TXT > OUT.TXT`; (2) `tools/run-dos-batch.sh` (the real gate path — all DOS cases run in one boot via a TSV manifest + `RUNALL.BAT`) gained an **optional third TAB manifest field** = host stdin file, staged 8.3-safe as `Tnnnn.IN` with the per-program line becoming `Tnnnn.EXE < Tnnnn.IN > Tnnnn.TXT`; two-field legacy entries parse with an empty third field (`IFS=$'\t' read -r exe out stdin` → `stdin=""` → no redirect, byte-identical command line); (3) `tools/test-dos.sh`'s `stage_runtime_case` gained an optional **4th arg** (host stdin file) threaded into that third manifest field, plus a new `for t in stdin_test scanf_test` gate loop.  **Fixtures** `minic/dos/tests/newlibc_stdin_test.stdin.txt` (`Ahello\n` → `getchar()`='A' then `fgets()`="hello\n", PASS: stopped at newline) and `newlibc_scanf_test.stdin.txt` (`victor 42\n` → `scanf("%15s %d")` → word="victor" value=42, PASS); goldens captured under the redirect (no echo, so the typed bytes do not appear interleaved in the output — deterministic and stable).  Both new entries → `[ok]`; **DOS pipeline 293/293** (291 → +2), all prior entries unchanged, the 2-field legacy manifest path re-verified RC=0 against an output-only test (`snprintf_test`) and the new 3-field path RC=0 in a standalone two-entry batch.  **No compiler/toolchain source changed** (git diff = `run-dos-exe.sh` + `run-dos-batch.sh` + `test-dos.sh` + two goldens + two fixtures) → no emit audit, no MP byte-compare.  Next: drive the SAME two tests **bare-metal** through the cooked `bm_tty` console via `V9K_KEYPOST` (the keystrokes-with-Backspace path §6g/§6h already exercise — would make `stdin_test`/`scanf_test` battery entries, NOT a `< IN.TXT` redirect, since on hardware they read CON, not a file); `run-dos-exe.sh`/`run-dos-batch.sh` stdin is now available for any future input-driven DOS gate test; the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6n session notes (2026-06-13)

### Pure harness plumbing — no compiler change
- `stdin_test`/`scanf_test` build small-model unchanged (`scanf_wrappers.c`
  was already in `build-newlibc-test.sh`'s `SUPPORT_TUS`).  The reason they
  were never gated is the harness had no input channel, not a toolchain gap.
- DOS `< IN.TXT` redirect makes AH=3Fh-on-handle-0 read the file raw — no
  echo, so the golden does NOT contain the typed input interleaved with the
  prompts.  That is the deterministic, stable behavior we want for a gate;
  it differs from interactive cooked-CON behavior (which echoes), so these
  goldens are redirect-specific and must be regenerated the same way.

### The three layers (all backward-compatible)
- `run-dos-exe.sh`: `$DOS_STDIN=host/file` env var (NOT a positional — keeps
  every `run-dos-exe.sh foo.exe [secs]` caller working).  Copies in as
  `IN.TXT`, autoexec line becomes `$SHORT_NAME < IN.TXT > OUT.TXT`.
- `run-dos-batch.sh`: optional 3rd TAB field per manifest line = host stdin
  file → staged `Tnnnn.IN` → `Tnnnn.EXE < Tnnnn.IN > Tnnnn.TXT`.  The
  parser rewrite is `IFS=$'\t' read -r exe out stdin`; a 2-field line yields
  `stdin=""` and the redirect string is empty, so the emitted command is
  byte-identical to the old behavior.  Verified: an output-only test in a
  2-field manifest still runs (RC=0, golden match).
- `test-dos.sh`: `stage_runtime_case`'s optional 4th arg → 3rd manifest
  field (`printf '%s\t%s\t%s\n'`, trailing empty tab when absent).

### Gotcha (not a bug)
- A `stdin_test` run with NO `$DOS_STDIN`/no stdin field HANGS — `getchar()`
  blocks on CON for keyboard input that never comes (DONE.TXT never written
  → watchdog kill).  That is correct: these tests REQUIRE input.  Only feed
  them via the redirect; don't add them to any output-only path.

### Open tracks (carried)
- Bare-metal `stdin_test`/`scanf_test` via `V9K_KEYPOST` cooked-`bm_tty`
  input (a battery entry, not a `< IN.TXT` redirect — on hardware they read
  CON).  The Backspace-through-the-ISR path from §6g/§6h already proves it.
- `run-dos-exe.sh`/`run-dos-batch.sh` stdin is now general — any future
  input-driven DOS gate test can use the 3rd manifest field / `$DOS_STDIN`.
- newlibc-under-far-DATA-models (compact/large) stdio story — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- `fat_write` over the REAL `-scsi:0` disk read-WRITE for the six bare-metal
  FAT tests (bm_sasi WRITE(6) + `vfs_mount_victor_fat_rw` proven).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

# Next session (§6n — continue Phase 6.  §6m [2026-06-13, this session] completed **a SECOND medium-model FAT-write gate, both halves, with ZERO toolchain changes: the UNMODIFIED upstream `fat_write_unit_test.c` now PASSES DOS-hosted (`test-dos` 291/291) AND bare-metal through bm_stdio test-host mode (battery 24/24).**  Where §6k/§6l's `fat_write_test` drove the FAT WRITE layer through `vfs_mount_fat_rw` over a RAM/SASI disk, this unit test exercises `fat_write.c`'s primitives DIRECTLY on hand-built RAM volumes — FAT16 entry write/read + both-FAT mirroring + cluster-chain alloc/free + create/write/truncate/unlink/mkdir/rename + ENOSPC, plus FAT12 entries straddling a FAT sector boundary in both parities — so there is no SASI dependency on either host.  Both halves were FIRST-RUN PASS on the same medium support landed in §6k (the `asm_to_omf.py` `split_sym_long = far_data or model == 'medium'` far CODE-pointer static-init fix, minic's `NEAR_DATA()` covering medium, the `libstub_to_exe.py` `near_data_model` `--no-stdio` guard); **no compiler/toolchain source changed** (git diff = docs + three harness scripts + two goldens only) → no emit audit, no MP byte-compare.  The only changes were build/harness plumbing: `tools/build-newlibc-baremetal.sh` gained `--stack-size=N` (mirroring the DOS build — the test's hand-built RAM-volume `media[]` arrays on top of the full bm_stdio driver set push data+bss to ~60.7 KB, so the default 8 KB stack overflows the 64 KB DGROUP; it runs at 4096), and `tools/test-newlibc.sh` grew an optional **seventh** `:<stack>` entry field — the entry parser was rewritten from nested `${x%%:*}`/`${x#*:}` peeling to clean `IFS=: read` field-splitting (no field contains a colon, so the `::` empty-middle gaps are preserved exactly), confirmed equivalent for all 24 entries via a `--show`-style field dump.  DOS gate `newlibc medium (fat_write_unit_test)` (`--model=medium --stack-size=5120`); bare-metal entry `fat_write_unit_test:60::::medium:4096` (RAM-only, no `-scsi:0`).  Goldens: `minic/dos/tests/newlibc_fat_write_unit_test.golden.txt` (DOS) and `minic/dos/tests/fat_write_unit_test.golden.txt` (bare-metal test-host).  Verified this session: DOS-hosted output byte-identical to golden via `run-dos-exe.sh`; bare-metal `test-newlibc.sh fat_write_unit_test` → **[ok]** under MAME.  Phase 6 step 4h now has TWO medium FAT-write gates on both hosts.  Next: `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`); the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6m session notes (2026-06-13)

### Pure plumbing — §6k's medium support already covered it
- `fat_write_unit_test` `#include`s `fat_write.h`, so `build-newlibc-test.sh`'s
  §6k `fat_write.h` probe already pulls `vfs/fat_write.c` + the FAT/VFS/block
  stack — no build-helper change needed.  DOS-hosted EXE: 140,288 bytes
  (body 136,688), data+bss 52,176, links and runs at `--stack-size=5120`.
- Bare-metal: same test-host mode as §6j (`-Dmain=newlibc_test_main` +
  `bm_testhost.c`), RAM volumes only so NO `-scsi:0` disk field.  Its
  `media[]` arrays + the full bm_stdio driver set push data+bss high enough
  that the default 8 KB stack overflows the 64 KB DGROUP → runs at 4096.

### The harness parser rewrite (the one real risk this session)
- Old `test-newlibc.sh` peeled fields with `${rest%%:*}`/`${rest#*:}` and
  detected "no model field" with `[ "$model" = "$disk" ]`.  Adding a seventh
  field made that brittle, so it was rewritten to `IFS=: read -r name secs
  keypost serial_bytes disk model stack <<EOF`.  Safe because NO field value
  contains a colon, so splitting is exact AND empty middle fields (the `::`
  keypost/serial gaps) are preserved.  Verified by dumping the parsed fields
  for all 24 entries: every 5-field entry → model=small/stack=empty, the two
  medium entries → their 6th/7th fields, `::` gaps intact.

### Open tracks (carried)
- `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`).
- newlibc-under-far-DATA-models (compact/large) stdio story — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- `fat_write` over the REAL `-scsi:0` disk read-WRITE for the six bare-metal
  FAT tests (bm_sasi WRITE(6) + `vfs_mount_victor_fat_rw` proven; the unit
  test deliberately stays RAM-only to isolate the `fat_write.c` primitives).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6l and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
