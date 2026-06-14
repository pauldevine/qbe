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

# Next session (§7l — continue Phase 6 / open compiler tracks.  §7k [2026-06-14, this session] **MEASURED the carried "Kw spill-slot sharing" track and declined it as spent — NO code change, working tree clean, nothing committed.**  The user picked Kw spill-slot sharing (the frame-size lever left open by §4w `colorklslots`, whose note read "Kw spill slots still never share (minor lever, open)"), and the right engineering per the house rule *"easy size levers now spent, MEASURE before sub-KB Victor cycles"* was to quantify the payoff BEFORE building a target-general `spill.c` change that would break MP byte-identity.  **Method:** temporary `QBE_KWSLOT_DBG` instrumentation in `spill.c` — counters in `slot()` for narrow (Kw, 1-word = 2 B on i8086) vs wide (Kl/Ks, 2-word) slots carved during spilling, plus a `peak_kw_live(fn)` helper computing peak simultaneous live Kw temps at spill entry (real liveness, the coloring LOWER bound on narrow slots), reported per-function and run over the **full 108-TU MicroPython corpus** (`build/mp-link/*.ssa`, `qbe -t i8086 -m medium`).  **Findings (decisive):** (1) **ZERO wide slots reach `slot()` corpus-wide** — §4w's `colorklslots()` already interference-colors ALL Kl/Ks (the bulk of every frame) before the spill loop, so there is nothing left on the wide path to optimize.  (2) The ONLY recursively-multiplied frame is `mp_execute_bytecode` (per-generator-level via resume recursion — the very reason §4w mattered): its narrow Kw slots are **34 B of a 472 B frame (7 %)**; the other 438 B is colored Kl + alloca fast-locals, which this lever does NOT touch.  Best-case coloring saving ≤ **14 B/level** (nkw=17 vs peaklive=10), and realistically less since the 17 spilled temps don't all overlap the 10-wide peak window.  (3) Every OTHER function's narrow frame is ONE-SHOT, not multiplied — worst `mp_setup_code_state_helper` 41 slots = 82 B (save ≤ 48 B), `mp_format_float` 33 = 66 B, then a long tail averaging ~8 B/fn across 178 functions; shrinking a depth-bounded one-shot stack frame affects neither heap nor code size.  **Cost side:** `slot()` is TARGET-GENERAL, so a Kw-sharing pass risks all four backends (amd64/arm64/rv64/i8086) AND guarantees MP byte-divergence → a mandatory full, slow Victor re-verification.  Returning ≤ 14 B/generator-level + a few hundred bytes of one-shot non-recursive frame for that is a poor trade — confirming both the §4w "minor lever" parenthetical and the house-rule instinct.  **The track is CLOSED as quantifiably spent.**  Instrumentation reverted (`git checkout spill.c`; `make qbe` rebuilt clean; `git diff --stat` empty).  No gate change, no `make check` run needed (no QBE source change persisted), no emit audit, no MP byte-compare.  The measurement is recorded in memory ([[project-7k-kwslot-measured-spent]]) so it is not re-litigated.  **Next: there is NO QBE backend bug currently open and the easy frame-size levers are now exhausted — prefer a NEW capability.**  Candidates: resume **Phase-6 newlibc gating** (the most live frontier — `interrupt_test` stays SKIPPED per §6v; the newlibc-under-far-DATA-models compact/large stdio story waits for a far-DATA consumer; scout newlibc's tree for any remaining `bm_testhost`-shaped upstream test); OR the REMAINING aoa sub-gaps IF a consumer appears (file-scope/static multi-decl array-first `static jmp_buf fa[2], fb[2];` — a grammar PARSE-ERROR gap, not aoa sizing; plain `jmp_buf a, b;` array-typedef-instance multi-decl).)

## §7k session notes (2026-06-14)

### The track (carried from §4w — Kw spill-slot sharing, a frame-size lever)
- §4w `spill.c::colorklslots()` interference-colors the i8086 forced-resident
  Kl/Ks slots; its closing note: "Kw spill slots still never share (minor
  lever, open)."  The user picked it for §7k.
- House rule applied FIRST: *"easy size levers now spent, MEASURE before
  sub-KB Victor cycles."*  A `slot()` change is TARGET-GENERAL and would break
  MP byte-identity → mandatory Victor re-verify.  So: measure the payoff before
  building anything.

### The measurement (temporary `QBE_KWSLOT_DBG`, since reverted)
- `slot()` counters: narrow Kw (1-word = 2 B) vs wide Kl/Ks (2-word) carved.
- `peak_kw_live(fn)`: peak simultaneous live Kw temps at spill ENTRY (real
  liveness) = the coloring lower bound on narrow slots.
- Run over the full 108-TU MP corpus (`build/mp-link/*.ssa`, `-t i8086 -m medium`).

### Findings (decisive — track is SPENT)
- **0 wide slots reach `slot()` corpus-wide** — `colorklslots()` already handles
  all Kl/Ks (the bulk of every frame) optimally.  Nothing left on the wide path.
- **`mp_execute_bytecode`** (the ONLY recursively-multiplied frame): narrow Kw =
  **34 B of a 472 B frame**; best-case saving ≤ **14 B/level** (nkw=17,
  peaklive=10).  The 438 B remainder is colored Kl + alloca — untouched.
- Every other fn is ONE-SHOT: worst `mp_setup_code_state_helper` 41 slots = 82 B
  (≤48 B saveable); avg ~8 B across 178 fns.  No heap/code-size impact.
- ⇒ ≤14 B/generator-level for an all-target-risk, MP-byte-breaking change = poor
  trade.  **Declined.**  Reverted clean (`git checkout spill.c`, tree empty).

### ⇒ Next session (§7l): NO QBE bug open; easy frame levers exhausted
- Prefer a NEW capability.  Most live: **Phase-6 newlibc** — scout for any
  remaining `bm_testhost`-shaped upstream test; `interrupt_test` SKIPPED (§6v);
  far-DATA-model (compact/large) newlibc stdio waits for a far-DATA consumer.
- Remaining aoa sub-gaps IF a consumer appears: file-scope/static multi-decl
  array-first (grammar parse-error gap); plain `jmp_buf a, b;` multi-decl.


---

Older session headers (§7j and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
