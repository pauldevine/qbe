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

# Next session (§7o — continue Phase 6 libstub retirement / open compiler tracks.  §7n [2026-06-14, this session] **STARTED Phase-6 milestone 6 — libstub retirement — and proved the libstub-free architecture end-to-end for a DOS-hosted newlibc program.**  The user picked "start libstub retirement" (the Phase-6 end-state, ROADMAP §6.6) over the two declined alternatives (gate the marginal `font_layout_test`; proactively tackle a consumer-less carried compiler gap), since the bm_testhost test-gating sweep is exhausted (battery 41/41) and no QBE bug is open.  **Result: `snprintf_test` now builds and runs DOS-hosted with ZERO libstub linked — byte-identical to its existing golden — exercising printf → _write → INT 21h through a runtime assembled entirely from newly-authored objects.  test-dos 320 → 321; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  Background: the `--no-stdio` libstub builds only dropped libstub's *python stdio epilogue* (newlibc's printf replaced it) — the 2884-line `libstub.asm` body was still linked wholesale, supplying str/mem/ctype, the int86 DOS-syscall family, and the irreducible `_qbe_*` compiler-runtime helpers.  This increment splits those into three standalone objects linked **instead of** libstub, via a new `--no-libstub` flag on `build-newlibc-test.sh`: (1) **`minic/dos/qbe_rt.asm`** — the `_qbe_*` compiler runtime (div32u/s, rem32u/s, huge_norm/add/sub/cmp, get_cs) + the shared `UDIVMOD32_BODY` macro, COPIED VERBATIM (near form) from `libstub.asm` lines 38-61 + 2229-2506; (2) **`minic/dos/dos_syscall.asm`** — the INT 21h primitives (int86/intdos/segread/int86x/intdosx), copied verbatim from `libstub.asm` (self-contained: CS-relative SMC + function-local inline `dw` scratch, no shared libstub label); (3) **`minic/dos/newlibc/dos_libc.c`** — the minic-COMPILED libc fill (memcpy/memset/strlen/strcmp/strcpy/memcmp + the std-stream FILE objects `stdin/stdout/stderr`), the actual Phase-6 point: our own compiler builds the libc newlibc itself lacks (phase-3 normally links newlib's libc.a here).  **STRATEGY = COPY, NEVER MUTATE:** all-new files; `libstub.asm`, `libstub_to_exe.py`, `crt0_exe.asm`, `omf_link.py`, and the entire existing `--no-stdio` build path are UNTOUCHED, so MicroPython / stevie / every existing gate provably cannot regress (verified: gate green 321/321, including all the unchanged entries).  Accepted cost: `_qbe_*` + int86 logic now lives in TWO places (libstub.asm AND the new TUs) — documented with cross-reference comments in both new files so a future divide/huge/sign fix is applied to both.  **Build-glue specifics worth recalling:** the new asm TUs are pure code (no DGROUP data — the int86 family's only data is CS-local inline `dw`), so they must NOT declare `group DGROUP _DATA _BSS` (nasm errors "group DGROUP contains undefined segment _DATA" — crt0_exe.asm declares the group for the whole link; a code-only TU just contributes to `segment _TEXT class=CODE align=2 use16`).  The std streams: shiminc `stdio.h` declares `FILE *stdin/stdout/stderr` (POINTERS; `FILE = { int _file; }`) and printf_wrappers' `stream_fd` only uses pointer identity + `->_file`, so three one-word FILE objects carrying fd 0/1/2 suffice (defined in dos_libc.c since libstub no longer provides the sentinels).  Minimal libc surface a simple printf test actually CALLS (verified — no `call _malloc`, no surviving `call .*_far_` in the small-model `.omf.asm`): memcpy/memset/strlen/strcmp/strcpy/memcmp; **NO malloc reached** (snprintf/printf format to a buffer / write directly), so the whole heap question was deferred out of this increment.  Gate: a new `test-dos.sh` entry "newlibc libstub-free (snprintf_test)" builds `snprintf_test --no-libstub` (overwriting the same `build/newlibc-tests/snprintf_test/` path the libstub gate uses, run after it) and diffs the SAME `newlibc_snprintf_test.golden.txt` — bug-loud (a missing runtime symbol fails the link; a wrong `_qbe_*` decimal conversion diffs the golden).  IDE clang flagged the FILE `{ 0 }` inits as int→pointer warnings — a linter false-positive (it uses macOS system headers where FILE's first member is a pointer; the actual build uses `-nostdinc -I shiminc`, compiled+linked+ran clean).  **⇒ Next session (§7o): CONTINUE libstub retirement.**  The obvious next increments, in order of value: (1) **malloc/free + a real heap** — add a BSS `char __heap[N]` exposed via `__heap_start`/`__heap_end` routed through newlibc's existing `_sbrk` (`libgloss/syscalls.c:85-105`) + a thin malloc/free in dos_libc.c, gated by a malloc-using DOS-hosted test; MIND the small-model DGROUP-64KB invariant (`omf_link.py:1290` dies if DGROUP+stack+heap overflows 64KB — code is a separate `_TEXT` segment so it doesn't count, but stack+statics+heap share one 64KB DGROUP).  (2) **widen `--no-libstub` to the rest of the small NEWLIBC_TESTS** (most need only the same six libc fns + maybe a few more str/mem; grow dos_libc.c as undefined-symbol errors appear) to prove generality, then **to medium model** (the qbe_rt/dos_syscall copies are near-form — medium needs far-call ABI, i.e. route them through a libstub_to_exe-style +2/retf rewrite OR author medium variants; this is the documented growth path).  (3) Eventually retire `libstub_to_exe.py`'s python printf engine for non-newlibc programs too (the larger end-state).  Carried, await a consumer (unchanged): far-DATA-model (compact/large) newlibc stdio; the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl).  `font_layout_test` still gateable like §7m at a ~360-s budget if its constant-arithmetic coverage is ever wanted; `interrupt_test` stays SKIPPED (§6v).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7n session notes (2026-06-14)

