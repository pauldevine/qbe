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

# Next session (§7q — continue Phase 6 libstub retirement / open compiler tracks.  §7p [2026-06-14, this session] **WIDENED `--no-libstub` to the MEDIUM model — the two MEDIUM FAT-write tests now run libstub-free, the increment the §7o handoff named first.  test-dos 334 → 336; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  Background: the §7n/§7o libstub-free path was `--model=small`-gated because `qbe_rt.asm` (the `_qbe_*` compiler runtime) and `dos_syscall.asm` (the int86 family) were copied in NEAR form (plain `ret`, incoming args at `[bp+4]`).  Under the MEDIUM model the compiler emits a FAR call to these helpers — verified on the medium `fat_write_test` build: `call far _qbe_div32u` (22×), `call far _qbe_rem32u` (12×), `call far _int86` (2×) — which pushes a 4-byte CS:IP return address and returns via `retf`, so the near-form TUs would corrupt the stack.  **The fix is the documented `libstub_to_exe.py` pattern, in a small dedicated tool — NOT a third hand-authored copy:** a new `tools/near_to_far_rt.py` mechanically rewrites a near-form standalone runtime asm TU to the far-call ABI — (1) bare `ret` (optional trailing comment) → `retf`; (2) every positive `[bp+N]` → `[bp+N+2]` (the far return CS occupies an extra word between saved bp and arg0; `[bp-N]` locals, `[bx+N]` pointer derefs, and `[cs:...]` SMC references are untouched — only `[bp+(\d+)]` matches); (3) the near `_TEXT` code segment is renamed to a UNIQUE far-code segment (`--seg-name`, `QBE_RT_TEXT` / `DOS_SYSCALL_TEXT`, matching `asm_to_omf.py`'s per-module `<BASE>_TEXT` far-code naming) so omf_link keeps it in its own paragraph (its own CS) — near-code `_TEXT` would otherwise coalesce into the single small-model frame.  This keeps the near `qbe_rt.asm`/`dos_syscall.asm` files as the single source of truth (assembled raw for small, transformed for medium at build time), exactly as `libstub_to_exe.py` does for the libstub body (the transform logic — `shift_bp_offset` + the `^ret\b` match — is copied from it).  **`heap.asm` and `dos_libc.c` needed NOTHING:** the BSS heap is near data (DGROUP) in both models, and `dos_libc.c` (memcpy/memset/str*/malloc/free + the std-stream FILE objects) is compiled with `minic -m medium` like every other TU, so its code goes far + data stays near automatically.  **Build glue:** `build-newlibc-test.sh`'s `--no-libstub` small-only guard became `small|medium`; the runtime-objects branch assembles the two TUs raw for small and routes them through `near_to_far_rt.py` for medium before nasm.  **Verification:** `fat_write_test` (medium, `--no-libstub`, stack 5120) links clean — 0 libstub mentions in the map, `_qbe_div32u`/`_int86`/etc. resolve from the new `QBE_RT_TEXT`/`DOS_SYSCALL_TEXT` far-code segments — and runs byte-identical (49 B) to `newlibc_fat_write_test.golden.txt`; `fat_write_unit_test` likewise byte-identical (59 B) to its golden.  Both gated as `newlibc medium libstub-free (<t>)` in `test-dos.sh` (a loop after the libstub medium builds; same `cp`-at-stage-time .exe-overwrite pattern as the small libstub-free loop), each diffing the SAME golden as its libstub build — bug-loud: a wrong far-ABI rewrite corrupts the stack (hang/garbage → diff), an unresolved runtime symbol fails the link.  **FULL gate green test-dos 336/336** (334 + the two medium entries).  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** `near_to_far_rt.py` is all-new; `qbe_rt.asm`/`dos_syscall.asm`/`heap.asm`/`dos_libc.c`/`libstub.asm`/`libstub_to_exe.py`/`crt0_exe.asm`/`omf_link.py` are UNTOUCHED, so MP/stevie/every existing gate provably can't regress (MP NOT rebuilt — links none of these files).  Traps recorded: `_qbe_get_cs` (`mov ax, cs`) returns qbe_rt's OWN segment under a far call — semantically wrong for building far ISR-IVT entries, but it matches the libstub medium behavior (its comment: "far-code callers would need a per-segment answer") and NO DOS-hosted medium consumer uses it (the FAT tests have no `__attribute__((interrupt))` fns; `--gc-sections` may even drop it); the `[bp+N]` regex shifts offsets inside COMMENTS too (harmless, mirrors `libstub_to_exe.py`).  **⇒ Next session (§7q): CONTINUE libstub retirement.**  Remaining increments, in order: (1) the **larger end-state** — retire `libstub_to_exe.py`'s python printf engine for NON-newlibc programs (stevie + any plain minic .EXE still link the full libstub for its str/mem + printf); this means giving those programs a newlibc-style portable stdio or a minic-compiled printf, a bigger lift than the FAT tests (which already use newlibc's printf).  (2) far-DATA models (compact/large/huge) `--no-libstub` — needs `far_stdlib`-aware newlibc stdio + a far-pointer libc fill (the `_far_*` mangling minic does under far-DATA), await a consumer.  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7p session notes (2026-06-14)

