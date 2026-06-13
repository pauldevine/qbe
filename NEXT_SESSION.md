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

Older session headers (§6m and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
