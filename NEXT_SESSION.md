# Next session (the §9a handoff CLOSED the file-scope function-pointer VARIABLE grammar hole and left only consumer-driven follow-ups; the user (AskUserQuestion) chose **(4) the BOUNDED fn-ptr grammar extension**.  §9b [2026-06-18, this session] **EXTENDED §9a to its two bounded follow-ons — the MULTI-DECLARATOR comma form `int (*a)(int), (*b)(int);` and the `__far`-QUALIFIED POINTEE `void __far (*v)(void);`, both still hard parse errors after §9a (which added only the single-declarator file-scope fn-ptr forms); the fix is a frontend `minic.y` change → no emit audit; test-dos 407 → 412; conflicts UNCHANGED at the §9a baseline 117 s/r, 0 r/r; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  ROOT CAUSE: §9a's `gfnptrdcl` had four single-declarator productions (`{plain,STATIC} × {';' , '=' expr ';'}`), so a second comma-separated declarator and a `__far` qualifier on the pointee had no production.  **THE FIX (frontend `minic.y`, additive, +68/-13):** refactored the four §9a productions into a shared `gfnptr_decllist` so the declaration's ONE return type applies to every comma-separated declarator (C semantics), via two new helpers next to `emit_global_fnptr` — `mk_fnptr_decl(name, fptpar, init)` builds a private list node (`'Q'` tag: `u.v`=name, `l`=`'Z'` holder{`l`=fptpar, `r`=init}, `r`=next-in-chain; these nodes are consumed ONLY by the emitter and never reach codegen, so the op tags are free bookkeeping) and `emit_global_fnptr_list(base, list, is_static)` walks the chain calling §9a's `emit_global_fnptr` per declarator with the shared `base`.  `gfnptr_decl` now has four forms: `'(' '*' IDENT ')' '(' fptpar0 ')'` ± `'=' expr`, and the same two with a leading `TFAR` — the `__far` qualifier is ACCEPTED and DROPPED (the far calling convention is a memory-model property on this toolchain, so the pointer type is `IDIR(FUNC(base))` identical to the unqualified declarator regardless of model — the §8r fn-ptr-PARAMETER reasoning).  **THE CONFLICT TRAP (the session's one real design iteration):** my first cut reused §8r's `fpquals` nonterminal (= `TFAR | fp_attr | TFAR fp_attr`), which introduced **1 NEW reduce/reduce conflict** — yacc state 391, `attrreset` (rule 136) vs `fp_attr_save` (rule 176), both EMPTY markers reduced after `ATTRIBUTE '(' '('`; bringing `fp_attr` into the file-scope context merged its state with the `attropt` state, and the default resolution (attrreset) would break `fp_attr`'s save/restore.  **FIX = admit ONLY `TFAR` at file scope, NOT the `fp_attr` form:** a file-scope fn-ptr VARIABLE has no enclosing function for an `__attribute__((interrupt))` to qualify, there is no consumer, and `TFAR`'s distinct first token stays conflict-free against the `type TFAR '*'` far-pointer-type extension (the §8r reasoning) → conflicts back to the §9a baseline 117 s/r, **0 r/r** (verified `yacc -v`).  **GATED bug-loud** by `minic/dos/examples/fnptr_multi_probe.c` (small+medium+compact+large+huge): on the §9a compiler the first multi-declarator is `error:40: parse error` so the program does not build (confirmed by `git stash`-ing the §9b `minic.y` change and recompiling); the probe covers multi-decl plain / static / per-item-initialized + `__far` plain+initialized, and its values are dispatch results (not pointer addresses) so the golden is model-independent and all five models are byte-identical (golden ends `fnptr_multi_probe done\n`, no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **412/412 ok** (407 → 412, the 5 new probe entries, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (MP has no file-scope fn-ptr variables → the new branches never fire → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master `f3cae83` (`minic.y` = the `gfnptr_decllist` refactor + `mk_fnptr_decl`/`emit_global_fnptr_list` helpers + the `%type` decls; new `minic/dos/examples/fnptr_multi_probe.c` + `minic/dos/tests/fnptr_multi_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **⇒ Next session — the bounded fn-ptr grammar follow-ons are CLOSED; all remaining follow-ups are consumer-driven (pick with the user):** (1) merge newlibc **PR #24** (`minic-dostest-hw-gate` — the §8z `_dos_getvect`/`_dos_setvect`/`_chain_intr` intrinsics + `clock()` fill) into `victor9K_newlibc` main; (2) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (3) IF Victor-native timer/keyboard coverage is wanted, author INT 1Ah/16h-free DOS-hosted timing/keyboard probes — but the §8l/§8n bare-metal pattern already covers that ground.  NO QBE/minic codegen bug open; NO carried compiler track remains.  Bounded gap STILL left (no consumer): an `__attribute__`-qualified file-scope fn-ptr variable, or a multi-declarator mixing `__far`/near pointees — the §8r `fpquals` pattern is the template if one appears.)

