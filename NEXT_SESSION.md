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

# Next session (§7k — continue Phase 6 / open compiler tracks.  §7j [2026-06-14, this session] closed the carried **bounded array-of-array-typedef gap (§7e)** — the brace-init and multi-declarator aoa forms the §7e single-declarator fix had left open — the user picked it and chose to do BOTH forms.  **Background:** §7e made `jmp_buf bufs[N];` (single-declarator, uninitialised; file-scope / block-local / static-local) work by registering an array-of-array-typedef as `IDIR(elem)` with a `var_aoa_dim`=D flag and desugaring a one-level subscript `bufs[i]` to the bare pointer-add `bufs + i*D` (the row address, no deref) via `mkidx` (minic.y ~5318); but two declarator SHAPES still ignored the typedef's inner dimension `g_td_arraydim` entirely.  **(1) MULTI-DECLARATOR aoa** (`jmp_buf a[2], b[2];`, block-local): each declarator reached the multi-decl sized-array (`'B'`) branch of `emit_local_multi_decl_full` (minic.y ~6646), which sized the slot `count*sizeof(elem)` (4 B) instead of `count*D*sizeof(elem)` (32 B) and never set the aoa flag — so `b[i]` was lowered as a SCALAR `loadw` (value-as-pointer) rather than the row address, and `setjmp(b[i])` ran through a garbage pointer (the unfixed probe `alloc4 4` + `%t = loadw (b + i*2)` confirmed it).  **(2) BRACE-INIT 2-D table** (`row3_t t[2] = {{1,2,3},{4,5,6}};` where `typedef int row3_t[3]`): minic has NO true `int[N][3]` (`int x[2][3]` is a hard parse error), so a typedef element is the ONLY way to write a 2-D constant table — and the four local array brace-init rules (dcls sized/unsized + stmt sized/unsized) sized the element as `sizeof(elem)` and stored each top-level item with a single scalar `expr()`, so a nested `{…}` row aborted the compiler (Abort trap 6).  **The fix (all in minic.y, frontend only):** (a) the `emit_local_multi_decl_full` `'B'` branch now reads `aoa = g_td_arraydim` and, when >0, sizes `count*D*sizeof(elem)` + calls `var_set_aoa_dim` (gated `aoa>0` → non-aoa multi-decls byte-identical); (b) a new `static Node *mk_aoa_array_init(v, initlist, dim, zerofill, rows, *out_rows)` helper flattens each `{…}` row into per-element stores `*(v + (r*dim + c))` — built as RAW `'@'(+ V off)` nodes that BYPASS `mkidx` (so the linear index is NOT re-multiplied by D; the bare `'+'` Scale scales by `sizeof(elem)` since `v` decays to `IDIR(elem)`), with row-aligned braced rows + a linear-fill fallback for brace elision + an optional `N*dim` zero-fill; (c) all four brace-init rules (dcls sized ~8461, dcls unsized ~8547, stmt sized ~8980, stmt unsized ~8999) gained an `aoa>0` branch that sizes `N*D*sizeof(elem)`, registers `IDIR(elem)`, calls `var_set_aoa_dim`, and inits via `mk_aoa_array_init` (dcls context `expr()`s the chain at parse time; stmt context defers it as an `Expr` stmt for control-flow order) — every `aoa==0` path left textually unchanged.  **Scope deliberately bounded:** only the BLOCK-LOCAL multi-decl form (the user's named `jmp_buf a[2], b[2]` target, which reaches `emit_local_multi_decl_full` via the §7c array-first stmt/dcls rule) was fixed AND gated; the FILE-SCOPE / function-local-STATIC multi-decl array-first forms (`static jmp_buf fa[2], fb[2];`) are a SEPARATE, PRE-EXISTING **parse-error** gap (no such grammar production — confirmed `parse error` on the unfixed AND fixed compiler), NOT an aoa sizing bug, so `emit_local_multi_decl`'s `'B'` branch and the file-scope `ext_decllist` `'B'` branch were left untouched (no parseable consumer to gate them bug-loud, per the "only fix what you gate" house rule); the plain `jmp_buf a, b;` (array-typedef instance, not array-OF) multi-decl also stays a bounded gap.  **Gated bug-loud:** new `minic/dos/examples/aoa_extended_probe.c` (block-local multi-decl `jmp_buf a[2],b[2]` cross-frame longjmp through BOTH declarators → `md=10,11,20,21`; dcls-context sized 2-D table `t1`; stmt-context sized `t2` + unsized `t3`; write-back-through-indexed-rows `t1x2sum=42`) + golden `minic/dos/tests/aoa_extended_probe.golden.txt`, wired `:medium :compact :large` (matching `arr_jmpbuf_probe`).  Bug-loud confirmed: the unfixed compiler **aborts** on this probe (multi-decl scalar-load + nested-brace abort); the three model builds produce byte-identical correct output.  **test-dos 317/317 → 320/320** (`320/320 ok`, every prior entry unchanged).  Toolchain checks: `make check` green; grammar conflicts UNCHANGED at **115 shift/reduce, 0 reduce/reduce** (no new productions — the nested `{…}` already parses as an `inititem`; only actions changed); **MP compact body EXACTLY 731,088 bytes, byte-identical** to the documented golden (image 751,664 = header 20,576 + body 731,088) → codegen unchanged → no Victor run; and since this is a `minic.y` FRONTEND change (NOT `i8086/emit.c` or middle-end) the emit-bracket audit was NOT required.  The "bounded aoa init/multi-declarator gap (§7e)" open track is now CLOSED for the parseable forms.  Next: pick a carried track — **Kw spill-slot sharing** (frame-size lever, no consumer pain); the REMAINING aoa sub-gaps (file-scope/static multi-decl array-first — a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl — array-typedef-instance decay) if a consumer appears; OR resume **Phase-6 newlibc gating** — `interrupt_test` stays SKIPPED (§6v), the newlibc-under-far-DATA-models (compact/large) stdio story waits for a far-DATA consumer.  There is NO QBE backend bug currently open.)

