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

# Next session (§7m — continue Phase 6 / open compiler tracks.  §7l [2026-06-14, this session] gated the UNMODIFIED upstream `font_ram_test` bare-metal through bm_testhost — the user resumed the Phase-6 newlibc track.  **battery 39/39 → 40/40, test-dos UNCHANGED (bare-metal-only gate, like §6q/§6u–§6y/§7i), ZERO compiler/qbe/emit/minic changes — one build-glue alias in `bm_shim.c`.**  This is the **first gate of the REAL font-loading path**: `display_init()` → `display_load_fonts()` copies all 8192 bytes of the native `victor_font[]` table to font RAM at `0000:0C00`, then `verify_font_ram()` reads it back and byte-compares against `victor_font[]` (3429 non-zero bytes), printing PASS/FAIL via `printf` (serial-capturable through bm_stdio).  **Unique coverage:** the hand-mirrored `memory_bm` (§6f) only does arbitrary-pattern write-readback of font RAM — it NEVER exercises the real `display_load_fonts()` copy-vs-table correctness, the path every text-mode program on the character-ROM-less Victor depends on.  **Re-examined and overturned a §7i scoping call:** §7i had lumped `font_ram_test` (with memory/segment/simple_screen/font/font_layout/minimal_irq) as "display-only/`hlt`-loop … NOT bm_testhost-shaped," but that was over-conservative for THIS test — `font_ram_test` reports through `printf` (→ serial), and although it ends in `while(1) hlt` (never returns, so bm_testhost's `test returned`/`__V9END__` trailer never prints), `run-victor-baremetal.sh` captures `__V9BEGIN__`→end-of-budget and **exits 0 whenever `__V9BEGIN__` is present** (MAME always runs the full budget — `emu.wait(run_seconds)` then `machine:exit()`, no early-exit on `__V9END__`), so the golden simply ends at the test's own PASS line.  (The truly display-only siblings — segment/memory/simple_screen — use `display_puts` not `printf`, so they stay non-bm_testhost-shaped and covered by the hand-mirrors; `font_test`/`font_layout_test` DO use printf but emit verbose glyph dumps, candidates for a later session if their unique coverage is wanted.)  **The one change (build-glue only):** `bm_shim.c` gained a `display_init` → `bm_display_init` alias (joining its existing `display_puts`/`putc`/`clear`/`set_cursor` surface; `bm_display.c` + `bm_font_data.c` — the latter DEFINES `victor_font` — are already linked into every bm_stdio build, so nothing NEW links, only the wrapper symbol, `--gc-sections`-stripped when unreferenced).  Without the alias the build won't even link (`_display_init` undefined — that WAS the first build error, fixed by the alias, the §6u/§6w/§6y aliasing pattern).  **Bug-loud + toolchain-stable:** the output carries no timer values (pure font-RAM byte-compare), so the golden is run-stable AND a regression is LOUD — broken font loading prints `FAIL: font RAM mismatch count N` and diffs, a missing `display_init` fails the link.  **Gate:** entry `font_ram_test:30:::` in `test-newlibc.sh`, golden `minic/dos/tests/font_ram_test.golden.txt` (7 lines: bm_testhost preamble + the 4 test lines ending `PASS: font RAM matches native Victor table.`).  SMALL (59,563 B `_TEXT`, under the 64 KB ceiling); bare-metal ONLY (the DOS host has no Victor font RAM); 30-s budget; FIRST-RUN PASS on MAME (verified `tools/test-newlibc.sh font_ram_test` → [ok]); additive alias confirmed non-disturbing by re-running `stdio_bm`/`driver_test`/`snprintf_test` → all [ok].  Newlibc bare-metal support glue (NOT compiler/qbe/emit/minic; MP does not link `bm_shim.c`) → **no emit audit, no MP byte-compare**; test-dos provably unaffected (DOS gate links `dos_shim.c`, not `bm_shim.c`).  Next: with the keyboard family (§6w/§6x/§6n/§6o/§6t), PIC mask API (§6y), serial loopback (§7i), and now font loading (§7l) all gated, the remaining ungated phase-3 tests are `interrupt_test` (stays SKIPPED — §6v's `[90,110]` FAIL-window + raw iteration count) and the printf-verbose `font_test`/`font_layout_test` (glyph-dump goldens — gateable like §7l if their coverage is wanted, but very verbose).  Other open frontiers unchanged: the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; the carried aoa sub-gaps (file-scope/static multi-decl array-first — a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl) if a consumer appears.  There is NO QBE backend bug currently open and the easy frame-size levers are spent (§7k).)

