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

# Next session (the aoa array-typedef-INSTANCE gaps are now CLOSED for every reachable declaration site; ONE bounded grammar gap remains — function-local-`static` MULTI-declarators, which is a GENERAL grammar hole, not aoa-specific.  §8g [2026-06-16, this session] **CLOSED the array-typedef-INSTANCE declaration gaps — `jmp_buf env`-style plain instances (jmp_buf is `int[8]`) are now sized + decayed correctly at every file-scope / static-local / multi-declarator site, and the file-scope array-FIRST multi-decl (`static jmp_buf fa[2], fb[2];`) now PARSES; `tools/test-dos.sh` 367 → 370, the fix is a frontend `minic.y` change (g_td_arraydim-gated + one new grammar production) → no emit audit, MP compact body 689,760 BYTE-IDENTICAL → no Victor run, `make check` green, grammar conflicts UNCHANGED at 115.**  The carried "aoa sub-gaps" track turned out BROADER than the §8e handoff named: a survey of all `jmp_buf` declaration shapes found that ONLY block-local single-decl (`jmp_buf env;`, §7e) and block-local bracketed multi (`jmp_buf a[2], b[2];`, §7j) were correct — SIX shapes were broken: (1) file-scope single instance `static jmp_buf g;` (mis-sized 2 B + scalar `loadw` instead of 16 B array decay), (2) file-scope multi instance `static jmp_buf a, b;`, (3) file-scope array-FIRST aoa `static jmp_buf fa[2], fb[2];` (a hard PARSE ERROR — the named GAP1), (4) block-local multi instance `jmp_buf a, b;` (the named GAP2), (5) static-local single instance `static jmp_buf s;` (in a fn).  Root cause for (1)/(2)/(4)/(5): every plain-instance / multi-declarator emission site treated an array-typedef instance as a single scalar of the ELEMENT type (`int`, 2 B, value-loaded) instead of the whole D-wide array, because only the block-local single-decl rule (`dcls type IDENT ';'`, minic.y ~8236) had the `g_td_arraydim > 0` branch.  **FIX (frontend minic.y, all g_td_arraydim-gated so non-array-typedef codegen is byte-identical):** (A — action-only, no grammar change, lowest risk) two new helpers `emit_global_arr_instance` (file-scope/static-local zero-block of D*sizeof(elem), registered IDIR(elem) array so it decays to its address, NO aoa_dim — a plain array typedef instance is NOT an array-of-array) and `emit_global_sized_array` (aoa-aware sized array, factored verbatim out of the existing `[expr] ';'` rule so that rule's output stays byte-identical), wired into: the file-scope bare-`;` rule, the file-scope `, ext_decllist ';'` multi rule (first name + each plain item), `emit_local_multi_decl` (first declarator — drop the op check since the leading IDENT is always a bare token there — + plain loop item), `emit_local_multi_decl_full` (plain op==0 loop item), and the static-local `dcls STATIC type IDENT ';'` rule (via `emit_static_local` isarray=1); (B — ONE new grammar production) `typed_decl_rest: '[' expr ']' ',' ext_decllist ';'` for the file-scope array-FIRST multi-decl (GAP1) — emits the first declarator via `emit_global_sized_array` then walks ext_decllist ('B' sized-array items via the same helper, plain instance items via `emit_global_arr_instance`, plus the existing 'A'/'F'/scalar arms) — mirroring the block-local `dcls type IDENT '[' expr ']' ',' ext_decllist ';'` rule §7c added; this also fixes plain-int `int counts[3], total;` at file scope, which was the same parse-error hole.  Gated by the all-new `aoa_instance_probe` (`minic/dos/examples/aoa_instance_probe.c`, medium+compact+large, model-independent program output) — bug-loud: on the UNFIXED compiler shape (3) is a hard parse error (the probe won't even build) and (1)/(2)/(4)/(5) pass `setjmp()` a garbage pointer loaded from a 2-byte slot (crash / wrong value); each case round-trips a distinct value through setjmp/longjmp IN-FRAME (file/static buffers persist but the longjmp fires while the setjmp frame is live → no cross-frame UB).  Golden `file_single=11 / file_multi=21,22 / file_aoa=30,31,40,41 / block_multi=51,52 / static_single=61`, byte-identical across all three models.  Existing aoa/setjmp probes (arr_jmpbuf, aoa_extended, setjmp) all still [ok]; plain global arrays (`int arr[3];`) emit byte-identical (the helper is a literal copy); MP compact body 689,760 byte-identical (MP has no array-typedef instance multi-decls in the changed forms, and the gated branches never fire for its non-aoa decls).  STRATEGY: frontend-only, additive + g_td_arraydim-gated; the COPY/ADD-NEVER-MUTATE libstub-free toolchain is untouched → MP/stevie/every gate provably can't regress.  **⇒ Next session: ONE bounded grammar gap remains (and it is NOT aoa-specific — synthetic-but-bug-loud gating if pursued): function-local `static` MULTI-declarators** — `static int x, y;` / `static int a[3], b;` / `static jmp_buf a, b;` INSIDE a function body are ALL a parse error, because the `dcls STATIC type IDENT …` rules have NO `, ext_decllist` (plain-first) or `[expr] , ext_decllist` (array-first) production at all (the gap predates aoa and affects plain `int` too — it is a general static-local-multi-decl grammar hole, distinct from the aoa sizing the §8g fix closed).  The §8c "minic trailing-main quirk" was DISPROVED as a NON-BUG in §8f.  The huge pointer-arith family is CLOSED (relational §7u + equality §8e).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  Libstub-retirement campaign COMPLETE.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8g session notes (2026-06-16)

