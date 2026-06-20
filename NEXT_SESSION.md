# Next session (the §9d handoff completed the file-scope function-pointer grammar family and left only consumer-driven options; the user (AskUserQuestion) chose **HUNT A COMPILER TRACK**.  §9e [2026-06-19, this session] **CLOSED the MULTI-DECLARATOR bitfield list — `unsigned a:3, b:5, c:4;` (the common hardware-register form) plus the mixed `unsigned a:3, b;` / `unsigned a, b:5;` and arbitrary interleavings — all of which were hard parse errors; AND fixed a latent pre-existing bug in the same `sm_more_names` code path that silently dropped middle members from any struct declaration with 4+ comma-separated members.  The fix is a frontend `minic.y` change → no emit audit; test-dos 417 → 422; conflicts UNCHANGED at the §9a/§9b/§9d baseline 117 s/r, 0 r/r; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  EMPIRICAL SCOPING FIRST (house rule): batch-probed ~30 C11/GNU constructs through `minic -m small < x.c` to find a REAL gap rather than assume one.  Genuine gaps surfaced — multi-declarator bitfields; block-scope/static-local/`__far`/array function-pointer VARIABLES (`int (*p)(int)` block-scope works, but `static int (*p)(int);`, `int __far (*p)(int);`, `int (*tab[3])(int);` all parse-error); pointer-to-array (`int (*p)[3];`); array-of-fn-ptr file-scope + typedef; unnamed fn-ptr parameter (`int f(int (*)(int))`); compound-literal-array (`(int[]){1,2,3}`); nested designated initializer (`{[0].x=1}`).  Picked **multi-declarator bitfields**: bounded, codegen already works (a single bitfield packs correctly with `and`/`shl`/`or` mask-shift), directly relevant to Victor 9000 hardware-register structs, and no consumer needed.  **THE GAP:** `smembers` had only a single-bitfield production (`type IDENT ':' expr ';'`) and a plain multi-NAME production (`type IDENT ',' sm_more_names ';'`, for `struct L *prev, *next;`) — there was no C11 struct-declarator-list (6.7.2.1) in which each comma-separated item can independently carry a `: width`.  **THE FIX (frontend `minic.y`, additive):** (1) a `sm_more_names` list node now carries an optional bitfield width-expr in `n->l` (NIL = a plain member); added a start item `IDENT ':' expr` and a chain item `sm_more_names ',' IDENT ':' expr`.  (2) a NEW production `smembers type IDENT ':' expr ',' sm_more_names ';'` handles a list whose FIRST declarator is a bitfield.  (3) the existing plain multi-name action was generalized to emit a bitfield (`structaddbitfield`) when a node's `n->l` is set, else `structaddmember` — so the plain-only path is byte-identical (every node keeps `n->l == NIL`, exactly the prior behavior) and an all-bitfield list emits SSA byte-identical to the equivalent separate-declaration form (verified by `diff`).  Lookahead distinguishes the single-bitfield rule (`;` after `expr`) from the new multi (`,` after `expr`), so it is conflict-free.  **THE LATENT BUG (found + fixed in the SAME code):** `sm_more_names` chained new items with `$1->r = n` — writing the HEAD node's link, not the tail's — so a list of 3+ TRAILING declarators (4+ comma-separated members total in one declaration) silently DROPPED its middle members: `struct L { int a, b, c, d; }` registered only 3 members (`alloc 6`, should be 8).  Latent because real `T a, b;` / `struct L *prev, *next;` lists rarely exceed ONE trailing name (the overwrite only bites the 2nd-and-later append); the new multi-declarator bitfield probe's 3-item tail (`n, o:4, r`) was the first construct to hit it.  Fixed all three `sm_more_names ','` chain productions to APPEND at the tail (`Node *tl = $1; while (tl->r) tl = tl->r; tl->r = n;`).  The MP body byte-compare being IDENTICAL proves MP contains no 4+-comma struct member lists (so the correctness fix changes no existing gated output).  **GATED bug-loud** by `minic/dos/examples/bitfield_multidecl_probe.c` (small+medium+compact+large+huge): on the pre-fix compiler the first multi-declarator bitfield is `error:46: parse error` so the program does not build (confirmed by `git stash`-ing the §9e `minic.y` change and recompiling); the probe covers form A all-bitfield / B bitfield+plain / C plain+bitfield / D mixed (bitfield,plain,bitfield,plain), field-independence (assigning `a = 13` to a 3-bit field wraps to `5` AND leaves `b`/`c` untouched — proving the per-field read-modify-write masking), and the four struct `sizeof`s (A=2 B=4 C=4 D=8).  All values are field contents and sizes (not addresses), so the golden is model-independent and byte-identical across all five models (golden ends `bitfield_multidecl_probe done`, no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **422/422 ok** (417 → 422, the 5 new probe entries, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (the chaining fix + bitfield productions never alter MP's codegen → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = the `smembers` bitfield-list productions + the generalized actions + the `sm_more_names` tail-append chaining fix; new `minic/dos/examples/bitfield_multidecl_probe.c` + `minic/dos/tests/bitfield_multidecl_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **NOTE on conflict figures:** they come from the SYSTEM yacc (`/usr/bin/yacc` = bison): `yacc -v minic/minic.y`, then sum the per-state `N shift/reduce` lines in `y.output` → 117 s/r, 0 r/r.  The vendored `minic/yacc` can no longer parse the current `minic.y` (the §9c finding, [[minic miniyacc and lexer quirks]]).  Rebuild minic staleness-safe: `rm -f minic/minic && touch minic/minic.y && make minic/minic`.  **⇒ Next session — still consumer-driven (pick with the user):** (1) hunt another bounded no-consumer compiler track — the scoping sweep above is a ready menu (block-scope/static-local/`__far`/array fn-ptr VARIABLES; ptr-to-array; unnamed fn-ptr param; compound-literal-array; nested designated init); (2) merge newlibc **PR #24** is ALREADY DONE (`victor9K_newlibc` `46eb8a7`, confirmed §9c/§9e); (3) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (4) Victor-native INT 1Ah/16h-free timer/keyboard DOS probes if wanted (the §8l/§8n bare-metal pattern already covers that ground).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §9e session notes (2026-06-19)

