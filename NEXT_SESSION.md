# Next session (§7t — continue Phase 6 libstub retirement / open compiler tracks.  §7s [2026-06-14, this session] **FIXED libstub-free file I/O — stevie now LOADS and SAVES real DOS files.  §7r got stevie running libstub-free but its file I/O was broken (the user hit "New File" on `stevie config.sys` and "Can't open file for writing!" on save); the §7r smoke test only reached the input loop, never a real file.  test-dos 344 → 348; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  ROOT CAUSE: a normal program's `fopen`/`getc`/`fputs` funnel through newlibc's stdio → `_read`/`_write(fp->_file)` → syscalls.c → `vfs_*`, and newlibc's `vfs.c` is the BARE-METAL VFS (FAT-on-block/SASI + ramfs + device table) — it has no backend for a plain DOS path like `config.sys`, so `vfs_open` returned -ENOENT (load → "New File") and could not create (save → the error).  The bare-metal VFS is simply the wrong filesystem for a DOS host, where DOS itself *is* the filesystem.  FIX: a new `minic/dos/newlibc/dos_vfs.c` implementing exactly the `vfs_*` surface syscalls.c/dirent.c/unlink.c/rename.c call (`vfs_open`/`close`/`read`/`write`/`lseek`/`stat`/`fstat`/`isatty`/`unlink`/`rename` + `vfs_init` no-op + dir stubs), each mapped to the matching DOS INT 21h file function (AH=3Dh open / 3Ch create / 3Fh read / 40h write / 3Eh close / 42h lseek / 41h unlink / 56h rename via int86x for ES:DI).  The fd IS the DOS handle: 0/1/2 are DOS's pre-opened CON handles, so the console path (printf's `_write(1)`, getchar's `_read(0)`) is unchanged with no special-casing, and opened files get DOS handles ≥5 — no fd table, no FAT/block/ramfs.  This is what libstub's open/read/write/fopen did directly; here it sits behind newlibc's stdio so the rest of the libstub-free stack (printf_wrappers, the dos_shim FILE layer, dos_libc) is untouched.  `dos_vfs.c` is linked INSTEAD of `vfs.c`+`fat.c`+`block.c` on the `build-example.sh`/`build-stevie.sh` `--no-libstub` path (the "real DOS program" path); `build-newlibc-test.sh` is UNCHANGED — its RAM/FAT-volume tests still use the bare-metal `vfs.c`+`fat.c`+`block.c` (those test the VFS/FAT machinery itself, which DOS-passthrough would bypass).  Dropping fat.c/block.c also shrank stevie's code 123,648 → 84,627 B.  **Gate:** the all-new `minic/dos/examples/dos_file_probe.c` round-trips a real DOS file (fopen"w" + fputs/fputc, fclose, fopen"r" + getc to EOF, remove, reopen→gone), gated FOUR ways like dos_libc_probe — small + medium × {libstub anchor (libstub's own INT 21h fopen/getc), libstub-free (dos_vfs.c)} against ONE golden; both runtimes do real INT 21h file I/O so the round-tripped bytes are identical, all four first-run identical.  **Verified on stevie itself:** `stevie TFILE.TXT` in DOSBox now renders the file content and the status line `"TFILE.TXT" 3 lines, 34 characters` (was "New File"); the dos_file_probe proves the write path (fopen"w"+fputs+fclose) stevie's save uses.  The two existing probes (printf_nolibstub, dos_libc) stay golden-identical on the dos_vfs.c stack (no file I/O → output unchanged).  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** `dos_vfs.c` + `dos_file_probe.c` + golden are all-new; `build-example.sh`/`build-stevie.sh` swapped the support-set TUs on the `--no-libstub` branch only; newlibc's `vfs.c`/`fat.c`/`block.c`/`syscalls.c` and libstub are UNTOUCHED, so the newlibc FAT/VFS gate + MP + the libstub stevie provably can't regress.  Trap recorded: `rename.c` calls `vfs_rename` (so `dos_vfs.c` must provide it — INT 21h AH=56h needs ES:DI for the new name → `segread`+`int86x`); off_t is 16-bit so `vfs_lseek` returns the low word only (fine — stevie reads/writes sequentially).  **CRITICAL SECOND BUG (save was STILL broken; fixed same session):** the user reported load works but `:x`/`:w` on an EXISTING file gave status `"(null)" N lines` with the file UNCHANGED (`:w NEWFILE` and load both worked).  ROOT CAUSE: `struct stat` has DIFFERENT sizes in the two compile regimes that meet at `stat()` — stevie's minic/include `{int st_mode; int st_size; time_t}` (~8 B) vs the newlibc shiminc struct (~30 B, 11 fields) — and `vfs_stat`'s `for(i<sizeof(*st)) p[i]=0` zeroed the ~30 B shiminc size into stevie's ~8 B stack `statbuf` inside `writeit`, OVERFLOWING the stack and clobbering the adjacent `fname` local to NULL → the write went to a garbage name, config.sys untouched, status "(null)".  It only fired when `stat` SUCCEEDS (an existing file); a new file's `stat` fails early (-ENOENT, before the write) — exactly why `:w NEWFILE` worked.  libstub's `_stat` is a `-1` no-write stub, so it never overflowed — why the libstub build worked.  FIX: `vfs_stat`/`vfs_fstat` write NOTHING to `*st`, return status only (stevie uses `statbuf` solely for a no-op `chmod`; the cross-regime `struct stat` has no shared ABI, so any field write lands at the wrong offset anyway).  LESSON: passing a struct by pointer across the minic/include vs shiminc compile regimes is an ABI landmine (sizes/offsets differ) — `stat`/`fstat` were the only such cross-regime struct calls.  Gated by extending `dos_file_probe.c` with `stat_keeps_fname()` (mirrors writeit's frame — a `char *fname` param next to a stack `struct stat`, `stat()` on the existing file, return `fname`); bug-loud — the unfixed overflow sends the probe's `main` into a repeating crash loop (verified by reverting the fix).  Verified on stevie: `stevie IN0.TXT`, edit, `:x` now shows `"IN0.TXT" 3 lines, 14 characters` and the file is correctly changed — load AND save both work.  **⇒ Next session (§7t): CONTINUE libstub retirement.**  (1) **deeper stevie verification** — exercise edit→`:w`→reload, multiple files, `:e`, tags; confirm the SAVE path writes correct bytes on the real DOS disk (the probe + load are proven; a full edit/save/verify cycle is the remaining interactive check).  Watch the carried §7r notes: `system`/`getenv` are no-op stubs (`:!`/`$COMSPEC` won't work), the 32 KB `STEVIE_HEAP_SIZE` caps file size (DGROUP-bounded — now with fat/block gone there is MORE DGROUP headroom, so the heap could grow).  (2) far-DATA models (compact/large/huge) `--no-libstub` — `far_stdlib`-aware stdio + a far-pointer libc fill + a far-pointer `dos_vfs` (buffers would be far → INT 21h needs DS:DX set per-call via int86x), still awaits a consumer.  (3) the ultimate end-state — make `--no-libstub` the default (retire `libstub_to_exe.py`'s python printf engine outright).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps.  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open.)

