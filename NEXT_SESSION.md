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

# Next session (the §8s handoff WIRED ALL SIX §8k-translated upstream newlibc drivers into ONE bare-metal image running TOGETHER on MAME (the driver-sweep capstone, battery 47 → 48) and left four consumer-driven follow-ups; the user (AskUserQuestion) chose **merge `minic-asm-port` into newlibc `main` + retire the §6y `interrupts.h` shadow**.  §8t [2026-06-17, this session] **MERGED the `minic-asm-port` newlibc branch into `main` (fast-forward to `5b6b261`, local only — NOT pushed) AND RETIRED the §6y `interrupts.h` shadow (`minic/dos/newlibc/interrupts.h` removed) now that the merged upstream `drivers/interrupts.h` is `__MINIC__`-aware; NO qbe/minic/emit/build-script source touched (→ `make check` green by construction, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare), and the five affected bare-metal tests were re-verified `[ok]` on MAME victor9k through the upstream header.**  **The merge is a clean fast-forward:** `main` (`a65d15c`) was exactly the merge-base, `minic-asm-port` is `+1` commit (`5b6b261`, the §8k gas→nasm in-place port, `+127` lines all-additive across 9 files: console/display/keyboard/pic/sasi/timer `.c` + `interrupts.h` + the es_ss_diagnostic/serial_debug tests), so `git merge --ff-only` advanced `main` to `5b6b261` with no conflicts — and because it is a fast-forward, `main`'s working-tree content is byte-IDENTICAL to `minic-asm-port`, so the qbe build (which reads `$NL` = `~/projects/newlibc/phase3_newlib` at whatever is checked out) sees NO change from the merge itself.  `main` is now `ahead 2` of `origin/main`, NOT pushed (house rule: no push without ask).  **The §6y shadow was redundant** because the merged upstream `interrupts.h` already carries the §8k `#if defined(__MINIC__)` fork: SAVE_ES/RESTORE_ES become no-ops and the `static inline get_interrupt_vector` collapses to a plain far-pointer IVT read under `__MINIC__` — exactly the shadow's behavior — and `-D__MINIC__` has been on the bare-metal build path since §8l.  **Include-resolution nuance that bounded the blast radius:** for quoted `#include "interrupts.h"`, clang searches the *including file's own directory* before the `-I` paths, so the upstream driver `.c` files (in `$NL/drivers/`) ALWAYS resolved to the upstream `interrupts.h` (same dir) — they never used the shadow; only the qbe test files that name `"interrupts.h"` resolved to the shadow via `-I$NLC_DIR` (searched before `-I$NL/drivers`).  So retiring the shadow affects exactly FIVE TUs: the three `*_upstream_bm` tests in `minic/dos/newlibc/` (`all_upstream_bm`, `keyboard_upstream_bm`, `pic_upstream_bm`) plus the two test-host upstream tests in `$NL/tests/` that `#include "interrupts.h"` (`pic_test`, `font_ram_test`).  All five use the header only for `ISR_HANDLER`/`ivt_entry_t`/the `interrupts_enable`+`interrupts_disable` declarations (the `*_upstream_bm` tests DEFINE enable/disable locally as `sti`/`cli` stubs; `pic_test`/`font_ram_test` use no symbol from it) — NONE use `get_interrupt_vector`/`set_interrupt_vector` — so shadow→upstream is observably identical for every consumer.  **VERIFICATION:** all five rebuilt clean and PASS on MAME `[ok]` through the battery harness (`tools/test-newlibc.sh all_upstream_bm keyboard_upstream_bm pic_upstream_bm pic_test font_ram_test` → 5 passed, 0 failed) against the upstream header; `all_upstream_bm` code is 27,393 B (vs §8s's 27,427 — the upstream `get_interrupt_vector`'s dead `es_save` static GC'd 34 B differently, observable serial output identical and golden byte-matched); `make check` green; the stale `"§6y shadow"` comments in the six `*_upstream_bm`/`sasi`/`console` test files + the `pic_test` block in `tools/test-newlibc.sh` were rewritten to `"merged upstream (§8k __MINIC__ fork)"`.  **git scope:** `D minic/dos/newlibc/interrupts.h` (the retired shadow) + comment-only edits to `all_upstream_bm.c`/`keyboard_upstream_bm.c`/`pic_upstream_bm.c`/`sasi_upstream_bm.c`/`console_upstream_bm.c` + `tools/test-newlibc.sh` — NO compiler/qbe/emit/minic, NO build-script-logic, NO golden change → compiler byte-identical, no emit audit, no MP byte-compare, test-dos UNCHANGED.  The newlibc tree change is the local FF merge only (uncommitted-to-origin).  **⇒ Next session — the driver sweep, its capstone, AND the merge/shadow-retirement consolidation are all COMPLETE; remaining follow-ups are consumer-driven (pick with the user):** (1) the remaining minic-PARSE bucket — `vshell.c` + the 6 Watcom-`_asm{}` `dos_tests` (a park/rewrite porting track, not a bounded compiler feature); (2) FINISH `interrupts.c` end-to-end by porting its lone `#else`-branch gas asm (`movw %%cs, %0`) to the `__MINIC__` Intel fork (a small §8k-track newlibc edit — but model-dependent CS-vs-far-ptr logic and `interrupts.c` is linked into no image, so low value until an integrated image needs its IVT installer); (3) deepen the capstone — e.g. wire `interrupts.c`'s `set_interrupt_vector`/IVT-installer in place of the local `install_isr` now that upstream `interrupts.h` is the live header, or add the cooked `/dev/console` path now that keyboard+display+console coexist; (4) push the merged `minic-asm-port` work to `origin/main` if/when the user wants it published.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8t session notes (2026-06-17)

