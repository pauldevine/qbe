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

# Next session (§7p — continue Phase 6 libstub retirement / open compiler tracks.  §7o [2026-06-14, this session] **CONTINUED libstub retirement: widened `--no-libstub` across the ENTIRE small NEWLIBC_TESTS set AND added malloc/free + a real BSS heap — the two increments the §7n handoff named.  test-dos 321 → 334; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  Increment 1 (generality): all 13 small NEWLIBC_TESTS (snprintf + the six FAT/VFS + ramfs + stdio_route + bss + terminal_meta + fat_victor_label + block) now build AND run libstub-free, each DOSBox-verified byte-identical to the SAME golden as its libstub build — proving §7n's 6-function `dos_libc.c` (memcpy/memset/strlen/strcmp/strcpy/memcmp) covers the WHOLE portable FAT/VFS/ramfs/stdio/block surface with ZERO new libc functions (every test built clean on the first try with NO undefined symbols; a clean link wasn't trusted — all 12 were run-verified before gating).  The §7n snprintf-only libstub-free gate entry became a loop over `NEWLIBC_TESTS`.  Increment 2 (malloc): newlibc's portable subset has NO allocator of its own (phase-3 links newlib's libc.a for malloc/free), so `dos_libc.c` gained the canonical K&R free-list `malloc`/`free`, backed by newlibc's own `_sbrk` (`libgloss/syscalls.c`, ALREADY linked in every build — nothing new there) carving from a real **BSS heap** in the all-new `minic/dos/heap.asm`.  The KEY constraint: `_sbrk` brackets the heap with `extern char __heap_start[]/__heap_end[]` and tests `next > __heap_end` by ADDRESS, so `__heap_end`'s address must be exactly end-of-heap — which two separate C arrays can't guarantee, hence a hand-authored asm TU placing `___heap_start: resb HEAP_SIZE` then `___heap_end:` contiguously (C `__heap_start` → asm `___heap_start`, three underscores: C convention + the name's two; verified against `syscalls.omf.asm`'s `extern ___heap_end`).  HEAP_SIZE default 8 KB (data+bss 4322 → 11038 with the heap, far under the small-model 64 KB DGROUP).  **Conflict resolved:** `dos_shim.c` carried §6b 2-byte placeholder `__heap_start`/`__heap_end` (link-satisfaction stubs, "documentedly NOT a usable heap") that duplicate-symbol-collided with heap.asm — now `#ifndef NO_LIBSTUB`-guarded, so the libstub build keeps the stubs but the `--no-libstub` build takes the real heap; `build-newlibc-test.sh` passes `-DNO_LIBSTUB` to every `--no-libstub` compile_unit (via a new `NL_DEFS` var threaded into both the test-TU and support-TU compile calls).  `--gc-sections` drops the whole heap chain (malloc → _sbrk → heap symbols) from any build that never reaches malloc, so the 12 non-malloc tests are byte-unchanged (heap costs them nothing).  Gated by the all-new `malloc_probe` (`minic/dos/newlibc/malloc_probe.c`, a qbe-LOCAL probe, NOT an upstream newlibc test — `build-newlibc-test.sh`'s source resolver gained a `minic/dos/newlibc/$name.c` fallback AFTER the `$NL/tests/$name.c` lookup so the gate calls `build_newlibc_test malloc_probe --no-libstub` naturally and `$t` gives the right .exe path): bug-loud over no-clobber block overlap (8×64 B distinct-stamp blocks, no overlap), free-list reuse keeping live blocks intact (free evens, realloc+restamp, odds unchanged), heap exhaustion (`malloc(60000)` on the 8 KB heap → NULL via `_sbrk`'s `__heap_end` bound), recovery after the failed over-large request, and a string round-trip.  DOSBox-verified golden `noclobber ok / liveintact ok / exhaust ok / string victor 9000 / malloc_probe done` (`minic/dos/tests/malloc_probe.golden.txt`).  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** heap.asm + malloc_probe.c are all-new; dos_libc.c only grew (malloc/free + forward decls + a header-comment refresh); dos_shim.c's change is a guarded OMISSION; `libstub.asm`/`libstub_to_exe.py`/`crt0_exe.asm`/`omf_link.py`/the `--no-stdio` path are UNTOUCHED, so MP/stevie/every existing gate provably can't regress (FULL gate green **334/334**, incl. all 13 libstub-free + malloc_probe + all unchanged entries; MP NOT rebuilt — it links none of these files).  Build-glue traps recorded: a `_BSS`-bearing asm TU MUST declare `group DGROUP _DATA _BSS` + define both segments (the §7n trap is the inverse — a PURE-CODE TU must NOT); `morecore` calls `free` before its definition → forward-declare both; the corpus-wide `word exceeds bounds` nasm warnings (printf_wrappers/vfs/fat/block) are PRE-EXISTING, not from this work.  **⇒ Next session (§7p): CONTINUE libstub retirement.**  Remaining increments, in order: (1) **widen `--no-libstub` to MEDIUM model** — `qbe_rt.asm`/`dos_syscall.asm` are NEAR-form (the `--no-libstub` path is currently `--model=small`-gated in build-newlibc-test.sh); medium needs the far-call ABI, so either route the two asm TUs through a `libstub_to_exe.py`-style +2/retf far-entry rewrite OR author medium variants (the documented growth path).  This would let the medium `fat_write_test`/`fat_write_unit_test` go libstub-free too.  (2) Eventually retire `libstub_to_exe.py`'s python printf engine for non-newlibc programs (the larger end-state — those still link the full libstub).  Carried, await a consumer (unchanged): far-DATA-model (compact/large) newlibc stdio; the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7o session notes (2026-06-14)