### The pick (continued §7o libstub retirement per its handoff)
- §7o's handoff named MEDIUM widening as the next increment: qbe_rt/dos_syscall
  were near-form, so the --no-libstub path was --model=small-gated.  Did it.

### The seam — medium far-calls the runtime helpers
- Confirmed on the existing medium fat_write_test (libstub) build: minic/qbe
  emit `call far _qbe_div32u` / `_qbe_rem32u` / `_int86`.  Far call pushes a
  4-byte CS:IP, returns via retf → near-form qbe_rt/dos_syscall corrupt the
  stack.  So they need: ret→retf, args [bp+4]→[bp+6], own far-code segment.

### The approach — tools/near_to_far_rt.py (not a third copy)
- The libstub_to_exe.py pattern, in a small dedicated tool: keep the near
  .asm files as the single source of truth, generate the far form at build.
  Transform = ret→retf + [bp+N]→[bp+N+2] (copied from libstub_to_exe.py's
  shift_bp_offset/transform) + rename `_TEXT` → a unique far-code segment.
- Segment names QBE_RT_TEXT / DOS_SYSCALL_TEXT match asm_to_omf's per-module
  <BASE>_TEXT far-code naming; omf_link coalesces CODE by NAME, so unique
  names → each its own paragraph (its own CS) for the `call far` fixups.
  Near `_TEXT` would coalesce into the single small-model frame (wrong).
- Only `[bp+(\d+)]` matches, so [bx+N] derefs and [cs:.int_op+1] SMC are
  left alone (verified in the far output).  Both far forms nasm-assemble.

### What needed NOTHING
- heap.asm: BSS heap is near data (DGROUP) in both models — unchanged.
- dos_libc.c: compiled `minic -m medium` like every TU → code far, data
  near automatically (memcpy/str*/malloc/free + the FILE objects).

### Build glue
- build-newlibc-test.sh: --no-libstub small-only guard → small|medium; the
  runtime-objects branch assembles raw for small, transforms for medium.

### Verification
- fat_write_test medium --no-libstub: clean link, 0 libstub in map, runtime
  resolves from QBE_RT_TEXT/DOS_SYSCALL_TEXT; DOSBox output byte-identical
  (49 B) to newlibc_fat_write_test.golden.txt.
- fat_write_unit_test medium --no-libstub: byte-identical (59 B) to golden.
- Gated both as "newlibc medium libstub-free (<t>)"; FULL gate test-dos 336/336
  (334 + 2).  No compiler/qbe/emit/minic source touched → no emit audit, no
  MP byte-compare (near_to_far_rt.py new; build/test scripts only).

### Traps
- _qbe_get_cs (mov ax, cs) returns qbe_rt's OWN segment under a far call —
  wrong for far ISR-IVT entries, but matches libstub medium behavior and no
  DOS-hosted medium consumer uses it (no interrupt-attr fns in the FAT tests).
- The [bp+N] regex shifts offsets in COMMENTS too (harmless, same as
  libstub_to_exe.py).

### ⇒ Next session (§7q): continue libstub retirement
- Larger end-state: retire libstub_to_exe.py's python printf engine for
  NON-newlibc programs (stevie / plain minic .EXEs still link full libstub).
- far-DATA models (compact/large/huge) --no-libstub: far_stdlib-aware stdio +
  far-pointer libc fill, await a consumer.
---

---
Older session headers (§7o and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
