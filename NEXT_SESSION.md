# Next session (the §8i handoff said NO carried compiler track remained, so this session took on the user-chosen Phase-6 frontier **"compile newlib proper"** — scoped (per the user's correction) to `~/projects/newlibc/phase3_newlib`'s OWN sources, NOT external/upstream newlib.  §8j [2026-06-16, this session] **RE-RAN the §6a triage sweep on the current phase3_newlib tree (now 53/66 TUs compile, up from §6a's 46 — the portable subset all lands) and FIXED the one genuine codegen bug it flushed: GCC extended-asm OUTPUT/INPUT operands (`"=r"`/`"=m"`/`"r"`/`"m"` bound to a local) now work END-TO-END; `tools/test-dos.sh` 376 → 381, the triage qbe failure bucket went 4 → 0, `make check` green, emit audit clean (0 clobbers / 124,955 regions — i8086/emit.c changed so the audit was REQUIRED and run), MP compact body 689,760 BYTE-IDENTICAL → no Victor run.**  The triage's 20 failures split into three clean buckets: **qbe (4)** — `display`/`keyboard`/`pic`/`sasi`, the `"=r"`/`"=m"` output-operand bug (the LONE genuine codegen defect); **nasm (8→12 after the fix)** — gas/AT&T mnemonics (`pushw %%es`, `movb`, `$imm`, AT&T operand order) that nasm rejects; **minic-parse (8)** — `interrupts.c` (an ISR-function-pointer parameter declarator), `vshell.c`, and 6 `dos_tests` (Watcom `_asm{}` blocks).  KEY FINDING: the `"=r"` bug was an **incomplete feature**, not a one-line abort — minic parsed extended-asm operands and substituted the local as `[bp-%name]` into the opaque asm string, but (1) QBE's `promote` ABORTED on an output slot (`slot %x is read but never stored to`, mem.c:108 — the asm's write is invisible to QBE dataflow), (2) an INPUT slot was the dual defect (stored-but-IR-never-read → `promote` forwarded it into a register and removed the alloc, leaving `[bp-%name]` dangling), and (3) the i8086 backend printed the asm string VERBATIM — it never resolved `%name` to a real frame offset, so even past the abort `[bp-%name]` reached nasm as an undefined symbol.  The existing `minic/test/asm_clobber_test.c` (same `"=m"(result)` pattern) was NEVER gated, confirming the feature had never worked end-to-end.  **FIX (3 parts, COPY/ADD-style, additive):** (A) **minic.y** — lower a local-Var operand `%0` to the bare temp ref `%name` (was the never-resolved `[bp-%name]` guess; output branch ~5309, input branch ~5323; the `[_glo%d]` global branch left as-is, no consumer); (B) **QBE `mem.c` new pass `asmvol(fn)`** (declared in all.h, called in main.c right before `markvol`, after `filluse`) — scans every `Oasm` instruction's string for `%name` tokens (skips `%%` escapes), matches each to an alloc temp by name, and sets that alloc's `vol=1` so BOTH `promote` (mem.c:65 vol-skip) and `coalesce` (mem.c:275 vol-skip) keep the slot in memory; `markvol` (run next) then propagates the bit to the slot's own readback load/store.  Handles inputs AND outputs uniformly with NO `promote`-logic change; a no-op unless an asm names a slot, so all currently-compiling code (MP/stevie/the gate) is byte-identical; (C) **i8086/emit.c** `Oasm` handler — resolve each `%name` token to `[bp±N]` via `fn->tmp[t].slot` + the existing `slot(SLOT(idx), fn)` offset formatter; a name matching no slot-resident temp (e.g. a gas `%reg` surviving `%%` handling) is emitted verbatim, so the `%%`→`%` path is byte-identical.  Gated by the all-new `asm_output_probe` (`minic/dos/examples/asm_output_probe.c`, small+medium+compact+large+huge, NASM-valid Intel-syntax mnemonics so it compiles end-to-end and runs) — bug-loud: on the unfixed toolchain the build aborts at the QBE stage → no `.exe` → golden diff fails.  Golden `out_r=4660 / out_m=22136 / dbl21=42 / dblr=9320 / PASS`, byte-identical across all five .EXE models (verified each in DOSBox; `out_r` = `"=r"` immediate write, `out_m` = sasi's `"=m"`, `dbl` = `"=r"` output + `"r"` input round trip).  Generated asm confirms the slot survives + resolves: `mov word [bp-10], 0x1234` (the asm write) then `mov ax, word [bx]` reading the SAME `[bp-10]` (the readback).  **WHAT THIS DOES AND DOESN'T BUY for "compile newlib proper":** the fix eliminates the entire qbe failure CLASS — the 4 drivers no longer abort at QBE and advance one full stage to the nasm bucket — but they STILL don't compile end-to-end because their inline asm is gas/AT&T syntax (minic faithfully compiles C but passes asm templates through VERBATIM; it does not translate syntax).  So PASS stayed at 53; the fix is a real codegen-correctness gain and a PREREQUISITE for any ported driver (a nasm-ported `keyboard.c` still uses `"=r"(flags)`), but the real drivers need source porting to Intel/nasm — exactly what the project already did via the hand-mirrored `bm_*.c` ports, and what §6a flagged as "driver asm needs per-target porting" (step-4 work).  STRATEGY: the fix is additive (new QBE pass + emit-handler extension + 2-line minic substitution change), and provably codegen-neutral for everything that compiles today (MP byte-identical; gate's existing entries all unchanged at 381/381).  **⇒ Next session: NO carried compiler track is open again.**  The "compile newlib proper" frontier for phase3_newlib is now portable-subset-COMPLETE (53/66) with the lone codegen bug fixed; the remaining 13 fails are ALL non-compiler-bug porting work — (1) **gas→nasm inline-asm porting/translation** for the 12 nasm-bucket TUs (drivers display/keyboard/pic/sasi/console/timer + board_init + 6 diagnostic tests) — either an AT&T→Intel translation layer in minic (big, speculative) or per-TU source porting (the `bm_*.c` approach); (2) the **ISR-function-pointer parameter declarator** parse gap in `interrupts.c` (`void ISR_HANDLER (*isr)(void)`); (3) **Watcom `_asm{}` blocks** in the 6 phase-1-style `dos_tests` (park or rewrite).  None is a QBE/minic codegen bug.  A natural next direction is consumer-driven: pursue gas→nasm porting if more real phase3 drivers are wanted under minic, OR pick a different frontier (a parked MicroPython feature track) — chosen with the user.)