### The pick (continued §7n libstub retirement per its handoff)
- §7n proved the libstub-free architecture on ONE test (snprintf).  Its handoff
  named two next increments: malloc/heap, and widening to the rest of the small
  tests.  Did BOTH this session.

### Increment 1 — widen --no-libstub to all 13 small NEWLIBC_TESTS
- Built each with --no-libstub: ALL 13 compiled clean, ZERO undefined symbols.
  The §7n 6-function dos_libc.c already covers the whole FAT/VFS/ramfs/stdio/
  block surface — no growth needed (none of these reach malloc; _sbrk is the
  only heap reference and it sits unused in syscalls.c).
- Clean link not trusted: ran all 12 (snprintf already done by §7n) through
  run-dos-exe.sh and diffed goldens → all byte-identical.
- test-dos.sh: the §7n snprintf-only libstub-free entry became a loop over
  NEWLIBC_TESTS (runs AFTER the libstub loop, overwrites the same .exe path —
  safe because stage_runtime_case `cp`s the exe at stage time).

### Increment 2 — malloc/free + a real BSS heap via _sbrk
- NEW minic/dos/heap.asm: `___heap_start: resb 8192 / ___heap_end:` in _BSS.
  Hand-authored asm is REQUIRED — _sbrk tests `next > __heap_end` by ADDRESS,
  so __heap_end must sit exactly end-of-heap, which two C arrays can't promise.
  Declares `group DGROUP _DATA _BSS` + both segments (a _BSS-bearing TU MUST —
  inverse of §7n's pure-code-TU-must-NOT trap).
- dos_libc.c: canonical K&R free-list malloc/free calling _sbrk (morecore).
  Forward-declare malloc/free (morecore calls free before its definition).
- CONFLICT: dos_shim.c's §6b 2-byte placeholder __heap_start/__heap_end
  duplicate-collided with heap.asm.  Guarded `#ifndef NO_LIBSTUB`; build script
  passes -DNO_LIBSTUB to every --no-libstub compile_unit (new NL_DEFS var).
- --gc-sections drops malloc→_sbrk→heap from non-malloc builds → the 12 other
  tests' data+bss unchanged (4322 B); only malloc_probe pays the 8 KB.
- NEW minic/dos/newlibc/malloc_probe.c (qbe-local, not upstream): no-clobber /
  free-list reuse / heap exhaustion (malloc(60000)→NULL) / recovery / string.
  build-newlibc-test.sh resolver gained a minic/dos/newlibc/ fallback so the
  gate calls `build_newlibc_test malloc_probe` by name.
- DOSBox golden: noclobber ok / liveintact ok / exhaust ok / string victor 9000
  / malloc_probe done.

### Verification + house rules
- FULL gate green: **test-dos 334/334** (321 + 13 libstub-free widening + 1
  malloc_probe; net +13).
- NO compiler/qbe/emit/minic source touched (heap.asm/malloc_probe.c new;
  dos_libc.c/dos_shim.c are newlibc-support C; build/test scripts) → no emit
  audit, no MP byte-compare (MP links none of these files).
- Corpus-wide nasm `word exceeds bounds` warnings are PRE-EXISTING.

### ⇒ Next session (§7p): continue libstub retirement
- Widen --no-libstub to MEDIUM (qbe_rt/dos_syscall are near-form → far-call ABI:
  +2/retf rewrite or medium variants); unblocks the medium fat_write tests.
- Eventually retire libstub_to_exe.py's python printf engine for non-newlibc.

---
Older session headers (§7n and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