## §7s session notes (2026-06-14)

### The report
- `tools/build-stevie.sh --exe` (libstub) works; `--no-libstub --exe` broke file
  I/O: `stevie config.sys` → "New File", save → "Can't open file for writing!".
  §7r's smoke test only reached the input loop (stevie reads keys via INT 21h
  AH=07h, not redirectable), so file I/O was never exercised.

### Root cause
- stevie fopen/getc/fputs -> newlibc stdio -> _read/_write(fp->_file) ->
  syscalls.c -> vfs_*.  newlibc's vfs.c is the BARE-METAL VFS (FAT-on-disk +
  ramfs + device table); no backend for a plain DOS path -> open fails.
- Can't modify upstream vfs.c/syscalls.c, and vfs_open has no catch-all hook.

### Fix — dos_vfs.c (DOS INT 21h, replaces vfs.c on the DOS-program path)
- New minic/dos/newlibc/dos_vfs.c implements the vfs_* surface via INT 21h
  (3Dh/3Ch/3Fh/40h/3Eh/42h/41h/56h).  fd = DOS handle: 0/1/2 = CON (console/
  printf path unchanged), files >=5.  No fd table / FAT / block / ramfs.
- Linked INSTEAD of vfs.c+fat.c+block.c on build-example/build-stevie
  --no-libstub.  build-newlibc-test.sh UNCHANGED (RAM/FAT tests need vfs.c).
  Dropping fat/block shrank stevie code 123,648 -> 84,627 B.
