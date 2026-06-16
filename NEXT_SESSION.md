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

# Next session (NO carried compiler track remains — the function-local `static` MULTI-declarator grammar gap (the last one left after §8g) is now CLOSED.  §8h [2026-06-16, this session] **CLOSED the function-local `static` MULTI-declarator gap — `static int x, y;` / `static int a[3], b;` / `static int a, b[3];` / `static char *p, *q;` / `static char *p, c;` / `static jmp_buf a, b;` inside a function body all PARSED + emitted correctly now; `tools/test-dos.sh` 370 → 373, the fix is a frontend `minic.y` change (3 new helpers + 4 new productions) → no emit audit, MP compact body 689,760 BYTE-IDENTICAL → no Victor run, `make check` green, grammar conflicts UNCHANGED at 115.**  This was NOT aoa-specific (the §8g handoff named it precisely): the `dcls STATIC type IDENT …` and statement-scope `STATIC type IDENT …` rules carried ONLY single-declarator productions, so EVERY multi-declarator static was a hard PARSE ERROR — plain `int` included, not just `jmp_buf`.  **FIX (frontend `minic.y`, all `g_td_arraydim`-gated so a non-array-typedef base emits byte-identically to the existing single-decl rules):** 3 new helpers near `emit_global_sized_array` — (A) `emit_static_local_scalar_or_instance(base, v)` (the plain scalar OR array-typedef instance first declarator; mirrors the `dcls STATIC type IDENT ';'` rule body), (B) `emit_static_local_sized_array(base, v, count)` (aoa-aware sized array, like the statement-scope `STATIC … '[' expr ']' ';'` rule), and (C) `emit_static_local_rest_item(base, n)` (one `ext_decllist` item, dispatched by op tag, with uniform-* peeling via `ebase = (KIND(base)==PTR)?DREF(base):base` so `static char *p, *q;` makes BOTH `char*` and `static char *p, c;` makes `p` a pointer + `c` a `char`; an `F`/`G`/`H` proto item is a `varadd` with no storage; an unsized `A` item or a per-item initializer dies clearly).  Each declarator is emitted as its own mangled file-scope data global via `emit_static_local`.  **4 new grammar productions (conflicts UNCHANGED at 115):** `dcls STATIC type IDENT ',' ext_decllist ';'` + `dcls STATIC type IDENT '[' expr ']' ',' ext_decllist ';'` + the two statement-scope equivalents (plain-first + array-first × dcls + stmt).  Gated by the all-new `static_multidecl_probe` (`minic/dos/examples/static_multidecl_probe.c`, medium+compact+large, program-output model-independent) — bug-loud: on the UNFIXED compiler every probe function is a parse error so the build FAILS; each `static` is exercised for distinct storage + correct type/size + persistence across calls, plus a statement-scope multi-decl.  Golden `tick=11,22,33 / arrfirst=7,14 / scalfirst=106,206 / twoptrs=175 / ptrscalar=105,106,107 / jmpmulti=71,72 / stmtscope=5,10`, byte-identical across all three models.  MP compact body 689,760 byte-identical (MP has no static multi-decls → the new branches never fire), frontend (not emit.c) → no emit audit.  STRATEGY: frontend-only, additive + `g_td_arraydim`-gated; the COPY/ADD-NEVER-MUTATE libstub-free toolchain is untouched → MP/stevie/every gate provably can't regress.  **⇒ Next session: NO carried compiler track remains.**  The huge pointer-arith family is CLOSED (relational §7u + equality §8e); the aoa array-typedef family is CLOSED (block-local §7e/§7j, every instance site §8g, static multi-decl §8h); the §8c trailing-main "quirk" was DISPROVED as a NON-BUG (§8f); the bare-metal phase-3 bm_testhost tests are EXHAUSTED; the libstub-retirement campaign is COMPLETE.  NO QBE backend bug open; easy frame-size levers spent (§7k).  **ONE bounded gap is documented but has NO consumer** (synthetic-but-bug-loud gating if pursued): the INITIALIZED static multi-decl `static int x = 1, y = 2;` is still a parse error — a distinct, larger feature (per-item const-init folding into each data block + a separate `… '=' expr ',' init_decllist ';'` production family); §8h's new productions reject a per-item initializer with a clear die rather than mis-emit it.  With no compiler track left, a natural next direction is a NEW consumer-driven frontier — e.g. resuming Phase 6 newlibc work, or picking up a parked MicroPython feature track — chosen with the user.)

## §8h session notes (2026-06-16)

### The pick
- §8g handoff left exactly ONE carried compiler track: function-local `static`
  MULTI-declarators.  Continued directly (it was the sole remaining track).

### Verified the gap first (house rule: bug-loud before trust)
- Rebuilt minic fresh; confirmed `static int x, y;` / `static int a[3], b;` /
  `static char *p, *q;` / `static char *p, c;` / `static jmp_buf a, b;` are ALL
  parse errors on the unfixed compiler (single-decl forms all parse OK).

### The fix (frontend minic.y, all g_td_arraydim-gated)
- 3 helpers near emit_global_sized_array: emit_static_local_scalar_or_instance
  (plain/instance first decl — mirrors `dcls STATIC type IDENT ';'`),
  emit_static_local_sized_array (aoa-aware sized array — like the stmt-scope
  single rule), emit_static_local_rest_item (one ext_decllist item, dispatch by
  op tag, uniform-* peel via ebase=DREF(base); F/G/H = proto varadd no storage;
  unsized A or per-item init = die).  Each decl → own mangled file-scope global.
- 4 new productions (dcls + stmt × plain-first/array-first).  Conflicts 115.
- Refinement: 'H' (`*ident(par1)` proto) handled alongside 'G' so it registers
  the function type instead of hitting the misleading init-die (unexercised by
  any gated probe / MP, but clean).

### Gate + verification
- NEW minic/dos/examples/static_multidecl_probe.c (medium+compact+large) +
  golden static_multidecl_probe.golden.txt.  Bug-loud: unfixed = parse error,
  build fails.  Output byte-identical across all three models.
- test-dos 370→373 (3 new entries, DOS pipeline 373/373 ok, exit 0).
- make check green; conflicts UNCHANGED 115.
- MP compact rebuilt: image 710,352 / body 689,760 BYTE-IDENTICAL → no Victor.
- minic.y frontend change (not emit.c) → no emit audit.

### ⇒ Next session
- NO carried compiler track remains.  Huge ptr-arith CLOSED (§7u+§8e); aoa
  family CLOSED (§7e/§7j/§8g/§8h); §8f trailing-main DISPROVED; bm_testhost
  tests EXHAUSTED; libstub-retirement COMPLETE; NO QBE backend bug open.
- LEFT (no consumer): INITIALIZED static multi-decl `static int x=1,y=2;` is a
  distinct larger feature (per-item const-init + a separate production family);
  the new productions reject a per-item initializer with a clear die.
- Natural next frontier is consumer-driven (resume Phase-6 newlibc, or a parked
  MicroPython track) — pick with the user.
---

---

Older session headers (§8f and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
