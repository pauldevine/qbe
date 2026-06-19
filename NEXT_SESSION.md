# Next session (the §9b handoff CLOSED the bounded fn-ptr grammar follow-ons and left only consumer-driven options; the user (AskUserQuestion) chose **AUDIT / CONSOLIDATE** — no new feature.  §9c [2026-06-18, this session] **VERIFIED the full §9b baseline reproduces end-to-end and recorded one non-obvious build-path finding; NO source change to the qbe repo (the only write was a memory file outside the tree).**  Verification chain, all GREEN: `make check` green; minic rebuilt via the canonical staleness-safe path (`rm -f minic/minic && touch minic/minic.y && make minic/minic`); grammar conflicts **117 shift/reduce, 0 reduce/reduce** (the §9a/§9b baseline, confirmed via `yacc -v minic.y` → sum of per-state s/r in `y.output`); **MP compact body 689,760 BYTE-IDENTICAL** to the §8d libstub-free baseline (`build-micropython.sh --model=compact`); full **`test-dos.sh` 412/412 ok** (matches §9b); §9a/§9b probes/goldens/gate entries all consistent (`file_fnptr_probe` + `fnptr_multi_probe`, `test-dos.sh` entries 374–383); working tree clean (only untracked `.claude/`); newlibc **PR #24 already MERGED** (`victor9K_newlibc` `46eb8a7` on `main` — so the §9b "merge PR #24" follow-up is DONE).  **THE ONE FINDING (recorded in memory [[minic miniyacc and lexer quirks]], NOT a regression):** the in-tree vendored `minic/yacc` (miniyacc) binary can no longer parse the current `minic.y` AT ALL — `./yacc minic.y` → "syntax error, ; or | expected (on line 8218)" at the PRE-EXISTING double-action-block `ansi_proto_register ATTRIBUTE...` weak-attr rule (a §8-era construct, not §9) — so the `cd minic && make` path is broken.  The CANONICAL generator is the SYSTEM yacc: the house-rule top-level `make minic/minic` uses GNU make's implicit `.y→.c` rule with `$(YACC)` = `/usr/bin/yacc` (= bison on macOS), which builds fine and is what every gate/handoff conflict figure (115/117 s/r) actually came from.  Don't try to get the conflict report from `./yacc`; use `yacc -v minic.y`.  **git scope:** NONE in the qbe repo (audit-only; one memory file updated outside the tree).  **⇒ Next session — baseline confirmed healthy; ALL follow-ups remain consumer-driven (pick with the user):** (1) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (2) hunt a new no-consumer compiler track (e.g. mixed-far/near fn-ptr multi-decl, `__attribute__`-qualified file-scope fn-ptr var — the §8r `fpquals` pattern is the template); (3) Victor-native INT 1Ah/16h-free timer/keyboard DOS probes IF wanted (the §8l/§8n bare-metal pattern already covers that ground).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §9c session notes (2026-06-18)

### The pick
- §9b handoff: bounded fn-ptr grammar closed; PR #24 follow-up listed.  User
  (AskUserQuestion) chose **Audit / consolidate** — verify, no new feature.

### What was verified (all green)
- `make check` green; minic rebuilt via the staleness-safe top-level path.
- Conflicts **117 s/r, 0 r/r** (`yacc -v` / `y.output`, the §9a/§9b baseline).
- **MP compact body 689,760** byte-identical (§8d libstub-free baseline).
- **`test-dos.sh` 412/412 ok** (matches §9b).
- §9a/§9b probes/goldens/gate entries consistent (374–383); tree clean.
- newlibc **PR #24 already merged** (`46eb8a7` on `victor9K_newlibc` main).

### The one finding (not a regression)
- Vendored `minic/yacc` (miniyacc) can't parse current `minic.y` — errors at
  the pre-existing double-action-block `ansi_proto_register ATTRIBUTE...` rule
  (`minic.y:8218`).  So `cd minic && make` is broken.
- Canonical generator = SYSTEM yacc via top-level `make minic/minic`
  (GNU implicit `.y→.c`, `$(YACC)`=`/usr/bin/yacc`=bison).  Conflict figures
  always came from there.  Recorded in [[minic miniyacc and lexer quirks]].

### git scope
- NONE in the qbe repo (audit-only).  One memory file updated outside the tree.

### ⇒ Next session (consumer-driven, with the user)
- (1) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (2) hunt a no-consumer compiler track (mixed-far/near fn-ptr multi-decl;
  __attribute__-qualified file-scope fn-ptr var — §8r fpquals is the template);
- (3) Victor-native INT 1Ah/16h-free timer/kbd DOS probes if wanted.
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

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

Older session headers (§9a and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