## §7l session notes (2026-06-14)

### The pick (resumed Phase-6 newlibc gating)
- §7k closed the last carried frame-size lever (Kw spill-slot, measured spent).
  Per §7k's handoff: NO QBE bug open, easy levers exhausted → prefer a NEW
  capability, most live = scout newlibc for any remaining `bm_testhost`-shaped
  upstream test.
- Scouted `~/projects/newlibc/phase3_newlib/tests/` (HEAD a65d15c).  Cross-
  referenced the 35-entry battery against the phase-3 tree.  §7i's note claimed
  the only ungated test was `interrupt_test` (SKIPPED) + a "display-only/hlt"
  group.  Re-examined that group: `font_test`/`font_ram_test`/`font_layout_test`
  use `printf` (serial-capturable), unlike the `display_puts`-only
  segment/memory/simple_screen.

### Why font_ram_test is in fact gateable (overturning the §7i call)
- It prints PASS/FAIL via `printf` → bm_stdio → serial.  It ends in
  `while(1) hlt` (never returns), so bm_testhost's `test returned`/`__V9END__`
  trailer never prints — BUT `run-victor-baremetal.sh` captures
  `__V9BEGIN__`→end-of-budget (awk `/__V9BEGIN__/{f=1} /__V9END__/{f=0} f`) and
  exits 0 on a present `__V9BEGIN__`.  MAME always runs the full budget
  (`emu.wait(run_seconds)` then `machine:exit()` — no early-exit on `__V9END__`),
  so a hlt-ending test runs its budget and captures cleanly.  Golden ends at the
  PASS line.
- UNIQUE coverage: the real `display_load_fonts()` copy of all 8192 B of
  `victor_font[]` to font RAM @ 0000:0C00, then byte-compare.  `memory_bm` only
  does arbitrary-pattern write-readback — never the real table-vs-RAM path.

### The fix (build-glue only, the §6u/§6w/§6y aliasing pattern)
- First build error: `_display_init` undefined.  bm_display.c exports
  `bm_display_init`; upstream `drivers/display.h` declares `void display_init(void)`.
- Added one alias to `bm_shim.c` display block: `display_init()` →
  `bm_display_init()` (joins display_puts/putc/clear/set_cursor).  bm_display.c +
  bm_font_data.c (defines `victor_font`) already link in every bm_stdio build →
  nothing NEW links, `--gc-sections`-stripped when unreferenced.

### Gate (bug-loud, toolchain-stable) + checks
- Entry `font_ram_test:30:::` in `test-newlibc.sh`; golden
  `minic/dos/tests/font_ram_test.golden.txt` (7 lines, ends
  `PASS: font RAM matches native Victor table.`).
- No timer values → golden run-stable; bug-loud (broken load → `FAIL: font RAM
  mismatch`, missing `display_init` → link failure).  SMALL 59,563 B; bare-metal
  ONLY; 30-s budget; FIRST-RUN PASS.
- Battery **39 → 40**.  Additive alias non-disturbing (re-ran
  stdio_bm/driver_test/snprintf_test → all [ok]).
- Newlibc support glue (bm_shim.c) → NO emit audit, NO MP byte-compare; test-dos
  UNCHANGED (DOS gate links dos_shim.c, not bm_shim.c).

### ⇒ Next session (§7m): no QBE bug open; pick a NEW capability
- Phase-6 newlibc: `interrupt_test` stays SKIPPED (§6v); `font_test`/
  `font_layout_test` gateable like §7l (printf, hlt-ending) but emit verbose
  glyph dumps — do them if their coverage is wanted.
- Carried, await a consumer: far-DATA-model (compact/large) newlibc stdio;
  aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error
  gap; plain `jmp_buf a, b;` multi-decl).

---

Older session headers (§7k and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
