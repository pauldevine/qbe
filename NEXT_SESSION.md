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

# Next session (the §8r handoff CLOSED the minic-PARSE-bucket ISR-function-pointer parameter declarator (the last bounded compiler track) and noted the driver sweep was complete — all six §8k-translated upstream drivers proven RUNNING bare-metal individually (timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p, pic §8q) — leaving four consumer-driven follow-ups; the user (AskUserQuestion) chose **the driver capstone**: integrate the six upstream drivers into ONE bare-metal program.  §8s [2026-06-17, this session] **WIRED ALL SIX of newlibc's OWN drivers into a SINGLE bare-metal image and RAN them TOGETHER on MAME victor9k — the Phase-6 driver-sweep capstone, where §8l..§8q each ran one upstream driver in ISOLATION (in place of its `bm_*.c` mirror); the new `all_upstream_bm` battery test PASSES all 19 phases (bare-metal battery 47 → 48), NO qbe/minic/emit compiler source touched AND NO build-glue change needed (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/all_upstream_bm.c`, ~310 lines) links and runs newlibc's OWN `drivers/timer.c` + `display.c` (+ `font_data.c`) + `keyboard.c` + `console.c` + `pic.c` + `sasi.c` (+ `block.c`) — the §8k gas→nasm in-place ports — together in one small-model image; the link map confirms `timer/display/keyboard/console/pic/sasi/block/font_data.obj` are all present and the ONLY `bm_*.obj` is `bm_console.obj` (the always-linked harness serial TX path), with NO `bm_timer`/`bm_display`/`bm_keyboard`/`bm_pic`/`bm_sasi` mirror.  **KEY ARCHITECTURAL PAYOFF — zero build-glue change:** the §8l/§8m/§8n/§8p/§8q `SUPPORT_TUS` rules in `build-newlibc-baremetal.sh` are each an INDEPENDENT, ADDITIVE `if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' && grep -q '"<drv>\.h"'` (plus the §8o sasi if/elif), so a program that `#include`s all six upstream headers fires all six rules and pulls all six drivers + `block.c` automatically (the dedup pass collapses `block.c`'s double-add) — the rules were DESIGNED additive for exactly this, so the capstone needed only a new test + golden + one battery entry.  **Two ISR-driven drivers run live the WHOLE test** (timer IR2 + keyboard IR6, each the §6d compiler-emitted ES-safe iret ABI acknowledged through the UPSTREAM `pic_send_eoi`), `pic.c`'s `pic_init` does the §6d-mandatory full 8259A re-init before `sti`, and the SASI sector read runs UNDER those live ISRs (re-validating the §8k SAVE_ES drop, §8o).  **Cross-driver data flows no single-driver test could exercise:** the SASI LBA-0 disk label (`"tandon_703_mame"`) is written to the CRT through `display.c` and read back from VRAM (disk→display), and the typed `"v9k"` (V9K_KEYPOST) is echoed to the CRT through `display.c` and read back from VRAM (keyboard→display).  Integrated state checks: IMR is `0xBB` (IR2 timer + IR6 keyboard both unmasked — vs the §8q single-driver `0xFB`), masking IR2 freezes ticks / unmasking resumes them, and a final all-drivers-alive check.  console.c's TX path is proven by a captured `console_puts` line on channel A; console.c's dead cooked-console externals (`console_dev_read`→`keyboard_getc`, `console_echo_input`→`display_putc`) resolve to the REAL `keyboard.c`/`display.c` definitions in the integrated image (so the §8p stub functions were deliberately NOT supplied — they would collide).  **TWO bugs found and fixed during bring-up (both in the TEST, the drivers were correct):** (1) a `/*…*/` comment in the new file's header contained the literal token `sasi_*/block_*`, whose embedded `*/` PREMATURELY CLOSED the block comment — clang `-E -P` does NOT strip comments here, so the rest of the comment leaked into the preprocessed output as code and minic died `error:0: parse error` (no line number); the `*/` had to be broken (`sasi_* + block_*`).  LESSON: a `*/` substring anywhere inside a block comment in a minic-bound TU closes it early — watch `foo_*/bar` glob lists in comments.  (2) the original phase 13 was a consuming "no key pending at start" check (`keyboard_getc_nonblock() < 0`); because the slow SASI read on the 5 MHz 8088 pushes phase 13 PAST the ~3 s V9K_KEYPOST_DELAY, the harness had already typed and that check CONSUMED-and-DISCARDED the first char (`v`), leaving only `"9k"` → phases 14/15/17 failed.  FIX: removed that racy check (it is already gated by §8n `keyboard_upstream_bm`) and let the keyboard accumulation loop drain all three chars from the interrupt-buffered ring whenever they land — robust regardless of when the keypost fires relative to the phases.  Golden (`minic/dos/tests/all_upstream_bm.golden.txt`, 22 lines) is deterministic (booleans + the captured `console_puts` line + the received chars), verified byte-identical across runs; bare-metal ONLY (no live 8253/8259/VRAM/SASI on the DOS host); small model (27,427 B code, well under the 64 KB `_TEXT` ceiling; data+bss 46,890 B incl. the 8 KB `victor_font[]` table, under the 64 KB DGROUP); `hd` disk field (V9K_HARD_DISK scratch copy — though the capstone is READ-ONLY, `allow_writes=0`); 45 s budget (the keypost-vs-slow-SASI margin).  **VERIFICATION:** `all_upstream_bm` `[ok]` (battery 47 → 48), `pic_upstream_bm`/`console_upstream_bm` `[ok]` (neighbors undisturbed) through the battery harness (build + MAME run + golden-diff); `make check` green; git scope is exactly a new test `.c` + a new golden + one `tools/test-newlibc.sh` entry — NO compiler/qbe/emit/minic, NO build-script, NO newlibc-tree change (the §8k port stays committed on branch `minic-asm-port` @ `5b6b261`), so compiler byte-identical → no emit audit, no MP byte-compare, test-dos UNCHANGED.  **⇒ Next session — the driver sweep AND its capstone are COMPLETE; all follow-ups are consumer-driven (pick with the user):** (1) the remaining minic-PARSE bucket — `vshell.c` + 6 Watcom-`_asm{}` `dos_tests` (a park/rewrite porting track, not a bounded compiler feature); (2) FINISH `interrupts.c` end-to-end by porting its lone `#else`-branch gas asm (`movw %%cs, %0`) to the `__MINIC__` Intel fork (a small §8k-track newlibc edit — but model-dependent CS-vs-far-ptr logic and `interrupts.c` is linked into no image, so low value until an integrated image needs its IVT installer); (3) MERGE the `minic-asm-port` newlibc branch into `main` + retire the now-redundant §6y `interrupts.h` shadow now that upstream `interrupts.h` is minic-aware; (4) deepen the capstone — e.g. wire `interrupts.c`'s `set_interrupt_vector`/IVT-installer in place of the local `install_isr`, or add the cooked `/dev/console` path now that keyboard+display+console coexist.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8s session notes (2026-06-17)

