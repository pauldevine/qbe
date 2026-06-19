# Next session (the §9c handoff confirmed the §9b baseline healthy and left only consumer-driven options; the user (AskUserQuestion) chose **HUNT A COMPILER TRACK**.  §9d [2026-06-18, this session] **CLOSED the LAST file-scope function-pointer grammar gap — the `__attribute__`-QUALIFIED pointee `void __attribute__((interrupt)) (*v)(void);` (plus the combined `__far __attribute__((...))` / `__attribute__((...)) __far` forms and the multi-declarator) was still a hard parse error after §9b (which deliberately admitted ONLY `TFAR` at file scope); minic now parses all of them; the fix is a frontend `minic.y` change → no emit audit; test-dos 412 → 417; conflicts UNCHANGED at the §9a/§9b baseline 117 s/r, 0 r/r; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  EMPIRICAL SCOPING FIRST: of the §9b handoff's two named candidates, "a multi-declarator mixing `__far`/near pointees" was ALREADY CLOSED — §9b's per-declarator `gfnptr_decl` chains `TFAR`/near independently (verified `int (*nb)(int), __far (*fa)(int);` and `int __far (*fa)(int), (*nb)(int);` both parse on the §9c compiler), so the ONLY genuine gap was the `__attribute__`-qualified pointee.  It is ALSO a real latent consumer: newlibc interrupts.h spells a far ISR `void __far __attribute__((interrupt)) ...`, and §8w had to sidestep the fn-ptr-VARIABLE equivalent in `test_timer_dos` by declaring its handler as a plain `static void __far *`.  **THE §9b TRAP, AND HOW §9d AVOIDS IT:** §9b found that reusing the §8r `fpquals` nonterminal (which contains `fp_attr`) introduced 1 NEW reduce/reduce at yacc state 391 — `fp_attr`'s SEPARATE empty save-marker `fp_attr_save` collided with `attrreset`, both reduced after `ATTRIBUTE '(' '('`.  **THE FIX (frontend `minic.y`, additive):** restructured `gfnptr_decl` around a non-nullable `gfnptr_quals` run that SUBSUMES the §9b `TFAR` forms and adds a `gfnptr_attr` which reuses attropt's OWN `attrreset` empty marker (`gfnptr_attr: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'`, dropping whatever it set + resetting `cur_fn_interrupt`/`cur_fn_weak` defensively) — so the attribute is parsed by the EXACT same item sequence as `attropt` and is distinguished only by the token after the closing `))` (IDENT continues `typed_decl`, `(` continues this fn-ptr declarator), which LALR(1) resolves by lookahead with NO new conflict.  `gfnptr_quals` = `{TFAR, gfnptr_attr, TFAR gfnptr_attr, gfnptr_attr TFAR}` and is deliberately NON-nullable (the bare no-qualifier declarator keeps its own two `gfnptr_decl` productions) so no empty reduction can ever compete with `typed_decl`'s `type TFAR attropt IDENT` on a TFAR lookahead.  The collapse into one `gfnptr_quals` symbol keeps the declarator's `$` indices fixed ($4 name, $7 fptpar) regardless of how many qualifiers were written.  Every qualifier is ACCEPTED and DROPPED — `__far` is a memory-model property and an interrupt/weak attribute on a pointer VARIABLE has no codegen meaning (the ISR ABI lives on a function DEFINITION's linkage, not a pointee type), so the pointer type is `IDIR(FUNC(base))` identical to the unqualified declarator.  Codegen confirmed correct: `$isr = { w $handler }`, `$sw = { w 0 }` (static, no `.globl`), and a real `__attribute__((interrupt))` FUNCTION after an attributed fn-ptr VAR still gets `export interrupt function` linkage while a plain function stays plain (NO attribute leak — the §8r concern).  **GATED bug-loud** by `minic/dos/examples/fnptr_attr_probe.c` (small+medium+compact+large+huge): on the §9b compiler the first attribute declaration is a `parse error` so the program does not build (confirmed by `git stash`-ing the §9d `minic.y` change and recompiling); the probe covers plain attribute / static attribute / `__far __attribute__` / `__attribute__ __far` / attributed multi-declarator / attributed initializer + a following plain function (proving no attribute leak), and its values are dispatch results (not pointer addresses) so the golden is model-independent and all five models are byte-identical (golden ends `fnptr_attr_probe done\n`, no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **417/417 ok** (412 → 417, the 5 new probe entries, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (MP has no file-scope fn-ptr variables → the new branches never fire → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = the `gfnptr_quals`/`gfnptr_attr` restructure of `gfnptr_decl`, subsuming the §9b `TFAR` forms; new `minic/dos/examples/fnptr_attr_probe.c` + `minic/dos/tests/fnptr_attr_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **⇒ Next session — the file-scope fn-ptr grammar family (§9a single, §9b multi+far, §9d attribute) is now COMPLETE; all remaining follow-ups are consumer-driven (pick with the user):** (1) merge newlibc **PR #24** (`minic-dostest-hw-gate` — the §8z `_dos_getvect`/`_dos_setvect`/`_chain_intr` intrinsics + `clock()` fill) into `victor9K_newlibc` main; (2) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (3) IF Victor-native timer/keyboard coverage is wanted, author INT 1Ah/16h-free DOS-hosted timing/keyboard probes — but the §8l/§8n bare-metal pattern already covers that ground; (4) hunt another no-consumer compiler track if one surfaces.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §9d session notes (2026-06-18)

