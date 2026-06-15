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

# Next session (§7n — continue Phase 6 / open compiler tracks.  §7m [2026-06-14, this session] gated the UNMODIFIED upstream `font_test` bare-metal through bm_testhost — the user resumed the Phase-6 newlibc track from the §7l handoff.  **battery 40/40 → 41/41, test-dos UNCHANGED (bare-metal-only gate, like §6q/§6u–§6y/§7i/§7l), ZERO compiler/qbe/emit/minic AND ZERO build-glue changes — just one battery entry + one golden.**  `font_test` is the verbose sibling of §7l's `font_ram_test`: it runs the same `display_init()` → `display_load_fonts()` 8192-byte copy + byte-compare against `victor_font[]` (Test 6, 3429 non-zero bytes, 0 mismatches — the §7l coverage), but ALSO snapshots font RAM **before** and **after** the load (Tests 3/5, the "before" reading the MAME-reset state `00 00 00 00` since the bm_testhost preamble inits only bm_tty/bm_stdio, never display), reports the font geometry (Tests 1/2: 8192 B, 32 B/glyph, 256 glyphs, table 0x0C00–0x2C00), and **dumps the glyph bit patterns** for space/A/'0' (Test 7).  **Its unique codegen over `font_ram_test`** is the glyph render loop — `for (bit = 9; bit >= 0; bit--) putchar((word & (1U << bit)) ? '#' : '.')` — a **VARIABLE left-shift by a loop counter** (the §4r variable-shift-count area) reading `uint16` glyph rows through a `volatile __far` pointer, exercised bug-loud against the deterministic native font shapes (e.g. 'A' rows `0x01E6 .####..##.` etc.).  **No code change at all this session:** `font_test` resolves entirely through the SAME `display_init` → `bm_display_init` alias §7l added to `bm_shim.c` (nothing new to link, the §6u/§6w/§6y/§7l pattern), so unlike §7l (which needed that alias) §7m needed NOTHING — the only diffs are the battery entry `font_test:150:::` and the golden `minic/dos/tests/font_test.golden.txt` (95 lines, preamble + all 7 tests ending `PASS: All font tests completed successfully!` + `Test complete. System halted.`).  **Bug-loud + toolchain-stable:** no timer values anywhere (pure font-RAM byte-compare + deterministic glyph renders), so the golden is run-stable AND a regression is LOUD — a broken load prints `FAIL`/mismatch counts, a corrupted glyph render diffs the `#`/`.` bit patterns.  **`font_layout_test` was DECLINED** (the other printf-verbose font sibling §7l flagged): its only truly-unique codegen is the same `1<<bit` render loop (already covered here) plus trivial constant integer arithmetic (`c+0x60`, `c*32`) exercised corpus-wide, and its ~230 output lines need a ~360-s budget (a 270-s test run only reached Test 3) — a poor trade for a standing gate (decline noted in the entry comment).  SMALL (60,299 B `_TEXT`, under the 64 KB ceiling); bare-metal ONLY (no Victor font RAM on the DOS host); 95 lines at the §6f display-scroll rate need a **150-emulated-second budget** (verified: the run completed all output and idled in `hlt`).  FIRST-RUN PASS on MAME (verified `tools/test-newlibc.sh font_test` → [ok]).  Newlibc bare-metal support glue (NOT compiler/qbe/emit/minic; MP does not link `bm_shim.c`) → **no emit audit, no MP byte-compare**; test-dos provably unaffected (DOS gate links `dos_shim.c`, font_test is bare-metal only).  Next: with the keyboard family (§6w/§6x/§6n/§6o/§6t), PIC mask API (§6y), serial loopback (§7i), and now the full font-loading path (§7l `font_ram_test` + §7m `font_test`) all gated, the remaining ungated phase-3 tests are `interrupt_test` (stays SKIPPED — §6v's `[90,110]` FAIL-window + raw iteration count) and `font_layout_test` (declined above — gateable like §7m at a ~360-s budget if its constant-arithmetic coverage is later wanted).  Other open frontiers unchanged: the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; the carried aoa sub-gaps (file-scope/static multi-decl array-first — a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl) if a consumer appears.  There is NO QBE backend bug currently open and the easy frame-size levers are spent (§7k).)