### The pick (user chose "start libstub retirement")
- Phase-6 bm_testhost test-gating is EXHAUSTED (battery 41/41), no QBE bug open,
  easy frame-size levers spent (§7k).  Offered three directions; user picked the
  Phase-6 end-state (ROADMAP §6.6): retire libstub.
- Investigation established the seam: `--no-stdio` only drops libstub's *python
  stdio epilogue*; the `libstub.asm` body still supplies str/mem/ctype + the
  int86 family + the irreducible `_qbe_*` compiler runtime.  newlibc has NO
  malloc/string of its own (phase-3 links newlib's libc.a — we've been filling
  that gap with libstub).  crt0_exe.asm is clean (externs only `_main`, does its
  own AH=4Ch exit).  A Plan-agent pass de-risked the malloc/heap question (no
  malloc reached by a printf test) and flagged the UDIVMOD32_BODY macro trap.

### What landed (first increment — prove the architecture, no malloc)
- NEW `minic/dos/qbe_rt.asm`: UDIVMOD32_BODY macro (libstub.asm:38-61) + the 8
  `_qbe_*` helpers (2229-2506), verbatim near form.  TRAP: must copy the macro
  too or the div/rem bodies won't assemble.
- NEW `minic/dos/dos_syscall.asm`: int86/intdos/segread/int86x/intdosx, verbatim.
  Self-contained (CS-rel SMC + local inline `dw`).
- NEW `minic/dos/newlibc/dos_libc.c`: minic-compiled memcpy/memset/strlen/strcmp
  /strcpy/memcmp + the std-stream FILE objects (libstub no longer provides the
  `_stdin/_stdout/_stderr` sentinels; printf_wrappers' stream_fd needs them).
- `tools/build-newlibc-test.sh`: `--no-libstub` flag (small-model-only) — links
  crt0 + program + SUPPORT_TUs(+dos_libc) + qbe_rt.obj + dos_syscall.obj, NO
  libstub.  Existing default path untouched.
- `tools/test-dos.sh`: new entry "newlibc libstub-free (snprintf_test)" diffing
  the SAME golden as the libstub build.

### Build-glue traps hit + fixed
- `group DGROUP _DATA _BSS` in a pure-code TU → nasm "undefined segment _DATA".
  Pure-code TUs must NOT declare the group (crt0 declares it for the link); just
  `segment _TEXT class=CODE align=2 use16`.
- First link: undefined `_stdout`/`_stderr` (libstub sentinels gone) + `_strcpy`
  (vfs) + `_memcmp` (fat).  Added all to dos_libc.c.

### Verification + house rules
- Built clean (62,752 B, 15 modules); no `call .*_far_` in the small `.omf.asm`;
  no libstub symbol in the map.  DOSBox run byte-IDENTICAL to the golden.
- Full gate green: **test-dos 321/321** (320 + the new entry).
- NO compiler/qbe/emit/minic source touched (only new asm/C files + build/test
  scripts; MP links none of them) → no emit audit, no MP byte-compare.

### ⇒ Next session (§7o): continue libstub retirement
- Add malloc/free + a real BSS heap via newlibc `_sbrk` (MIND DGROUP-64KB,
  omf_link.py:1290), gated by a malloc-using test.
- Widen `--no-libstub` across the small NEWLIBC_TESTS (grow dos_libc.c per the
  undefined-symbol errors), then to medium (far-call ABI for qbe_rt/dos_syscall).
- Eventually retire libstub_to_exe.py's python printf engine outright.
---
Older session headers (§7m and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