## §8j session notes (2026-06-16)

### The pick
- §8i handoff: NO carried compiler track.  User (AskUserQuestion) chose the
  Phase-6 frontier "compile newlib proper".  I initially mis-scoped it to
  external/picolibc newlib; user corrected: it is `~/projects/newlibc/
  phase3_newlib`'s OWN sources.

### Triage re-run (foundation = §6a build/newlibc-triage/sweep.sh, still present)
- Current tree: PASS 53/66 (was 46 at §6a — the portable subset all lands now).
  20 fails: qbe(4) display/keyboard/pic/sasi; nasm(8) gas mnemonics; minic(8)
  interrupts.c ISR-fn-ptr param + vshell + 6 Watcom-_asm dos_tests.
- The qbe bucket was the ONLY genuine codegen bug; the rest is gas-syntax
  porting / toolchain-specific source (the §6a "driver asm needs porting"
  conclusion, re-confirmed).

### The bug = an INCOMPLETE feature (not a one-line abort)
- minic substituted a local operand as `[bp-%name]` but: (1) QBE promote ABORTS
  on the output slot (read-never-stored, mem.c:108); (2) input slots are the
  dual defect (stored-never-read-in-IR → promoted into a register, alloc gone,
  `[bp-%name]` dangling); (3) i8086 emit printed the asm string VERBATIM —
  `%name` was never resolved to a frame offset.  `minic/test/asm_clobber_test.c`
  (same pattern) was never gated → the feature never worked end-to-end.