## §9b session notes (2026-06-18)

### The pick
- §9a handoff: file-scope fn-ptr VARIABLE grammar closed; no open compiler bug,
  no carried track.  User (AskUserQuestion) chose **(4) the BOUNDED fn-ptr
  grammar extension** from the follow-ups.

### Root cause
- §9a's `gfnptrdcl` had only single-declarator productions, so
  `int (*a)(int), (*b)(int);` (multi-declarator) and `void __far (*v)(void);`
  (far-qualified pointee) were still parse errors.

### The fix (frontend minic.y, additive, +68/-13)
- Refactored the four §9a single-decl productions into a shared
  `gfnptr_decllist` (one return type applies to every comma declarator).
- New helpers near `emit_global_fnptr`: `mk_fnptr_decl` (private 'Q'/'Z' list
  nodes — never reach codegen) + `emit_global_fnptr_list` (walks the chain).
- `gfnptr_decl`: `'(' '*' IDENT ')' '(' fptpar0 ')'` ± `'=' expr`, and the same
  two with a leading `TFAR` (accepted + dropped, §8r reasoning).

### The conflict trap (one design iteration)
- First cut reused §8r `fpquals` (incl `fp_attr`) → 1 NEW reduce/reduce (state
  391: `attrreset` vs `fp_attr_save`, both empty markers after `ATTRIBUTE '(' '('`,
  merged into the `attropt` state at file scope; default picks attrreset →
  breaks fp_attr's restore).
- FIX: admit ONLY `TFAR` at file scope (no consumer for an attribute on a
  file-scope fn-ptr VAR; `TFAR`'s distinct first token is conflict-free vs
  `type TFAR '*'`).  Back to the §9a baseline 117 s/r, 0 r/r.

### Gate + validation
- `fnptr_multi_probe.c` (5 models): multi-decl plain/static/per-item-init +
  __far plain+init; dispatch results (model-independent golden, ends
  `fnptr_multi_probe done\n`, no §8y trailing-blank).  Bug-loud: §9a compiler
  `error:40: parse error` at the first multi-decl, build fails (git-stash).
- `make check` green; test-dos 407 → 412; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master f3cae83: minic.y (gfnptr_decllist refactor + helpers + %type),
  minic/dos/examples/fnptr_multi_probe.c,
  minic/dos/tests/fnptr_multi_probe.golden.txt, tools/test-dos.sh (+5 entries).
  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- (1) merge newlibc PR #24 (minic-dostest-hw-gate);
- (2) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (3) Victor-native timer/kbd DOS probes if wanted (§8l/§8n already cover it).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
- Bounded (no consumer): __attribute__-qualified or mixed-far/near
  multi-declarator file-scope fn-ptr vars (the §8r fpquals pattern is template).
---

# Next session (the §8z handoff GATED 2 of 6 hardware dos_tests on MAME and FIXED a real QBE inline-asm-clobber codegen bug, leaving consumer-driven follow-ups; the user (AskUserQuestion) chose **(1) the FILE-SCOPE function-pointer VARIABLE grammar**.  §9a [2026-06-18, this session] **CLOSED the file-scope function-pointer VARIABLE grammar hole — `void (*v)(void);` (plus the `static` and function-address-initialized forms) at file scope was a hard parse error; minic now parses all four forms; the fix is a frontend `minic.y` change → no emit audit; test-dos 402 → 407; conflicts 115 → 117 (justified — see below); MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  ROOT CAUSE: `typed_decl` (the `prog`-level non-extern declaration) begins with `type_and_ident` (= `type IDENT`), which a `type '(' '*' IDENT ')' ...` declarator can NEVER match; only the EXTERN (`extern int (*cb)(int,int);`), TYPEDEF (`typedef void (*fp_t)(void);`), and function-scope (`dcls` / statement) fn-ptr forms had productions, so a plain file-scope DEFINITION had none.  **THE FIX (frontend `minic.y`, additive):** a new `emit_global_fnptr(name, base, fptpar, init, is_static)` helper (placed next to `emit_global_sym_init`) emits a zero- or symbol-initialized DATA global (`{ w 0 }` / `{ w $foo }` near; `{ l $foo }` far — the far code-pointer static init is split into offset+segment words by `asm_to_omf.py`, the §6k/§7h `split_sym_long` path, confirmed in the `.omf.asm` nasm input as `dw _foo+0 / dw seg _foo`) and records the fn-ptr prototype id via `varsetfpid(name, fpproto_alloc(base, fptpar))` so an indirect call coerces its arguments; `is_static` retro-marks the slot internal via `glo_mark_static_range` (no `.globl`).  A new `gfnptrdcl` nonterminal is wired into `prog` with FOUR productions — `{plain, STATIC} × {';' , '=' expr ';'}`; the initializer runs through `cival_eval` (a bare function name decays to its `$sym` address, case 'V').  **CONFLICTS 115 → 117 (justified, NOT a new conflict KIND):** both new conflicts are the IDENTICAL pre-existing `IDENT → reduce attrreset` shift/reduce conflict (baseline already carries 1; mine carries 3), which yacc default-resolves by SHIFT (the correct `type IDENT` path).  Adding the `type '('` / `STATIC type '('` productions forces the LALR builder to DUPLICATE the `type .` / `STATIC type .` item-sets into the new file-scope fn-ptr context, and each duplicate carries that same already-accepted benign conflict; the `gfnptrdcl` decision itself (shift `(`) is fully unambiguous.  Verified by diffing the `y.output` conflict descriptions (token + reduce-rule NAME, stable across state renumbering): the ONLY delta is the `attrreset` IDENT-conflict count 1 → 3.  **GATED bug-loud** by `minic/dos/examples/file_fnptr_probe.c` (small+medium+compact+large+huge): the UNFIXED compiler hits a `parse error` at the first file-scope fn-ptr declaration so the program does not even build (confirmed by `git stash`-ing the `minic.y` fix); it computes sums / dispatch results (not pointer addresses), so the golden is model-independent (near-code small/compact vs far-code medium/large/huge); it covers plain + static × uninitialized + function-address-initialized, proving `static` emits as plain `data` (internal linkage) and still dispatches.  All five models produce byte-identical output; the golden ends `done\n` (no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **407/407 ok** (the 5 new probe entries, 402 → 407, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (MP has no file-scope fn-ptr variables — they were parse errors — so the new branches never fire → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = `emit_global_fnptr` helper + `gfnptrdcl` nonterminal/4 productions + the `prog` wiring; new `minic/dos/examples/file_fnptr_probe.c` + `minic/dos/tests/file_fnptr_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **⇒ Next session — the file-scope fn-ptr-variable hole is CLOSED; all remaining follow-ups are consumer-driven (pick with the user):** (1) merge newlibc **PR #24** (`minic-dostest-hw-gate` — the §8z `_dos_getvect`/`_dos_setvect`/`_chain_intr` intrinsics + `clock()` fill) into `victor9K_newlibc` main; (2) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (3) IF Victor-native timer/keyboard coverage is wanted, author INT 1Ah/16h-free DOS-hosted timing/keyboard probes — but the §8l/§8n bare-metal pattern already covers that ground, so DOS-hosted versions add little.  NO QBE/minic codegen bug open; NO carried compiler track remains.  Note on the file-scope fn-ptr feature's bounds: it covers the single-declarator plain/`static` forms with an optional function-address initializer; a `__far`/attribute-qualified pointee (`void __far (*v)(void)` at file scope) or a multi-declarator comma form (`int (*a)(int), (*b)(int);`) is not handled — no consumer, and the §8r `fpquals` pattern is the template if one appears.)

## §9a session notes (2026-06-18)

### The pick
- §8z handoff: 2/6 hardware dos_tests MAME-gated + the inline-asm-clobber QBE
  fix; no open compiler bug, no carried track.  User (AskUserQuestion) chose
  **(1) the FILE-SCOPE function-pointer VARIABLE grammar** from the follow-ups.

### Root cause
- `void (*v)(void);` at file scope → `parse error`.  `prog` reduces
  `typed_decl: type_and_ident typed_decl_rest`, and `type_and_ident` is
  `type IDENT` — a `type '(' '*' IDENT ')' ...` declarator never matches it.
  Only EXTERN (7669), TYPEDEF (7783), and function-scope (`dcls` 9291 /
  statement 9788) fn-ptr forms had productions.  No file-scope variable form.

### The fix (frontend minic.y, additive)
- New `emit_global_fnptr(name, base, fptpar, init, is_static)` near
  `emit_global_sym_init`: emits a zero-init (`emit_zero_init`) or symbol-init
  (`cival_eval` → `{ c $sym }`) DATA global of type `IDIR(FUNC(base))`,
  `varsetfpid(... fpproto_alloc(base, fptpar))`, `glo_mark_static_range` when
  static.  Near = `{ w 0 }`/`{ w $foo }`; far = `{ l $foo }`, split into
  offset+seg words by asm_to_omf.py (§6k/§7h), confirmed in the .omf.asm.
- New `gfnptrdcl` nonterminal in `prog`, 4 productions:
  `type '(' '*' IDENT ')' '(' fptpar0 ')' ';'` and the `'=' expr ';'` form,
  plus the two `STATIC ...` siblings.

### Conflicts 115 → 117 (justified)
- Both new conflicts are the SAME pre-existing `IDENT [reduce attrreset]`
  shift/reduce conflict (default-resolved by shift), duplicated because
  `type '('` / `STATIC type '('` split the `type .` / `STATIC type .`
  item-sets into the new context.  Verified via a `y.output` conflict-desc
  diff (token + reduce-rule NAME, renumber-stable): the only delta is the
  attrreset count 1 → 3.  The gfnptrdcl decision (shift `(`) is unambiguous.

### Gate + validation
- `file_fnptr_probe.c` (5 models): plain/static × uninit/func-init; sums &
  dispatch (model-independent golden, ends `done\n`).  Bug-loud: stashing the
  minic.y fix → `error:40: parse error` at the first fn-ptr decl, build fails.
- `make check` green; test-dos 402 → 407; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (emit_global_fnptr + gfnptrdcl + prog wiring),
  minic/dos/examples/file_fnptr_probe.c, minic/dos/tests/file_fnptr_probe.golden.txt,
  tools/test-dos.sh (+5 entries).  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- (1) merge newlibc PR #24 (minic-dostest-hw-gate);
- (2) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (3) Victor-native timer/kbd DOS probes if wanted (§8l/§8n already cover it).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
- Bounded (no consumer): __far/attr-qualified or multi-declarator file-scope
  fn-ptr vars (the §8r fpquals pattern is the template).
---

Older session headers (§8z and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