### The pick
- §8r handoff: minic-PARSE ISR-fn-ptr param closed (last bounded compiler
  track); driver sweep complete (six upstream drivers running bare-metal
  individually); four consumer-driven follow-ups.  User (AskUserQuestion)
  chose **the driver capstone**: integrate the six into ONE bare-metal program.

### What was built
- New test `minic/dos/newlibc/all_upstream_bm.c` (~310 lines): ONE small-model
  bare-metal image linking and running newlibc's OWN timer.c + display.c
  (+ font_data.c) + keyboard.c + console.c + pic.c + sasi.c (+ block.c) — the
  §8k gas→nasm ports — TOGETHER.  Raw serial framing via bm_console.c's bm_puts
  (the always-linked harness path); NO bm_stdio.  19 phases.
- Link map proof: timer/display/keyboard/console/pic/sasi/block/font_data.obj
  all present; the ONLY bm_*.obj is bm_console.obj (no driver mirror linked).

### KEY payoff — zero build-glue change
- The §8l/§8m/§8n/§8p/§8q SUPPORT_TUS rules in build-newlibc-baremetal.sh are
  each an independent additive `if TESTHOST=0 && !bm_stdio.h && grep '"<drv>.h"'`
  (+ the §8o sasi if/elif).  Including all six upstream headers fires all six
  rules → all six drivers + block.c pulled automatically (dedup collapses the
  double block.c).  Designed additive for exactly this — only a new test +
  golden + one battery entry were needed.