### The fix (3 parts, additive)
- minic.y: local-Var operand `%0` → bare `%name` (not `[bp-%name]`).
- mem.c asmvol(fn): new pass before markvol; scans Oasm strings for `%name`,
  marks the matching alloc vol=1 → promote+coalesce keep it in memory (inputs
  AND outputs, no promote-logic change).  No-op unless an asm names a slot.
- i8086/emit.c Oasm: resolve `%name` → [bp±N] via tmp->slot + slot(); a name
  matching no slot-resident temp is emitted verbatim (%%→% path unchanged).

### Gate + verification
- NEW minic/dos/examples/asm_output_probe.c + golden, gated small+medium+
  compact+large+huge (NASM-valid mnemonics → runs end-to-end).  Bug-loud:
  unfixed aborts at QBE stage → no .exe → diff fails.
- All 5 .EXE models PASS in DOSBox (out_r=4660/out_m=22136/dbl21=42/dblr=9320).
- test-dos 376 → 381/381 ok; make check green; conflicts unchanged (frontend
  change adds no productions — only action edits + a QBE pass).
- emit audit: i8086/emit.c CHANGED → ran it → 0 clobbers / 124,955 regions
  (the mathfns_probe "build failed" lines are pre-existing soft-float link
  gaps in the standalone audit build, zero inline asm, unrelated).
- MP compact body 689,760 BYTE-IDENTICAL → no Victor run.
- Triage re-run after fix: qbe bucket 4 → 0 (drivers advance to nasm bucket,
  8 → 12, now blocked only on gas syntax).  PASS stays 53 (drivers need asm
  porting to compile end-to-end).

### ⇒ Next session
- NO carried compiler track open.  "Compile newlib proper" portable-subset is
  COMPLETE (53/66); the one codegen bug is fixed.  Remaining 13 fails are all
  porting / toolchain-specific, NOT compiler bugs:
  - gas→nasm porting/translation for the 12 nasm-bucket TUs (the bm_*.c path);
  - interrupts.c ISR-function-pointer param declarator parse gap;
  - Watcom `_asm{}` in the 6 dos_tests (park/rewrite).
- The asm-operand fix is a prerequisite for any nasm-ported driver (ported
  keyboard.c still uses "=r"(flags)).
- Next direction is consumer-driven (gas→nasm porting, or a parked MicroPython
  track) — pick with the user.
---