### The pick
- §9d handoff: file-scope fn-ptr grammar family COMPLETE, no open bug / no
  carried track.  PR #24 already merged (`46eb8a7`, verified this session).
  User (AskUserQuestion) chose **Hunt a compiler track**.

### Empirical scoping (do this before assuming the gap)
- Batch-probed ~30 C11/GNU constructs through `minic -m small < x.c`.  Real
  parse gaps: multi-declarator bitfields; block-scope/static-local/`__far`/
  array fn-ptr VARIABLES; ptr-to-array; array-of-fn-ptr file-scope+typedef;
  unnamed fn-ptr param; compound-literal-array; nested designated init.
- Picked **multi-declarator bitfields** — bounded, codegen already works
  (single bitfield packs with `and`/`shl`/`or`), hardware-register relevant,
  no consumer needed.

### The gap + fix (frontend minic.y, additive)
- `smembers` had only single-bitfield (`type IDENT ':' expr ';'`) and plain
  multi-name (`type IDENT ',' sm_more_names ';'`) — no struct-declarator-list
  where each item can carry `: width`.
- `sm_more_names` node now carries an optional width-expr in `n->l` (NIL =
  plain); added start item `IDENT ':' expr` + chain `sm_more_names ',' IDENT
  ':' expr`.
- New production `smembers type IDENT ':' expr ',' sm_more_names ';'`
  (first declarator is a bitfield).
- Existing plain multi-name action generalized: emit `structaddbitfield` when
  `n->l` set, else `structaddmember` — plain-only path BYTE-IDENTICAL (all
  nodes keep `n->l==NIL`).  All-bitfield list emits SSA byte-identical to the
  separate-declaration form (diff-verified).

### The latent bug (found + fixed in the same code)
- `sm_more_names` chained `$1->r = n` (overwrote the HEAD's link), so a list
  with 3+ trailing declarators (4+ comma members total) silently dropped its
  middle members — `struct L{int a,b,c,d;}` = 3 members, `alloc 6` not 8.
  Latent because real lists rarely exceed one trailing name; the probe's
  3-item tail `n,o:4,r` was the first to hit it.
- Fixed all 3 chain productions to APPEND at the tail
  (`while (tl->r) tl=tl->r; tl->r=n`).  MP body byte-identical ⇒ MP has no
  4+-comma struct lists, so the correctness fix changes no gated output.

### Conflicts (no change)
- `yacc -v minic/minic.y` → sum per-state s/r in `y.output` = 117 s/r,
  0 r/r (the §9a/§9b/§9d baseline).  Lookahead `;` vs `,` after the bitfield
  `expr` keeps the new multi production conflict-free.

### Gate + validation
- `bitfield_multidecl_probe.c` (5 models): forms A all-bitfield / B
  bitfield+plain / C plain+bitfield / D mixed, field-independence (a=13→wraps
  3-bit 5, leaves b/c), sizeofs (A=2 B=4 C=4 D=8).  Model-independent golden,
  ends `bitfield_multidecl_probe done`.  Bug-loud: pre-fix `error:46: parse
  error`, build fails (git-stash confirmed).
- `make check` green; test-dos 417 → 422; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (smembers bitfield-list productions + generalized
  actions + sm_more_names tail-append chaining fix),
  minic/dos/examples/bitfield_multidecl_probe.c,
  minic/dos/tests/bitfield_multidecl_probe.golden.txt,
  tools/test-dos.sh (+5 entries).  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- (1) hunt another bounded no-consumer track from the scoping menu above
  (block-scope/static-local/`__far`/array fn-ptr vars; ptr-to-array; unnamed
  fn-ptr param; compound-literal-array; nested designated init);
- (2) PR #24 already merged;
- (3) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (4) Victor-native INT 1Ah/16h-free timer/kbd DOS probes if wanted.
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

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

Older session headers (§9c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