### The pick
- §8s handoff: driver-sweep capstone COMPLETE (all six §8k upstream drivers
  running together bare-metal); four consumer-driven follow-ups.  User
  (AskUserQuestion) chose **merge `minic-asm-port` into newlibc `main` + retire
  the §6y `interrupts.h` shadow** (a consolidation: land the §8k port and drop
  the now-redundant workaround).

### The merge (newlibc repo, ~/projects/newlibc)
- `main` (a65d15c) was exactly the merge-base; `minic-asm-port` is +1 commit
  (5b6b261, the §8k port: +127 lines all-additive across console/display/
  keyboard/pic/sasi/timer.c + interrupts.h + es_ss_diagnostic/serial_debug
  tests).  `git checkout main && git merge --ff-only minic-asm-port` →
  fast-forward, no conflicts.  main now ahead 2 of origin/main, NOT pushed.
- Because it is a FF, main's working tree == minic-asm-port byte-for-byte, so
  the qbe build sees no change from the merge.

### Retiring the §6y shadow (qbe repo)
- Removed `minic/dos/newlibc/interrupts.h` (git rm).  Redundant because the
  merged upstream `$NL/drivers/interrupts.h` carries the §8k
  `#if defined(__MINIC__)` fork: SAVE_ES/RESTORE_ES → no-ops, and
  get_interrupt_vector collapses to a plain far-ptr read under __MINIC__ (which
  is on the bare-metal build path since §8l) — exactly the shadow's behavior.
- Include-resolution nuance bounding the blast radius: quoted includes search
  the including file's own dir first, so the upstream driver .c files (in
  $NL/drivers/) ALWAYS used the upstream interrupts.h, never the shadow; only
  the qbe test files resolved to the shadow via -I$NLC_DIR (before -I$NL/drivers).
- Affected TUs = exactly five: all_upstream_bm, keyboard_upstream_bm,
  pic_upstream_bm (minic/dos/newlibc/, include "interrupts.h") + pic_test,
  font_ram_test (test-host, $NL/tests/, include "interrupts.h" → shadow via -I).
  All use only ISR_HANDLER/ivt_entry_t/interrupts_enable+disable decls (the
  *_upstream_bm tests define enable/disable locally; pic_test/font_ram_test use
  no symbol) — none use get_interrupt_vector — so shadow→upstream is observably
  identical.
- bm_interrupts.h is a SEPARATE file (bare-metal interrupts mirror) and stays.
- No functional build-script reference to the shadow existed (all refs were
  comments); updated the stale "§6y shadow" comments in the six test files +
  the pic_test block in tools/test-newlibc.sh to "merged upstream (§8k
  __MINIC__ fork)".

### Verification
- Build-only: all five affected tests link clean with the upstream header.
- MAME battery: tools/test-newlibc.sh all_upstream_bm keyboard_upstream_bm
  pic_upstream_bm pic_test font_ram_test → 5 passed, 0 failed, 0 skipped — all
  [ok] against the upstream interrupts.h.
- all_upstream_bm code 27,393 B (vs §8s 27,427 — upstream get_interrupt_vector's
  dead es_save static GC'd 34 B differently; serial output identical, golden
  byte-matched).
- make check green; NO compiler/qbe/emit/minic/build-script-logic change → no
  emit audit, no MP byte-compare, test-dos UNCHANGED.

### git scope (qbe)
- D minic/dos/newlibc/interrupts.h; comment-only edits to all_upstream_bm.c,
  keyboard_upstream_bm.c, pic_upstream_bm.c, sasi_upstream_bm.c,
  console_upstream_bm.c, tools/test-newlibc.sh.  No goldens, no logic.

### ⇒ Next session (consumer-driven, with the user)
- Driver sweep + capstone + the merge/shadow consolidation all COMPLETE.
- (1) remaining minic-PARSE bucket: vshell + 6 Watcom-`_asm{}` dos_tests
  (park/rewrite porting track, not a compiler feature);
- (2) finish interrupts.c end-to-end (port its `#else` gas `movw %%cs,%0` to the
  __MINIC__ fork — small newlibc edit, model-dependent, no running consumer yet);
- (3) deepen the capstone (use interrupts.c's set_interrupt_vector IVT installer
  in place of the local install_isr now that upstream interrupts.h is live; or
  add the cooked /dev/console path);
- (4) push the merged minic-asm-port to origin/main if/when the user wants it
  published.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8s and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
