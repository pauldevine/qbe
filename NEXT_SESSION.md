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

# Next session (§7r — continue Phase 6 libstub retirement / open compiler tracks.  §7q [2026-06-14, this session] **EXTENDED `--no-libstub` to the ordinary `build-example.sh` path — a NORMAL (non-newlibc) minic program (its own `main()`, compiled in the build-example regime against `minic/include/` headers, the path stevie + plain examples take) now runs libstub-free.  test-dos 336 → 340; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  §7n/§7o/§7p had retired libstub only for the newlibc TEST TREE (built by `build-newlibc-test.sh` against newlibc's own shiminc headers); §7q proves a program built the NORMAL way links libstub-free the same way — the first step toward retiring libstub's python printf engine for stevie.  **Key finding (why it works):** `minic/include/stdio.h` declares `extern int printf()` (K&R, cdecl) — call-compatible with newlibc's `printf` (both cdecl variadic → the symbol `_printf`; under medium the cross-TU `call far _printf` resolves exactly as every medium newlibc test already proves).  So an example compiled in the build-example regime (TURBOC headers) calls `_printf`, which the linker resolves to newlibc's `printf_wrappers.c` definition (printf → `_write` → `vfs_write` → `dos_shim` INT 21h).  The two compile regimes — example = `cpp -D__TURBOC__` + `minic/include`; support TUs = `clang -E -D__ia16__` + shiminc/newlibc headers — meet ONLY at the linker (via `_printf`, `_write`, `_vfs_*`, `_newlibc_test_main`).  **Implementation (`build-example.sh` `--no-libstub`, small|medium only, additive + flag-guarded — the default libstub path is byte-unchanged):** (1) the example TU is compiled with `-Dmain=newlibc_test_main` (threaded via a new `EXAMPLE_DEFS` into the existing `compile_unit` cpp line) so `dos_shim.c`'s `main()` runs `vfs_init()` BEFORE tail-calling the program (printf can't reach the console until the VFS device table is up); (2) a new `compile_newlibc_unit` helper (newlibc regime, mirrors `build-newlibc-test.sh`'s `compile_unit`) compiles the REUSED portable stdio stack (printf_wrappers, scanf_wrappers, syscalls, reent_stubs, dirent, unlink, vfs, fat, block, dos_shim, dos_libc — nothing new authored); (3) the runtime objects are `qbe_rt`/`dos_syscall` (assembled raw for small; rewritten to the far-call ABI by `near_to_far_rt.py` for medium, exactly as §7p) + `heap.asm`, linked INSTEAD of libstub; (4) `--gc-sections` strips the FAT/block stdio code the printf-only program never reaches (12 of 16 modules dead-stripped — 48,943 B code, 68,160 B image small).  **Gate:** the all-new `minic/dos/examples/printf_nolibstub_probe.c` (a plain program — printf `%d`/`%u`/`%x`/`%c`/`%s`/`%ld` + a malloc/free/strcpy round trip), gated FOUR ways in `test-dos.sh` — small + medium × {libstub, libstub-free}, all diffing ONE golden (`minic/dos/tests/printf_nolibstub_probe.golden.txt`).  The libstub build is the EQUIVALENCE ANCHOR (libstub's python printf): a divergent `_qbe_*`/printf conversion reds ONLY the libstub-free entry (pinpointing the regression side), while an unresolved libc symbol fails its link.  All four FIRST-RUN PASS; FULL gate green **340/340**.  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** `printf_nolibstub_probe.c` + its golden are all-new; `build-example.sh`'s change is an additive flag branch (default path byte-identical); `libstub.asm`/`libstub_to_exe.py`/`crt0_exe.asm`/`omf_link.py`/`near_to_far_rt.py`/the existing libstub path are UNTOUCHED, so MP/stevie/every existing gate provably can't regress (MP NOT rebuilt — it links none of these files).  **Trap recorded (cost ~20 min): the stale-binary illusion.**  `libstub.asm` provides `_printf` but NOT `_puts`; the probe first used `puts()`, so the libstub (anchor) build FAILED at LINK (undefined `_puts`) — but the prior libstub-free build's `.exe` was still in `OUT_DIR`, so `run-dos-exe.sh` ran the STALE binary → byte-identical output → it LOOKED like the libstub build had worked.  Lesson: `rm -f` the target `.exe` before a build you mean to verify, and check the build's EXIT CODE, not just the run output.  Fix: the probe uses printf only (no puts), so BOTH runtimes link.  **⇒ Next session (§7r): CONTINUE libstub retirement.**  Remaining increments, in order: (1) **the larger end-state — retire `libstub_to_exe.py`'s python printf engine for stevie ITSELF.**  §7q proved the architecture for a small probe; stevie is a much bigger TU (a 146 KB medium .EXE) and will likely need `build-stevie.sh --no-libstub` plus a review of stevie's TURBOC-isms against the newlibc stdio surface (which functions beyond printf/str/mem it actually calls — grow `dos_libc.c` per the undefined-symbol errors, the §7o pattern).  Mind that stevie's `main(argc, argv)` takes args; the `-Dmain` rename + `dos_shim`'s `main()` would need to forward argc/argv (currently `dos_shim`'s `main()` is argument-less — a small extension, or a stevie-specific shim).  (2) far-DATA models (compact/large/huge) `--no-libstub` — far_stdlib-aware newlibc stdio + a far-pointer libc fill (the `_far_*` mangling minic does under far-DATA); still awaits a consumer.  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7q session notes (2026-06-14)