## §7j session notes (2026-06-14)

### The gap (carried open track — §7e bounded aoa forms)
- §7e closed single-declarator uninitialised aoa (`jmp_buf bufs[N];`) via
  `var_aoa_dim` + `mkidx` (row-address desugar).  Two declarator SHAPES still
  ignored the typedef inner dim `g_td_arraydim`:
  - **multi-declarator** `jmp_buf a[2], b[2];` — each `'B'` declarator sized
    `count*sizeof(elem)` (4 B) not `count*D*sizeof(elem)` (32 B), no aoa flag →
    `b[i]` was a scalar `loadw` (value-as-pointer); `setjmp(b[i])` ran through
    garbage.
  - **brace-init 2-D table** `row3_t t[2] = {{1,2,3},{4,5,6}};` — minic has no
    real `int[N][3]` (hard parse error), so a typedef element is the ONLY 2-D
    table; the four local brace-init rules sized `sizeof(elem)` and stored each
    item with one scalar `expr()` → a nested `{…}` row Abort-trap-6'd the
    compiler.

### The fix (minic.y, frontend only — all `aoa>0`-gated + additive)
- `emit_local_multi_decl_full` `'B'` branch (~6646): `aoa = g_td_arraydim`;
  size `count*SIZE(elem)*(aoa?aoa:1)`; `var_set_aoa_dim(v, aoa)` when aoa>0.
- New `mk_aoa_array_init(v, initlist, dim, zerofill, rows, *out_rows)`
  (~after `mk_local_array_init`): flattens braced rows into RAW `'@'(+ V off)`
  stores (BYPASSES `mkidx` so the linear index is NOT ×D again; the bare `'+'`
  Scale scales by `sizeof(elem)` via `v`'s `IDIR(elem)` decay), row-aligned
  rows + linear-fill fallback + optional `N*dim` zero-fill.
- All four brace-init rules — dcls sized (~8461), dcls unsized (~8547), stmt
  sized (~8980), stmt unsized (~8999) — gained an `aoa>0` branch: size
  `N*D*sizeof(elem)`, register `IDIR(elem)`, `var_set_aoa_dim`, init via
  `mk_aoa_array_init` (dcls `expr()`s at parse time; stmt defers as `Expr`).
  Every `aoa==0` path left textually unchanged → non-aoa byte-identical.

### Scope (deliberately bounded — "only fix what you gate")
- FIXED + GATED: block-local multi-decl `jmp_buf a[2], b[2]` (reaches
  `emit_local_multi_decl_full` via the §7c array-first rule) + block-local
  brace-init 2-D tables (dcls + stmt, sized + unsized).
- LEFT (separate gaps, no parseable/realistic consumer): file-scope /
  static multi-decl array-first (`static jmp_buf fa[2], fb[2];`) is a
  PRE-EXISTING grammar **parse-error** gap (verified `parse error` unfixed AND
  fixed), not aoa sizing — so `emit_local_multi_decl`'s `'B'` branch and the
  file-scope `ext_decllist` `'B'` branch were NOT touched; plain
  `jmp_buf a, b;` (array-typedef instance) multi-decl also stays bounded.

### Gate (bug-loud) + checks
- `minic/dos/examples/aoa_extended_probe.c` + golden
  `minic/dos/tests/aoa_extended_probe.golden.txt`, wired `:medium :compact
  :large` (matching `arr_jmpbuf_probe`).  Output: `md=10,11,20,21` /
  `t1=1,2,3,4,5,6` / `t2=10,20,30,40,50,60` / `t3=7,8,9,11,12,13` /
  `t1x2sum=42` / `done`.
- Bug-loud confirmed: unfixed compiler ABORTS (scalar-load multi-decl +
  nested-brace abort); all three model builds byte-identical.
- **test-dos 317 → 320**.  `make check` green.  Conflicts UNCHANGED
  (115 s/r, 0 r/r — no new productions, only actions).
- `minic.y` FRONTEND change → NO emit audit.  **MP compact body 731,088 B
  byte-identical** → no Victor run.

### ⇒ Next session (§7k): carried tracks (no QBE bug currently open)
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Remaining aoa sub-gaps IF a consumer appears: file-scope/static multi-decl
  array-first (grammar parse-error gap); plain `jmp_buf a, b;` multi-decl
  (array-typedef-instance decay).
- Phase-6 newlibc: `interrupt_test` stays SKIPPED (§6v); far-DATA-model
  (compact/large) newlibc stdio waits for a far-DATA consumer.

---

Older session headers (§7j and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