- Traps: rename.c calls vfs_rename -> dos_vfs provides it (AH=56h, ES:DI via
  segread+int86x); off_t is 16-bit so vfs_lseek returns low word only (stevie
  is sequential).

### SECOND bug — save STILL broken after the above (fixed same session)
- User: load works, but :x/:w on an EXISTING file -> status `"(null)" N lines`
  and the file is UNCHANGED.  (:w to a NEW file worked; load worked.)
- Root cause: struct stat has DIFFERENT sizes across the two regimes that meet
  at stat() — stevie minic/include {int st_mode; int st_size; time_t} ~8B vs
  newlibc shiminc ~30B (11 fields).  vfs_stat's `for(i<sizeof(*st)) p[i]=0`
  zeroed ~30B into writeit's ~8B stack statbuf -> STACK OVERFLOW clobbered the
  adjacent `fname` local -> NULL -> wrote to a garbage name, file untouched.
  Only fires when stat SUCCEEDS (existing file); a new file's stat fails early
  (-ENOENT, no write) -> why :w NEWFILE worked.  libstub's _stat is a -1
  no-write stub -> never overflowed -> why the libstub build worked.
- Fix: vfs_stat/vfs_fstat write NOTHING to *st, return status only (stevie uses
  statbuf only for a no-op chmod; cross-regime struct stat has no shared ABI,
  so any field write lands at the wrong offset anyway).  LESSON: passing a
  struct by pointer across the minic/include vs shiminc regimes is an ABI
  landmine; stat/fstat were the only such cross-regime struct calls.
- Verified: stevie IN0.TXT, edit (x), :x -> `"IN0.TXT" 3 lines, 14 chars`,
  file CHANGED correctly.  Load + save both work now.

### Gate + verification
- NEW dos_file_probe.c: fopen w + fputs/fputc, fclose, fopen r + getc to EOF,
  stat() on the existing file via stat_keeps_fname() (mirrors writeit's frame:
  char* fname + stack struct stat -> the overflow path), remove, reopen.
  4-way gate (small/medium x {libstub anchor, libstub-free}), one golden, all
  first-run identical.  test-dos 344 -> 348.  Bug-loud: the unfixed overflow
  sends the probe's main into a crash loop (verified by reverting the fix).
- stevie TFILE.TXT in DOSBox loads the file (status `"TFILE.TXT" 3 lines,
  34 characters`).  Existing probes (printf_nolibstub, dos_libc) stay golden
  on the dos_vfs stack.

### ⇒ Next session (§7t)
- Deeper stevie verify (multi-file, tags, large files, edit -> :w -> reload).
- far-DATA --no-libstub (far-ptr dos_vfs + stdio), awaits consumer.
- Ultimate: make --no-libstub the default.
---