# Next session (NO carried compiler track remains — the INITIALIZED static multi-decl (`static int x = 1, y = 2;`), the lone documented-but-unconsumed gap §8h left open, is now CLOSED.  §8i [2026-06-16, this session] **CLOSED the initialized function-local `static` MULTI-declarator gap — `static int x = 1, y = 2;` / `static int a = 100, b;` / `static int p, q = 5;` / `static char *p = a, *q = b;` / `static long n = 100000L, m = 1;` / `static int x = 1, arr[3];` plus the statement-scope twins all PARSE now, and each scalar/pointer item's constant folds into its own mangled file-scope data block exactly like the single `static T v = init;` form; `tools/test-dos.sh` 373 → 376, the fix is a frontend `minic.y` change (2 new grammar productions + a one-branch tweak to the §8h rest-item helper) → no emit audit, MP compact body 689,760 BYTE-IDENTICAL → no Victor run, `make check` green, grammar conflicts UNCHANGED at 115.**  §8h had closed the UNinitialized static multi-decl (`static int x, y;`) and explicitly left the INITIALIZED form as "a distinct, larger feature (per-item const-init folding + a separate `… '=' expr ',' init_decllist ';'` production family)".  **The fix turned out SMALLER than that forecast — the key finding is that `ext_decl` ALREADY captures rest-item initializers** (`minic.y` ext_decl rules: `IDENT '=' expr` → kr_name_node op 0 with the init expr hung on `n->l`; `'*' IDENT '=' expr` → op `'P'`), so §8h was merely REJECTING a captured init with a `die("initializer in static multi-declarator not supported")`.  No `init_decllist` machinery was needed.  **FIX (frontend `minic.y`, additive):** (1) `emit_static_local_rest_item` — a scalar (`op==0`) or pointer (`op=='P'`) rest item that carries an initializer (`n->l`) now FOLDS it into its own mangled data block via the existing `emit_static_local_init` const-folder (synthesizing a `'V'` ident node) instead of dying; the array (`'A'`/`'B'`) and array-typedef-instance (`op==0 && g_td_arraydim>0`) arms are unchanged (no grammar path gives them an init).  (2) Two new productions for the init-FIRST form (the existing `STATIC type IDENT …` rules capture the first declarator as a bare IDENT, so `IDENT '=' expr` first had no production): `dcls STATIC type IDENT '=' expr ',' ext_decllist ';'` + its statement-scope twin, each emitting the first declarator via `emit_static_local_init($n, $ident, $expr)` then walking `ext_decllist` through the rest-item helper.  Chose `ext_decllist` (not the non-static rules' `init_decllist`) for the rest to match the §8h uninitialized multi-decl rules AND to additionally support pointer items (`static char *p = a, *q = b;` — `init_decl` has no `*` form).  Gated by the all-new `static_multidecl_init_probe` (`minic/dos/examples/static_multidecl_init_probe.c`, medium+compact+large, model-independent program output) — bug-loud: on the UNFIXED compiler every probe function is a parse error so the build FAILS; each initialized `static` is exercised for distinct storage + correct type/size + the initial value AND persistence across calls (a folded static keeps its value between calls).  Golden `twoinit=5,7,9 / initfirst=106,112,118 / uninitfirst=10,15,20 / twoptrs=175 / twolong=200002,300003,400004 / initarr=56,63 / stmtscope=17,19`, byte-identical across all three models.  MP compact body 689,760 byte-identical (MP has no initialized static multi-decls → the new productions/branch never fire), frontend (not emit.c) → no emit audit.  **CONSISTENCY NOTE (pre-existing, deliberately left as-is):** `emit_static_local_init`'s negated-literal fold branch is STALE — it expects a unary `op=='-'` node with `l->op=='N'` and no right child, but `mkneg` produces the BINARY `0 - n` shape (`op=='-'`, `l`=a zero `'N'` node, `r`=the value), so `static int y = -7;` matches NEITHER the `'N'` nor the negated-literal arm and falls to the runtime/stack alloc-and-store init path (functional, re-runs the init each call — not true static-data semantics).  This is TRUE of the SINGLE `static int y = -7;` rule too; the new multi-decl path mirrors the single-decl path EXACTLY.  Not fixed here: improving the fold would change the single-decl rule's codegen (and potentially MP byte output), out of this feature's scope; the probe therefore asserts only shapes that fold cleanly (positive int/char/long literals, `0`, string-literal pointers).  STRATEGY: frontend-only, additive; the COPY/ADD-NEVER-MUTATE libstub-free toolchain is untouched → MP/stevie/every gate provably can't regress.  **⇒ Next session: NO carried compiler track remains** — the huge pointer-arith family is CLOSED (§7u relational + §8e equality), the aoa array-typedef family is CLOSED (block-local §7e/§7j, every instance site §8g, static multi-decl §8h), the initialized static multi-decl is CLOSED (§8i, this session), the §8c trailing-main "quirk" was DISPROVED as a NON-BUG (§8f), the bare-metal phase-3 bm_testhost tests are EXHAUSTED, the libstub-retirement campaign is COMPLETE, NO QBE backend bug is open, and the easy frame-size levers are spent (§7k).  **Bounded gaps that remain but have NO consumer:** an array/struct rest item with a BRACE initializer in a static multi-decl (`static int x = 1, a[3] = {1,2,3};`) — `ext_decl`'s array forms (`'A'`/`'B'`) carry no `'=' gaggr`, so it stays a parse error; and the negated-literal fold consistency point above (a `static T v = -N;` data-block fold, single- AND multi-decl).  A natural next frontier is consumer-driven — resume Phase 6 newlibc work, or pick up a parked MicroPython feature track — chosen with the user.)

