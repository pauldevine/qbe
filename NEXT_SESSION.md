# Next session (the §8u handoff DROVE the `all_upstream_bm` capstone on the UPSTREAM interrupt framework (`drivers/interrupts.c`) and left four consumer-driven follow-ups — and one of them, (3) publishing the merged newlibc work, has since landed: newlibc `main` now shows `08637c3 Merge pull request #22 from pauldevine/minic-isr-entry-port` and is IN SYNC with `origin/main` (the `minic-asm-port` + `minic-isr-entry-port` ports are published).  The user (AskUserQuestion) chose **the minic-PARSE bucket**.  §8v [2026-06-17, this session] **CLOSED the file-scope sized char-array string initializer — minic now parses `char NAME[N] = "string";` (e.g. `static char cwd[64] = "/";`), the form newlibc `tests/vshell.c` uses for its path buffers; the minic triage FAIL bucket dropped 7 → 6 at small AND medium (vshell now compiles END-TO-END); the fix is a frontend `minic.y` change → no emit audit; test-dos 386 → 391; conflicts UNCHANGED at 115 shift/reduce, 0 reduce/reduce; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  minic's file-scope declaration grammar had `'[' ']' '=' STR ';'` (the UNSIZED `char a[] = "x"` form) but NO `'[' expr ']' '=' STR ';'` sibling, so an EXPLICIT-dimension char array initialized from a string literal was a hard parse error.  (The brace forms `int a[3] = {1,2,3}` / `T a[N] = {…}` and the unsized string form `char a[] = "x"` all already parsed; ONLY sized + string-literal was missing.)  **THE FIX (frontend `minic.y`, additive, designed conflict-free):** a new `emit_string_array_sized(elemtyp, name, str_idx, count, static_local)` lays the literal's QBE data block bytes (incl. its NUL) at the front of the array and zero-fills the remaining `N*sizeof(T) - natural` bytes — reusing the literal's block `{ b "...", b 0 }` VERBATIM when the declared size fits exactly (`pad == 0`), and splicing `, z PAD` before the closing brace otherwise (verified byte-correct: `char a[8]="hi"` → `{ b "hi", b 0 , z 5 }` = 8 B; `char b[3]="hi"` → `{ b "hi", b 0 }` exact; `char c[64]="/"` → `… , z 62 }` = 64 B); a declared size SHORTER than the natural length (the exact-fit drop-NUL edge `char a[3]="abc"`, where C drops the NUL) dies clearly — no consumer, a documented bounded gap.  Wired to the new `'[' expr ']' '=' STR ';'` file-scope production placed next to the existing `'[' ']' '=' STR ';'`; its lookahead after `'[' expr ']' '='` (STR vs the `gaggr` `'{'` sibling) is DISTINCT, so the conflict count stays 115/0.  The `static` keyword at file scope is absorbed upstream (both `char a[N]="x"` and `static char a[N]="x"` flow through the same production — confirmed).  **SCOPE — file scope only (what vshell needs):** the BLOCK-scope string-array init is more broadly broken (even the unsized `void f(){ char a[] = "hi"; }` is a parse error there, and the dcls/statement-scope STATIC `'[' expr ']' '=' STR` variants are also missing), but vshell's six failing declarations were ALL file-scope statics, and fixing file scope makes vshell compile fully — so the block-scope and static-local sized-string variants are left as a documented bounded gap (no consumer yet; the natural follow-on if one appears).  **GATED `sized_str_array_probe` small+medium+compact+large+huge** (`minic/dos/examples/sized_str_array_probe.c` + `minic/dos/tests/sized_str_array_probe.golden.txt`, 5 entries in `tools/test-dos.sh`) — bug-loud: on the unfixed compiler the file does not parse (verified: a git-stashed-fix minic gives `parse error` on the probe, the fixed minic compiles clean), so the build fails outright; the probe exercises the headline vshell `static char cwd[64]="/"` form, a large zero-fill (`char greeting[16]="hi"`), exact fit (`char exactfit[3]="ab"`), an empty initializer (`char emptied[8]=""`), verifies the NUL + zero-filled slack, and rewrites the full N bytes to prove the declared size is real storage; the golden is contents/lengths/booleans → model-independent (verified byte-identical small/compact/huge).  **`vshell.c` now compiles END-TO-END small AND medium** (triage PASS 66 → 67 each); the 6 REMAINING parse-bucket TUs are all the Watcom-`_asm{}` `dos_tests` (`test_es_preservation`/`test_integration`/`test_keyboard_dos`/`test_memory_layout`/`test_serial_dos`/`test_timer_dos` — `_asm { mov ah,0 … }` brace-block inline asm with bare local-variable operands), a PORTING track (rewrite to minic's GNU `__asm__` Intel form / a `#if defined(__MINIC__)` fork, the §8k convention), NOT a bounded compiler feature.  **VERIFICATION:** minimal forms parse (`static char cwd[64]="/"`, `char a[8]="hi"`, `char z[1]=""`); too-long dies; data blocks byte-correct (above); conflicts 115/0 unchanged; `make check` green; MP compact body 689,760 byte-identical (MP has NO such decls — they were parse errors, so the new production never fires); full gate **391/391**.  Frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master `4c210a3` (`minic.y` helper + production, the new probe `.c` + golden, 5 `test-dos.sh` entries — NO compiler/qbe/emit, NO newlibc-tree change).  **⇒ Next session (consumer-driven, pick with the user):** (1) the remaining minic-PARSE bucket — the 6 Watcom-`_asm{}` `dos_tests` (a §8k-style gas/Watcom→minic inline-asm PORTING track in the newlibc tree, not a bounded compiler feature); (2) the BLOCK-scope / static-local sized-and-unsized string-array init gap (a bounded `minic.y` follow-on to §8v — but no consumer yet, since vshell only needed file scope); (3) deepen the capstone — wire `interrupts.c`'s `set_interrupt_vector` for additional vectors / a real IR1 serial-RX ISR, or add the cooked `/dev/console` path (needs a different harness shape — `bm_stdio` aliases collide with the upstream drivers); (4) a far-code minic model for `interrupts.c` (the `__MINIC__` `isr_entry` currently handles only the near/small-code path).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8v session notes (2026-06-17)