### The pick
- §9c handoff: §9b baseline confirmed healthy, no open bug / no carried track.
  User (AskUserQuestion) chose **Hunt a compiler track**.

### Empirical scoping (do this before assuming the gap)
- §9b named two bounded candidates.  Testing on the §9c compiler showed the
  "mixed __far/near multi-declarator" candidate ALREADY parses (§9b's
  per-declarator `gfnptr_decl` chains TFAR/near independently — both
  `int (*nb)(int), __far (*fa)(int);` and `int __far (*fa)(int), (*nb)(int);`
  parse).  The ONLY real gap was the `__attribute__`-qualified pointee, which
  is also a latent consumer (§8w's test_timer_dos handler, sidestepped as a
  plain `void __far *`; interrupts.h far-ISR spelling).

### Root cause + the §9b trap
- After `type`, `void __attribute__((..)) (*v)(..)` had no production: §9b
  admitted only TFAR at file scope because reusing §8r `fpquals` (with
  `fp_attr`, whose SEPARATE empty marker `fp_attr_save` collided reduce/reduce
  with `attrreset` at state 391) was the path it avoided.

### The fix (frontend minic.y, additive)
- Restructured `gfnptr_decl` around a NON-nullable `gfnptr_quals` run that
  subsumes the §9b TFAR forms and adds `gfnptr_attr`.
- `gfnptr_attr: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'` — reuses
  attropt's OWN `attrreset` marker (NOT a new one), so the attribute is parsed
  by the same item sequence as attropt and distinguished only by the token
  after `))` (IDENT → typed_decl, `(` → fn-ptr declarator).  Action drops the
  attribute + resets cur_fn_interrupt/cur_fn_weak (defensive).
- `gfnptr_quals: TFAR | gfnptr_attr | TFAR gfnptr_attr | gfnptr_attr TFAR` —
  non-nullable (bare declarator keeps its own productions) so no empty
  reduction competes with `type TFAR attropt IDENT` on a TFAR lookahead.  The
  one `gfnptr_quals` symbol keeps declarator $ indices fixed ($4 name, $7 par).
- All qualifiers accepted + DROPPED (type = IDIR(FUNC(base))).  Verified
  codegen: `$isr = { w $handler }`, static `$sw = { w 0 }` (no .globl); a real
  __attribute__((interrupt)) FUNCTION after an attributed fn-ptr VAR keeps
  `export interrupt function` linkage, a plain fn stays plain — NO leak (§8r).