## §7m session notes (2026-06-14)

### The pick (resumed Phase-6 newlibc gating from the §7l handoff)
- §7l's handoff named the only remaining gateable phase-3 tests as the
  printf-verbose `font_test`/`font_layout_test` (`interrupt_test` stays SKIPPED
  per §6v).  Scouted both (`~/projects/newlibc/phase3_newlib/tests/`, HEAD
  a65d15c): both use `printf` (→ serial) and end in `while(1) hlt`, so both fit
  the §7l hlt-ending bm_testhost shape (run-victor-baremetal captures
  `__V9BEGIN__`→end-of-budget, exits 0 on present `__V9BEGIN__`).

### Why font_test, why NOT font_layout_test
- `font_test` UNIQUE codegen over §7l's `font_ram_test`: the glyph render loop
  `for (bit=9; bit>=0; bit--) putchar((word & (1U<<bit)) ? '#' : '.')` — a
  VARIABLE left-shift by a loop counter (§4r area) over `uint16 __far` glyph
  rows.  font_ram_test only byte-compares; it never renders.
- `font_layout_test` shares that SAME `1<<bit` loop (its `dump_glyph_visual`),
  so its only truly-unique part is constant integer arithmetic (`c+0x60`,
  `c*32`) exercised corpus-wide — marginal.  AND it is ~230 output lines (a
  270-s run only reached Test 3) → ~360-s budget.  Poor trade → DECLINED
  (noted in the entry comment, the §6v interrupt_test decline pattern).

### Zero code change — resolves through the §7l alias
- First (only) potential build error would be `_display_init` undefined, but
  §7l already added `display_init` → `bm_display_init` to `bm_shim.c`.  So
  font_test built+linked SMALL (60,299 B) first try, NO change needed.
- bm_display.c + bm_font_data.c (defines `victor_font`) already link in every
  bm_stdio build; the alias is `--gc-sections`-stripped when unreferenced.

### Gate (bug-loud, toolchain-stable) + determinism note
- Entry `font_test:150:::`; golden `minic/dos/tests/font_test.golden.txt`
  (95 lines, ends `PASS: All font tests completed successfully!` +
  `Test complete. System halted.`).
- Tests 3/5 snapshot font RAM before/after load.  The "before" snapshot reads
  `00 00 00 00` — the bm_testhost preamble inits bm_tty/bm_stdio only, NEVER
  display, so font RAM at 0x0C00 is the MAME-reset state (deterministic).
- No timer values anywhere → golden run-stable; bug-loud (broken load → FAIL +
  mismatch counts; corrupted glyph render → diffed `#`/`.` patterns).
- 95 lines at the §6f scroll rate need 150 s (verified: full output + hlt idle
  within budget; a first 30-s probe truncated mid-Test-7, as expected).
- Battery **40 → 41**.  No emit audit, no MP byte-compare, test-dos UNCHANGED
  (only `tools/test-newlibc.sh` + the new golden changed; bare-metal-only gate).

### ⇒ Next session (§7n): no QBE bug open; pick a NEW capability
- Phase-6 newlibc: `interrupt_test` stays SKIPPED (§6v); `font_layout_test`
  gateable like §7m at a ~360-s budget if its constant-arithmetic coverage is
  later wanted (otherwise the phase-3 bm_testhost-shaped tests are exhausted).
- Carried, await a consumer: far-DATA-model (compact/large) newlibc stdio; aoa
  sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap;
  plain `jmp_buf a, b;` multi-decl).

---

Older session headers (§7l and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
