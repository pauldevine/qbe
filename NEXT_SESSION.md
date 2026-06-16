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
  affects plain int).  Synthetic-but-bug-loud gating if pursued.
- §8c trailing-main quirk DISPROVED (§8f).  Huge ptr-arith CLOSED (§7u+§8e).
- NO QBE backend bug open.
---

# Next session (carried compiler tracks — only the aoa sub-gaps remain; the §8c-surfaced "minic trailing-main quirk" was DISPROVED as a NON-BUG this session.  §8f [2026-06-16] **INVESTIGATED the carried "minic file-scope-statics-need-a-trailing-`main`" quirk (the §8c handoff's latent compiler track) and DISPROVED it — it is a NON-BUG; doc-only close, no compiler/qbe/emit/minic/toolchain source touched, gate UNCHANGED at 367/367, MP unaffected, `make check` not needed (no source change).**  §8c reported that compiling `dos_shim.c` without its trailing `main()` (the abandoned `-DNO_SHIM_MAIN` experiment) dropped ALL file-scope statics, ".ssa 0-vs-4 defs", hypothesising "trailing `main()` gates static-data emission".  **THREE INDEPENDENT PROOFS it does NOT reproduce:** (1) MECHANISM — `minic.y:10531-10548` emits file-scope data via an UNCONDITIONAL loop over `gloname[1..nglo)` AFTER `yyparse()`; `main` is never consulted, so the ONLY path to zero data output is `yyparse() != 0 → die("parse error")` ⇒ "0 defs" ⟺ MALFORMED input.  (2) EXACT §8c RECONSTRUCTION — `#ifndef NO_SHIM_MAIN` wrap, real cpp (`-D__ia16__ -DNO_LIBSTUB`), minic compact AND medium, both with/without `-DNO_SHIM_MAIN`: BOTH emit all 4 data defs, exit 0.  (3) TRUNCATION SWEEP — truncated the no-`main` TU at all 40 top-level `^}` boundaries: every one parses OK, defs rise monotonically 0→2→4.  **CONCLUSION:** the §8c 0-vs-4 was a MEASUREMENT ARTIFACT — the `-DNO_SHIM_MAIN` wrap fed minic malformed C (an eaten brace), parse-error → empty `.ssa`.  The `-Dmain=newlibc_test_main` rename remains right (solves the REAL `_main` collision, not a phantom bug).  No regression gate added (a meaningful one needs multi-TU link plumbing — disproportionate for a confirmed non-bug; user chose doc-only close).  Records corrected: `MEMORY.md`, `project_8c_mp_libstub_free.md`, `project_8e_huge_ptr_equality.md`, new `project_8f_trailing_main_nonbug.md`.)

## §8f session notes (2026-06-16)

### The pick
- §8e handoff offered two carried compiler tracks (aoa sub-gaps / minic
  trailing-main quirk).  User (AskUserQuestion) chose the trailing-main quirk.

### The investigation — it is a NON-BUG
- §8c claimed: dropping `dos_shim.c`'s trailing `main()` (via `-DNO_SHIM_MAIN`)
  drops ALL file-scope statics; ".ssa 0-vs-4 defs"; "trailing main() gates
  static-data emission".
- DISPROVED by three independent proofs (mechanism / exact reconstruction /
  truncation sweep — see the header above).  The 0-vs-4 was a measurement
  artifact (the `-DNO_SHIM_MAIN` wrap produced malformed C → parse error →
  empty `.ssa`).

### Outcome (doc-only close, user's choice)
- No code change.  test-dos UNCHANGED 367/367 (not re-run — nothing touched).
- The `-Dmain=newlibc_test_main` rename is still correct.
- No regression gate (multi-TU link plumbing — disproportionate for a non-bug).
- Records corrected: MEMORY.md, project_8c, project_8e, new project_8f.

### ⇒ Next session
- ONE carried compiler track remained at §8f close: the aoa sub-gaps (CLOSED in
  §8g above for every reachable site; function-local-static multi-decl left).
- Huge pointer-arith family CLOSED (§7u + §8e).  Libstub-retirement COMPLETE.
- NO QBE backend bug open.
---

Older session headers (§8e and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