### The pick
- §8u handoff: capstone on the upstream interrupt framework COMPLETE; four
  consumer-driven follow-ups.  At session start, confirmed follow-up (3) had
  already landed — newlibc main = 08637c3 (PR #22 merged), in sync with
  origin/main.  User (AskUserQuestion) chose **the minic-PARSE bucket**.
- Surveyed the bucket via build/newlibc-triage/sweep.sh: 7 TUs failed minic
  parse — vshell + 6 dos_tests.  vshell had NO _asm (a real grammar gap); the
  6 dos_tests are all Watcom `_asm { … }` blocks (a porting track).  Picked
  the vshell grammar gap (a clean, gateable compiler feature).

### The bug
- Bisected vshell's `error:334: parse error` to line 335:
  `static char cwd[64] = "/";` — a file-scope SIZED char array with a
  string-literal initializer.  Minimal repro matrix:
    - `char a[8] = "hi"`        -> PARSE ERROR  (the gap; static irrelevant)
    - `char a[]  = "hi"`        -> OK            (unsized string init)
    - `int  a[4] = {1,2,3,4}`   -> OK            (sized brace init)
  So ONLY sized + string-literal had no production.  Grammar: file-scope decl
  block had `'[' ']' '=' STR ';'` (minic.y ~8196) but no `'[' expr ']' '=' STR`.

### The fix (frontend minic.y, additive)
- New emit_string_array_sized(): natural = strlit_bytelen(idx) (incl NUL),
  total = count*SIZE(elem), pad = total-natural.  pad==0 -> reuse ini[idx]
  block verbatim; pad>0 -> splice `, z PAD` before the `}`; pad<0 -> die
  ("string initializer too long for array").  Routes static_local via
  emit_static_local, else adds a data global; var_set_arraybytes(name,total).
- New production `'[' expr ']' '=' STR ';'` next to the unsized STR rule,
  calling emit_string_array_sized(parsed_type, parsed_ident, $5->u.n,
  const_eval($2), 0).  Distinct lookahead vs the gaggr sibling -> conflicts
  stay 115/0.
- Data blocks verified byte-correct: char a[8]="hi" -> { b "hi", b 0 , z 5 };
  char b[3]="hi" -> { b "hi", b 0 } (exact); char c[64]="/" -> … z 62.

### Scope
- File scope only — what vshell needs (all 6 vshell errors were file-scope
  statics; the fix makes vshell compile end-to-end small AND medium).  Block
  scope is more broadly broken (even unsized `char a[]="hi"` fails in a fn
  body; dcls/stmt STATIC sized-STR also missing) — left as a documented
  bounded gap, no consumer yet.

### Gate
- sized_str_array_probe small+medium+compact+large+huge, one golden.
  Bug-loud: a git-stashed-fix minic gives parse error on the probe; the fixed
  one compiles clean.  Exercises cwd[64]="/", greeting[16]="hi" (big
  zero-fill), exactfit[3]="ab", emptied[8]="", NUL+slack zero checks, full-N
  rewrite.  Golden = contents/lengths/booleans, model-identical (verified
  small/compact/huge).

### Verification
- triage minic bucket 7 -> 6 (vshell PASS small + medium); remaining 6 = the
  Watcom-_asm{} dos_tests (porting track).
- conflicts 115/0 unchanged; make check green; MP compact body 689,760
  byte-identical (MP has no such decls); full gate 391/391.
- Frontend-only (minic.y) -> no emit audit, no Victor run.

### git scope (qbe)
- master 4c210a3: minic.y (emit_string_array_sized + the production), new
  sized_str_array_probe.c + golden, 5 test-dos.sh entries.  No emit/qbe, no
  newlibc-tree change.

### ⇒ Next session (consumer-driven, with the user)
- (1) the 6 Watcom-_asm{} dos_tests (a §8k-style inline-asm PORTING track in
  the newlibc tree, not a compiler feature);
- (2) block-scope / static-local sized-and-unsized string-array init (a bounded
  minic.y follow-on to §8v — no consumer yet);
- (3) deepen the capstone (interrupts.c set_interrupt_vector for more vectors /
  an IR1 serial ISR; or the cooked /dev/console path — needs a different
  harness shape, bm_stdio aliases collide);
- (4) a far-code minic model for interrupts.c (isr_entry currently near/small
  only).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

# Next session (the §8t handoff MERGED the `minic-asm-port` newlibc branch into `main` + retired the §6y `interrupts.h` shadow, completing the driver-sweep consolidation, and left four consumer-driven follow-ups; the user (AskUserQuestion) chose **deepen the capstone**.  §8u [2026-06-17, this session] **DROVE the `all_upstream_bm` driver-sweep capstone on the UPSTREAM interrupt framework itself — `drivers/interrupts.c`, the SEVENTH upstream newlibc TU — in place of the §8s capstone's hand-rolled local `install_isr` / `timer_isr` / `keyboard_isr` / `interrupts_enable`/`disable` scaffolding; the capstone now calls the upstream `interrupts_init()` (which builds each IVT entry with the model-aware `isr_entry` and writes it with `set_interrupt_vector`) and runs the UPSTREAM `timer_isr`/`keyboard_isr`/`serial_isr`, with a new phase 15/15b reading the timer + keyboard vectors back through the upstream `get_interrupt_vector` to prove `interrupts_init` installed exactly those ISRs; PASSES all 21 phases on MAME victor9k (battery still 48/48 — a deepen, not a new entry), `make check` green, NO qbe/minic/emit compiler source touched (→ compiler byte-identical, no emit audit, no MP byte-compare, test-dos UNCHANGED — a bare-metal-only gate).**  This required the ONE remaining §8k gas→nasm port on `interrupts.c` (follow-up item 2, done as a prerequisite): `isr_entry`'s near-code-model CS grab was gas/AT&T `__asm__ volatile("movw %%cs, %0" : "=r"(cs))` (the lone nasm-bucket remainder that kept `interrupts.c` from compiling under minic).  The fix is a `#if defined(__MINIC__)` Intel fork — `__asm__ volatile("mov word %0, cs" : "=m"(cs))` — which minic lowers to `mov word [bp-N], cs` (a valid 8086 segment-register-to-memory store; §8j operand resolution puts CS in the local's frame slot); verified end-to-end (preprocess → minic → qbe → `mov word [bp-18], cs`, zero gas leftovers, ISR prologues use the §6d ES-safe ABI).  **The newlibc edit was committed on a feature branch (`minic-isr-entry-port`, commit `ce21a72`) and FF-merged into newlibc `main` locally — NOT pushed** (the §8t precedent + the [[feedback_newlibc_use_prs]] rule: publish via PR, never a direct push; push/PR deferred to the user as follow-up item 4; newlibc `main` is now ahead 1 of `origin/main`).  The ia16-gcc / OpenWatcom builds take the unchanged `#else` gas path, byte-identical.  **Build-glue (one ADDITIVE rule in `tools/build-newlibc-baremetal.sh`):** link `$NL/drivers/interrupts.c` for a non-test-host / non-`bm_stdio` program that calls a WORD-BOUNDED `interrupts_init` (the `(^|[^_[:alnum:]])interrupts_init` grep does NOT match `bm_interrupts_init`) AND does not include `bm_interrupts.h` — so it fires ONLY for this capstone and NEVER collides with (a) the bm-mirror tests (`keyboard_bm`/`timer_bm`/… which call `bm_interrupts_init` and link `bm_interrupts.c`'s own `timer_isr`/`keyboard_isr`/`interrupts_enable`), or (b) the other `*_upstream_bm` tests (`timer`/`keyboard`/`pic_upstream_bm`, which define LOCAL ISRs + a local `install_isr` and never call `interrupts_init`), or (c) the test-host tests that `#include "interrupts.h"` (`pic_test`/`font_ram_test`, excluded by the `[ TESTHOST = 0 ]` guard).  **Link map proof:** the capstone now links `interrupts.obj` (the 7th upstream TU) + the six drivers (`timer`/`display`/`keyboard`/`console`/`pic`/`sasi`) + `font_data`/`block`, and the only `bm_*.obj` are `bm_console` (the always-linked harness serial TX) and `bm_crt0` — no driver mirror, no `bm_interrupts`.  Capstone code grew 27,427 → 27,849 B (still small, well under the 64 KB `_TEXT` ceiling).  **VERIFICATION:** `all_upstream_bm` `[ok]` (21 phases on MAME, golden re-captured for the new phase text — framework-driven phase 1/6 + the `get_interrupt_vector` readback phase 15/15b; all the §8s coverage — IMR 0xbb, live timer ticks under EOI, SASI read under live ISRs, disk→display + keyboard→display cross-driver flows, PIC mask gating — unchanged and still passing); the upstream-driver neighbors (`pic`/`console`/`keyboard_upstream_bm`), the bm-mirror tests (`keyboard_bm`/`timer_bm` — confirmed they keep `bm_interrupts.obj` and pull NO upstream `interrupts.obj`), and the test-host tests (`pic_test`/`font_ram_test`) all still `[ok]`; `make check` green.  **git scope:** qbe master `d98c94f` (capstone `.c` rewrite + re-captured golden + the one build-glue rule + the `tools/test-newlibc.sh` entry-comment §8u note — NO compiler/qbe/emit/minic, NO golden-logic-beyond-recapture, NO `test-dos` change); newlibc `main` `ce21a72` (the `interrupts.c` `isr_entry` `__MINIC__` fork, local FF-merge, not pushed).  **⇒ Next session — the driver sweep, its capstone, the merge/shadow consolidation, AND the upstream-interrupt-framework integration are all COMPLETE; remaining follow-ups are consumer-driven (pick with the user):** (1) the remaining minic-PARSE bucket — `vshell.c` + the 6 Watcom-`_asm{}` `dos_tests` (a park/rewrite porting track, not a bounded compiler feature); (2) deepen the capstone further — e.g. add the cooked `/dev/console` path (NOTE: that pulls `bm_stdio`/`bm_shim` whose unprefixed `console_*`/`keyboard_*`/`display_*` aliases collide with the upstream drivers, so it needs a different harness shape than the no-`bm_stdio` capstone — non-trivial), or wire `interrupts.c`'s `interrupts_init` for additional vectors / a real serial ISR on IR1; (3) push the merged `minic-asm-port` + the new `minic-isr-entry-port` (`interrupts.c` `__MINIC__` fork) newlibc work to `origin/main` via a PR if/when the user wants it published (newlibc `main` is ahead of origin, NOT pushed); (4) a far-code minic model for `interrupts.c` (the `__MINIC__` `isr_entry` currently handles only the near/small-code path, since minic does not define `__IA16_CMODEL_IS_FAR_TEXT`; a medium/far capstone would need the segment-from-the-far-pointer branch — no consumer yet).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8u session notes (2026-06-17)

### The pick
- §8t handoff: driver sweep + capstone + the minic-asm-port merge / §6y shadow
  retirement all COMPLETE; four consumer-driven follow-ups.  User
  (AskUserQuestion) chose **deepen the capstone** — specifically wire
  interrupts.c's set_interrupt_vector / IVT installer in place of the local
  install_isr now that upstream interrupts.h is the live header.

### What was built
- Rewrote minic/dos/newlibc/all_upstream_bm.c to drive the UPSTREAM interrupt
  framework (drivers/interrupts.c, the SEVENTH upstream TU) instead of the §8s
  local scaffolding.  Removed: extern qbe_get_cs, local interrupts_enable/
  disable, local timer_isr/keyboard_isr, local install_isr, kbd_isr_entries.
  Added: pic_init() + interrupts_init() bring-up (installs the upstream timer/
  keyboard/serial ISRs); interrupts_enable() (upstream sti); a get_interrupt_
  vector readback (phase 15/15b) proving interrupts_init wrote the timer +
  keyboard vectors to point at the upstream ISRs.
- Every other phase unchanged (IMR 0xbb, live ticks under EOI, SASI read under
  live ISRs, disk->display + keyboard->display cross-driver flows, PIC mask
  gating) and still passing.

### The prerequisite §8k port (newlibc)
- interrupts.c's isr_entry near-model CS grab was the lone remaining gas-asm
  site (movw %%cs, %0).  Ported to a #if defined(__MINIC__) Intel fork:
  __asm__ volatile("mov word %0, cs" : "=m"(cs)) -> minic lowers to
  mov word [bp-N], cs (valid 8086, §8j operand in the frame slot).
- Verified end-to-end before touching the capstone: preprocess (with the
  build's shiminc include set for stdint.h) -> minic small -> qbe i8086 ->
  mov word [bp-18], cs, 0 gas leftovers, ISR prologues use the §6d ES-safe ABI.
- Committed on feature branch minic-isr-entry-port (ce21a72), FF-merged into
  newlibc main LOCALLY (not pushed; [[feedback_newlibc_use_prs]] -> PR to
  publish, deferred to the user).  ia16-gcc/Watcom take the unchanged #else.

### Build-glue (one additive rule, build-newlibc-baremetal.sh)
- Link $NL/drivers/interrupts.c for a non-testhost / non-bm_stdio program that
  calls a WORD-BOUNDED interrupts_init ((^|[^_[:alnum:]])interrupts_init grep,
  does NOT match bm_interrupts_init) with a ! bm_interrupts.h guard.  Fires
  ONLY for this capstone; never collides with:
    - the bm-mirror tests (call bm_interrupts_init, link bm_interrupts.c's own
      timer_isr/keyboard_isr/interrupts_enable),
    - the other *_upstream_bm tests (local ISRs + local install_isr, no
      interrupts_init call),
    - the test-host tests that #include "interrupts.h" (TESTHOST=0 guard).
- Link map: interrupts.obj + 6 drivers + font_data/block; only bm_*.obj are
  bm_console (harness TX) + bm_crt0.  Code 27,427 -> 27,849 B (still small).

### Verification
- all_upstream_bm [ok]: 21 phases on MAME, golden re-captured for the new
  phase text (framework-driven phase 1/6 + get_interrupt_vector phase 15/15b).
- Neighbors undisturbed: pic/console/keyboard_upstream_bm [ok]; keyboard_bm/
  timer_bm [ok] and confirmed they keep bm_interrupts.obj / pull NO upstream
  interrupts.obj; pic_test/font_ram_test [ok].
- make check green.  NO compiler/qbe/emit/minic change -> no emit audit, no MP
  byte-compare, test-dos UNCHANGED (bare-metal-only gate).

### git scope
- qbe master d98c94f: all_upstream_bm.c rewrite, re-captured golden, the one
  build-newlibc-baremetal.sh rule, the test-newlibc.sh entry-comment §8u note.
- newlibc main ce21a72 (local FF, not pushed): interrupts.c isr_entry __MINIC__
  fork.

### ⇒ Next session (consumer-driven, with the user)
- Driver sweep + capstone + merge/shadow consolidation + upstream-interrupt-
  framework integration all COMPLETE.
- (1) remaining minic-PARSE bucket: vshell + 6 Watcom-`_asm{}` dos_tests
  (park/rewrite — a porting track, not a compiler feature);
- (2) deepen the capstone further: cooked /dev/console (needs a different
  harness shape — bm_stdio aliases collide with the upstream drivers), or a
  real IR1 serial ISR via interrupts_init;
- (3) push the merged minic-asm-port + minic-isr-entry-port newlibc work to
  origin/main via a PR if/when the user wants it published (main ahead of
  origin, NOT pushed);
- (4) a far-code minic model for interrupts.c (the __MINIC__ isr_entry handles
  only the near/small path; no consumer yet).
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8t and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