### Conflicts (no change)
- `yacc -v minic.y` → 117 shift/reduce, 0 reduce/reduce = the §9a/§9b baseline.
  The shared-`attrreset` design (vs §9b's failed `fp_attr`) is conflict-free,
  including the combined far+attribute forms.  (System yacc = /usr/bin/yacc =
  bison; the vendored minic/yacc still can't parse minic.y, §9c finding.)

### Gate + validation
- `fnptr_attr_probe.c` (5 models): plain / static / __far __attribute__ /
  __attribute__ __far / attributed multi-decl / attributed initializer + a
  following plain fn (no-leak).  Dispatch results → model-independent golden,
  ends `fnptr_attr_probe done\n` (no §8y trailing-blank).  Bug-loud: §9b
  compiler `parse error` at the first attribute decl, build fails (git-stash
  confirmed).
- `make check` green; test-dos 412 → 417; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (gfnptr_quals/gfnptr_attr restructure of gfnptr_decl,
  subsuming §9b TFAR), minic/dos/examples/fnptr_attr_probe.c,
  minic/dos/tests/fnptr_attr_probe.golden.txt, tools/test-dos.sh (+5 entries).
  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- The file-scope fn-ptr grammar family (§9a single, §9b multi+far, §9d
  attribute) is COMPLETE.
- (1) merge newlibc PR #24 (minic-dostest-hw-gate);
- (2) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (3) Victor-native timer/kbd DOS probes if wanted (§8l/§8n already cover it);
- (4) hunt another no-consumer compiler track if one surfaces.
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

# Next session (the §9b handoff CLOSED the bounded fn-ptr grammar follow-ons and left only consumer-driven options; the user (AskUserQuestion) chose **AUDIT / CONSOLIDATE** — no new feature.  §9c [2026-06-18] **VERIFIED the full §9b baseline reproduces end-to-end and recorded one non-obvious build-path finding; NO source change to the qbe repo (the only write was a memory file outside the tree).**  Verification chain, all GREEN: `make check` green; minic rebuilt via the canonical staleness-safe path (`rm -f minic/minic && touch minic/minic.y && make minic/minic`); grammar conflicts **117 shift/reduce, 0 reduce/reduce** (the §9a/§9b baseline, confirmed via `yacc -v minic.y` → sum of per-state s/r in `y.output`); **MP compact body 689,760 BYTE-IDENTICAL** to the §8d libstub-free baseline (`build-micropython.sh --model=compact`); full **`test-dos.sh` 412/412 ok** (matches §9b); §9a/§9b probes/goldens/gate entries all consistent (`file_fnptr_probe` + `fnptr_multi_probe`, `test-dos.sh` entries 374–383); working tree clean (only untracked `.claude/`); newlibc **PR #24 already MERGED** (`victor9K_newlibc` `46eb8a7` on `main` — so the §9b "merge PR #24" follow-up is DONE).  **THE ONE FINDING (recorded in memory [[minic miniyacc and lexer quirks]], NOT a regression):** the in-tree vendored `minic/yacc` (miniyacc) binary can no longer parse the current `minic.y` AT ALL — `./yacc minic.y` → "syntax error, ; or | expected (on line 8218)" at the PRE-EXISTING double-action-block `ansi_proto_register ATTRIBUTE...` weak-attr rule (a §8-era construct, not §9) — so the `cd minic && make` path is broken.  The CANONICAL generator is the SYSTEM yacc: the house-rule top-level `make minic/minic` uses GNU make's implicit `.y→.c` rule with `$(YACC)` = `/usr/bin/yacc` (= bison on macOS), which builds fine and is what every gate/handoff conflict figure (115/117 s/r) actually came from.  Don't try to get the conflict report from `./yacc`; use `yacc -v minic.y`.  **git scope:** NONE in the qbe repo (audit-only; one memory file updated outside the tree).  **⇒ Next session — baseline confirmed healthy; ALL follow-ups remain consumer-driven (pick with the user):** (1) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (2) hunt a new no-consumer compiler track (e.g. mixed-far/near fn-ptr multi-decl, `__attribute__`-qualified file-scope fn-ptr var — the §8r `fpquals` pattern is the template); (3) Victor-native INT 1Ah/16h-free timer/keyboard DOS probes IF wanted (the §8l/§8n bare-metal pattern already covers that ground).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

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

Older session headers (§9b and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