### The pick (continued §7p libstub retirement per its handoff)
- §7p's handoff named "retire libstub printf for NON-newlibc programs" as the
  next increment.  Asked the user (divergent options); they chose "non-newlibc
  printf, VFS route" — reuse newlibc's proven printf via the VFS console route.

### Finding it was already half-proven, then the real frontier
- malloc_probe (§7o) ALREADY runs a non-newlibc qbe-local program (own main,
  printf via VFS, libstub-free) — but through build-newlibc-test.sh (newlibc
  compile regime, shiminc headers).  The genuinely-NEW frontier: the ORDINARY
  build-example.sh path (TURBOC + minic/include, the stevie path).
- minic/include/stdio.h's `extern int printf()` is cdecl-compatible with
  newlibc's _printf → an example compiled the normal way links against it.

### Implementation — build-example.sh --no-libstub (small|medium, additive)
- -Dmain=newlibc_test_main (new EXAMPLE_DEFS) → dos_shim main runs vfs_init first.
- new compile_newlibc_unit (newlibc regime) for the REUSED portable stdio TUs.
- runtime = qbe_rt/dos_syscall (raw small / near_to_far_rt.py far medium, §7p)
  + heap.asm, NOT libstub; --gc-sections strips the unused FAT/block code.
- exit 77 (not 2) when newlibc tree absent → gate prep() treats as [skip].

### Gate
- NEW printf_nolibstub_probe.c (printf %d/%u/%x/%c/%s/%ld + malloc round trip),
  gated 4× (small/medium × {libstub anchor, libstub-free}), all one golden.
- All four FIRST-RUN PASS; test-dos 336 → 340.  No compiler/qbe/emit/minic
  source touched → no emit audit, no MP byte-compare.

### Trap (cost ~20 min): the stale-binary illusion
- libstub has _printf but NOT _puts.  Probe first used puts() → libstub anchor
  build failed at LINK, but the prior libstub-free .exe was still in OUT_DIR, so
  run-dos-exe ran the STALE binary → identical output → looked like it worked.
- Lesson: rm -f the .exe before verifying, and check the build exit code.
  Fix: printf-only probe → both runtimes link.

### ⇒ Next session (§7r): continue libstub retirement
- Larger end-state: retire libstub's python printf for stevie ITSELF
  (build-stevie.sh --no-libstub; grow dos_libc.c per undefined symbols; forward
  argc/argv through the -Dmain rename — dos_shim's main is currently arg-less).
- far-DATA (compact/large/huge) --no-libstub: far_stdlib stdio + far-ptr libc
  fill, await a consumer.
---
Older session headers (§7q and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