# Next session (§7s — continue Phase 6 libstub retirement / open compiler tracks.  §7r [2026-06-14, this session] **RETIRED libstub for STEVIE ITSELF — the full 24-TU editor now builds AND runs libstub-free as a medium .EXE, behind a gateable `dos_libc.c` libc-surface expansion that is gated 4-way.  test-dos 340 → 344; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  §7q proved the libstub-free `build-example.sh` path with a small printf+malloc probe; §7r is the headline end-state target the §7q handoff named — a real, much larger program (a 146 KB libstub stevie) running with NO libstub.  Done GATE-FIRST per the user (stevie can't be auto-gated — it is interactively verified — so the reusable `dos_libc.c` fill it needs lands behind a bug-loud probe + golden first, then stevie is built on the proven fill).  **The `dos_libc.c` fill (the libc newlibc's portable subset lacks, beyond §7n/§7o's mem/str/malloc):** real `strncmp`/`strchr`/`strrchr`/`strcat`/`strncpy`/`strcspn` + the full ctype family (`isalpha`/`isdigit`/`isspace`/`islower`/`isupper`/`toupper`/`tolower`) + `atoi`/`getenv`/`system`/`signal`/`exit`/`chmod`/`mktemp`/`delay`/`sleep` + `getc`/`remove` — each matching **libstub's exact behavior** (the equivalence anchor): the real functions implemented for real; the ones libstub stubs (`atoi`→0, `getenv`→NULL, `system`→0, `signal`→NULL) stubbed identically, so libstub-free stevie stays behavior-identical to the interactively-verified libstub stevie.  **KEY TRAP — the .EXE libstub OVERRIDES the .COM stubs:** `libstub_to_exe.py`'s EXE epilogue replaces libstub.asm's `.COM`-path `getc`(`mov ax,-1`) and `remove`(`mov ax,0`) stubs with REAL implementations (buffered `getc`, real `unlink`), so the .EXE anchor — and stevie — expect working versions; matched by delegating `getc`→`fgetc` and `remove`→`unlink` (the newlibc funcs, FAT/VFS-gated), NOT the `.COM` stub values (and `getc(stdin)` is therefore NOT probed — the real EXE getc would block on console input).  **`rename` is NOT in `dos_libc.c`** — newlibc's `libgloss/rename.c` already provides it, and the medium `fat_write` tests link rename.c, so a `dos_libc.c` copy is a duplicate-public-symbol link error (the regression that briefly reded the final gate; fixed by removing it from dos_libc and adding rename.c to build-stevie's support set).  **Gate:** the all-new `minic/dos/examples/dos_libc_probe.c` (semantic/bucketed results so libstub and libstub-free agree by construction — strncmp SIGN, ctype 1/0, exact toupper/tolower chars, the exact stub returns, strcspn counts, chmod/remove/mktemp) gated FOUR ways in `test-dos.sh` — small + medium × {libstub anchor, libstub-free}, all diffing ONE golden (`dos_libc_probe.golden.txt`); all four FIRST-RUN identical.  **STEVIE:** `build-stevie.sh --no-libstub` mirrors `build-example.sh --no-libstub` (newlibc portable stdio + the dos_libc fill compiled in newlibc's regime, + `qbe_rt`/`dos_syscall`/`heap` runtime, NOT libstub; `--gc-sections` strips the FAT/block code the editor never reaches — 20 segments dead-stripped); `dos_shim.c`'s `main()` now forwards `argc`/`argv` (stevie's K&R `main(argc,argv)` switches on `argv[1][0]` when `argc>1`, so garbage args would crash it — the newlibc tests are `int main(void)` and ignore the extra cdecl args, output-neutral, gate-confirmed); a new `STEVIE_HEAP_SIZE` knob (32 KB default — data+bss 58,206 B + 4 KB stack < 64 KB DGROUP, verified to fit; the editor keeps edited lines in malloc'd memory) sizes the `heap.asm` BSS heap.  Result: stevie builds libstub-free (medium, code 123,648 B multi-CS, image 193,840 B) and its startup screen — the `Empty Buffer` status line + vi `~` tildes + Victor terminal escapes, rendered through the newlibc VFS/console write path — is **BYTE-IDENTICAL** to the libstub baseline's; interactive editing/save verification on Victor/DOSBox is handed to the user (driving it is keyboard-bound — stevie reads keys via INT 21h AH=07h, not redirectable stdin).  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** `dos_libc_probe.c` + golden are all-new; `dos_libc.c`/`dos_shim.c` only grew/forwarded; `build-stevie.sh` gained an additive flag branch (default libstub path byte-unchanged); `libstub.asm`/`libstub_to_exe.py`/`crt0_exe.asm`/`omf_link.py`/`near_to_far_rt.py`/`qbe_rt.asm`/`dos_syscall.asm`/`heap.asm` are UNTOUCHED, so MP/the libstub stevie/every existing gate provably can't regress (MP NOT rebuilt — links none of these files).  Other trap recorded: `link.err` is append-not-truncate, so a stale `undefined symbols` block (an old `_chars`/`_outone` run) misleads — read the LAST block / check the build exit code.  **⇒ Next session (§7s): CONTINUE libstub retirement.**  Remaining, in order: (1) **stevie interactive verification** — drive the libstub-free `stevie.exe` on DOSBox/Victor (open/edit/`:w`/`:q`); confirm file I/O through newlibc VFS+dos_shim works for real editing (the smoke test only reached the input loop).  Watch: `system`/`getenv` are no-op stubs (`:!cmd` shell-out + `$COMSPEC` won't work, matching the libstub baseline), `rename` is now newlibc-real (backup-file rename routes through vfs→dos_shim — verify dos_shim has the rename backend or accept graceful degradation), and the 32 KB heap caps editable file size (bump `STEVIE_HEAP_SIZE`, DGROUP-bounded).  (2) far-DATA models (compact/large/huge) `--no-libstub` — `far_stdlib`-aware newlibc stdio + a far-pointer libc fill (the `_far_*` mangling), still awaits a consumer.  (3) the ultimate end-state — make `--no-libstub` the default (retire `libstub_to_exe.py`'s python printf engine outright), which needs the far-DATA story (2) done + broad re-verification of every model/program.  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7r session notes (2026-06-14)