### Integrated coverage (beyond any single-driver test)
- Two ISR-driven drivers live the whole test: timer IR2 + keyboard IR6, §6d
  ES-safe iret ABI, acknowledged through the UPSTREAM pic_send_eoi; pic_init
  does the §6d full 8259A re-init pre-sti; SASI read runs UNDER the live ISRs
  (re-validates the §8k SAVE_ES drop, §8o).
- Cross-driver flows: SASI LBA-0 label "tandon_703_mame" → display → VRAM
  readback (disk→display); typed "v9k" → display → VRAM readback (keyboard→
  display).
- Integrated state: IMR == 0xBB (IR2 timer + IR6 keyboard both open, vs the §8q
  single-driver 0xFB); pic_disable_irq(2) freezes ticks / pic_enable_irq(2)
  resumes; final all-drivers-alive check.
- console.c TX proven by a captured console_puts line on channel A; its dead
  cooked-path externals (console_dev_read→keyboard_getc, console_echo_input→
  display_putc) resolve to the REAL keyboard.c/display.c defs — so the §8p stub
  functions were deliberately NOT supplied (they would collide).

### Two bugs found+fixed during bring-up (both in the TEST; drivers correct)
- (1) PARSE: the header comment contained `sasi_*/block_*`; the embedded `*/`
  PREMATURELY CLOSED the block comment (clang -E -P does NOT strip comments
  here), leaking the rest as code → minic `error:0: parse error` (no line).
  FIX: broke the `*/` (`sasi_* + block_*`).  LESSON: a `*/` substring anywhere
  in a block comment in a minic-bound TU closes it early — watch glob lists
  like `foo_*/bar` in comments.
- (2) KEYPOST RACE: the original phase 13 "no key pending at start" check
  (consuming keyboard_getc_nonblock) landed AFTER the ~3 s V9K_KEYPOST_DELAY
  because the slow SASI read pushes it past 3 emulated seconds, so it
  consumed-and-DISCARDED the first char ('v') → only "9k" left → phases
  14/15/17 failed.  FIX: removed the racy check (already gated by §8n) and let
  the accumulation loop drain all three from the interrupt-buffered ring
  whenever they land.

### Verification
- all_upstream_bm: PASS all 19 phases on MAME, deterministic byte-identical
  across runs; small (27,427 B code; data+bss 46,890 B incl. 8 KB victor_font);
  hd disk (read-only, allow_writes=0); 45 s budget.
- Battery harness: all_upstream_bm [ok] (47 → 48); pic_upstream_bm,
  console_upstream_bm [ok] (neighbors undisturbed).
- make check green; git scope = new test .c + new golden + one
  tools/test-newlibc.sh entry.  NO compiler/qbe/emit/minic, NO build-script, NO
  newlibc-tree change → compiler byte-identical, no emit audit, no MP
  byte-compare, test-dos UNCHANGED.  Bare-metal ONLY.

### ⇒ Next session (consumer-driven, with the user)
- Driver sweep + capstone COMPLETE.
- (1) remaining minic-PARSE bucket: vshell + 6 Watcom-`_asm{}` dos_tests
  (park/rewrite — a porting track, not a compiler feature);
- (2) finish interrupts.c end-to-end (port its lone `#else` gas `movw %%cs,%0`
  to the __MINIC__ fork — small newlibc edit, model-dependent, no running
  consumer yet);
- (3) merge minic-asm-port into newlibc main; retire the §6y interrupts.h shadow;
- (4) deepen the capstone (use interrupts.c's set_interrupt_vector IVT installer
  in place of the local install_isr; or add the cooked /dev/console path).
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8r and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