## §8i session notes (2026-06-16)

### The pick
- §8h handoff stated NO carried compiler track remained; the lone documented-
  but-unconsumed gap was the INITIALIZED static multi-decl.  User
  (AskUserQuestion) chose to close it.

### Verified the gap first (house rule: bug-loud before trust)
- Rebuilt minic fresh; confirmed `static int x = 1, y = 2;` (+ mixed
  `static int x = 1, y;`, init-not-first `static int x, y = 2;`, pointer
  `static char *p = 0, *q = 0;`, and the statement-scope variant) are ALL
  parse errors on the unfixed compiler.  The non-static `int x = 1, y = 2;`
  parses (the model to mirror).

### Key finding — smaller than the §8h forecast
- `ext_decl` ALREADY captures rest-item initializers: `IDENT '=' expr` →
  op 0, init on `n->l`; `'*' IDENT '=' expr` → op `'P'`.  §8h merely REJECTED
  a captured init with a die.  So no `init_decllist` production family was
  needed — only the init-FIRST form lacked a production.

### The fix (frontend minic.y, additive)
- emit_static_local_rest_item: a scalar (op==0) / pointer (op=='P') rest item
  with `n->l` now folds via emit_static_local_init (synthesize a 'V' ident
  node) instead of dying.  Array / array-typedef-instance arms unchanged.
- 2 new productions for the init-FIRST form (bare-IDENT first declarator can't
  capture `= expr`): `dcls STATIC type IDENT '=' expr ',' ext_decllist ';'`
  + the statement-scope twin.  ext_decllist (not init_decllist) for the rest →
  matches §8h + handles `static char *p = a, *q = b;`.  Conflicts UNCHANGED 115.

### Consistency note (pre-existing, left as-is)
- emit_static_local_init's negated-literal fold is STALE (expects unary
  `'-'`+`l='N'`; mkneg makes binary `0 - n`), so `static int y = -7;` falls to
  the runtime/stack init path — TRUE of the single-decl rule too.  The multi-
  decl path mirrors single EXACTLY; not touched (fixing it would change
  single-decl/MP codegen, out of scope).  Probe asserts only cleanly-folding
  shapes (positive int/char/long, 0, string-literal pointers).

### Gate + verification
- NEW minic/dos/examples/static_multidecl_init_probe.c (medium+compact+large)
  + golden static_multidecl_init_probe.golden.txt.  Bug-loud: unfixed = parse
  error → build fails.  Output byte-identical across all three models.
- test-dos 373→376 (3 new entries, DOS pipeline 376/376 ok).
- make check green; conflicts UNCHANGED 115.
- MP compact rebuilt: image 710,352 / body 689,760 BYTE-IDENTICAL → no Victor.
- minic.y frontend change (not emit.c) → no emit audit.

### ⇒ Next session
- NO carried compiler track remains.  Huge ptr-arith CLOSED (§7u+§8e); aoa
  family CLOSED (§7e/§7j/§8g/§8h); initialized static multi-decl CLOSED (§8i);
  §8f trailing-main DISPROVED; bm_testhost tests EXHAUSTED; libstub-retirement
  COMPLETE; NO QBE backend bug open.
- LEFT (no consumer): array/struct rest item with a BRACE init in a static
  multi-decl (`static int x = 1, a[3] = {1,2,3};` — ext_decl has no `= gaggr`);
  the negated-literal data-block fold consistency point (single- AND multi-decl).
- Natural next frontier is consumer-driven (resume Phase-6 newlibc, or a parked
  MicroPython track) — pick with the user.
---

---

Older session headers (§8h and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