### The pick (continued §7q libstub retirement per its handoff)
- §7q named "retire libstub printf for stevie ITSELF" as the next increment.
  Asked the user how to approach it given stevie is interactively verified (no
  auto-gate); they chose "gate-first, then stevie".

### The fill — dos_libc.c grew to stevie's whole libc surface
- Surveyed the BUILT stevie SOURCES (not ctags/minix/os2/unix — not built) for
  the libc surface; newlibc printf_wrappers already gives sprintf/fprintf/
  fputs/fputc/puts/fgets, dos_shim gives fopen/fclose/fread/fwrite, syscalls
  gives read/write/open/close + _exit/abort.  Gap → dos_libc.c.
- Matched libstub EXACTLY (equivalence anchor).  Two .EXE-vs-.COM traps: the
  EXE libstub (libstub_to_exe.py) overrides getc (real buffered read, blocks on
  stdin) and remove (real unlink, -1 on missing file) — so getc→fgetc,
  remove→unlink, NOT the .COM mov ax,-1 / mov ax,0 stubs.  Found by the anchor
  HANGING on getc(stdin) and a remove 0/-1 golden diff.

### The gate — dos_libc_probe.c, 4-way, one golden
- Semantic/bucketed output (strncmp SIGN, ctype 1/0, exact case chars, exact
  stub returns, strcspn, chmod/remove/mktemp) so libstub & libstub-free agree
  by construction.  getc/rename/sleep/delay NOT probed (block / undefined AX).
- All 4 (small/medium × anchor/free) first-run identical → golden.  340 → 344.

### Stevie — build-stevie.sh --no-libstub
- Mirrors build-example: -Dmain rename, compile_newlibc_unit support stack +
  rename.c (stevie uses rename), qbe_rt/dos_syscall/heap runtime, --gc-sections,
  crt0 NEAR_CODE for small.  STEVIE_HEAP_SIZE knob (32 KB default, fits DGROUP).
- dos_shim main() now int main(int argc, char **argv) → forwards both (newlibc
  tests ignore the extra cdecl args, output-neutral; gate-confirmed).
- Links libstub-free (medium, code 123,648 B, image 193,840 B); startup screen
  BYTE-IDENTICAL to the libstub baseline (Empty Buffer + ~ tildes).

### Trap (briefly reded the final gate): rename duplicate symbol
- dos_libc.c rename collided with newlibc rename.c (linked by the medium
  fat_write tests).  Fix: drop rename from dos_libc.c (newlibc provides it), add
  rename.c to build-stevie's support set.  Final gate 344/344 green.

### ⇒ Next session (§7s): continue libstub retirement
- Stevie interactive verification on Victor/DOSBox (open/edit/:w/:q).
- far-DATA --no-libstub (far_stdlib stdio + far-ptr libc fill), awaits consumer.
- Ultimate: make --no-libstub the default (retire libstub python printf).
---
Older session headers (§7r and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