### The pick
- §8f handoff left ONE carried compiler track: the aoa sub-gaps.  Continued
  directly (no AskUserQuestion — it was the sole remaining track).

### The survey — the gap was wider than named
- Tested every `jmp_buf` declaration shape (jmp_buf = `int[8]`).  Correct only:
  block-local single-decl (§7e) and block-local bracketed multi (§7j).
- BROKEN: file-scope single `static jmp_buf g;` (2-byte scalar + loadw),
  file-scope multi `static jmp_buf a, b;`, file-scope array-first
  `static jmp_buf fa[2], fb[2];` (PARSE ERROR = named GAP1), block-local multi
  `jmp_buf a, b;` (named GAP2), static-local single `static jmp_buf s;`.
- Root cause for the mis-sized ones: only the block-local single-decl rule
  (`dcls type IDENT ';'`) had the `g_td_arraydim > 0` array-typedef-instance
  branch; every other plain-instance/multi-decl site sized the ELEMENT type.

### The fix (frontend minic.y, all g_td_arraydim-gated)
- Helper `emit_global_arr_instance(name, elem, dim)`: file-scope/static-local
  zero-block of dim*sizeof(elem), registered IDIR(elem) array (decays), NO
  aoa_dim (plain instance, not array-of-array).
- Helper `emit_global_sized_array(name, count)`: aoa-aware sized array, factored
  VERBATIM out of the existing `[expr] ';'` rule (that rule's output unchanged).
- Wired into: file-scope bare-`;`, file-scope `, ext_decllist ';'` (first +
  plain items), emit_local_multi_decl (first decl — no op check, leading IDENT
  is always a bare token — + plain item), emit_local_multi_decl_full (plain
  item), static-local `dcls STATIC type IDENT ';'` (emit_static_local isarray=1).
- ONE new grammar production: `typed_decl_rest: '[' expr ']' ',' ext_decllist
  ';'` (file-scope array-first multi-decl, GAP1) — also fixes plain-int
  `int counts[3], total;` at file scope.  Conflicts UNCHANGED at 115.

### Gate + verification
- NEW minic/dos/examples/aoa_instance_probe.c (medium+compact+large), golden
  aoa_instance_probe.golden.txt.  Bug-loud: GAP1 won't build unfixed; the
  mis-sized ones crash/wrong-value through a garbage setjmp env pointer.
- test-dos 367→370 (3 new entries); make check green; conflicts 115.
- arr_jmpbuf/aoa_extended/setjmp probes still [ok] all models; plain global
  arrays byte-identical (helper is a literal copy of the old inline code).
- MP compact rebuilt: image 710,352 / body 689,760 BYTE-IDENTICAL → no Victor.
- minic.y frontend change (not emit.c) → no emit audit.

### ⇒ Next session
- ONE bounded grammar gap remains, NOT aoa-specific: function-local `static`
  MULTI-declarators (`static int x, y;` / `static int a[3], b;` /
  `static jmp_buf a, b;` inside a fn) are ALL parse errors — the `dcls STATIC
  type IDENT …` rules have no multi-decl production at all (predates aoa,
  affects plain int).  Synthetic-but-bug-loud gating if pursued.  (CLOSED in
  §8h above.)
- §8c trailing-main quirk DISPROVED (§8f).  Huge ptr-arith CLOSED (§7u+§8e).
- NO QBE backend bug open.
---

Older session headers (§8f and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
