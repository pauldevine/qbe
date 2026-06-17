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

# Next session (the §8q handoff COMPLETED the driver sweep — all six §8k-translated upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p, pic §8q) proven RUNNING bare-metal — and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **the minic-PARSE bucket**: implement `interrupts.c`'s ISR-function-pointer parameter declarator, the natural next COMPILER track.  §8r [2026-06-17, this session] **CLOSED the ISR-function-pointer parameter declarator — minic now parses newlibc `drivers/interrupts.c`'s `isr_entry` (`static void isr_entry(ivt_entry_t *entry, void __far __attribute__((interrupt)) (*isr)(void))`); the minic triage FAIL bucket dropped 8 → 7 at small AND medium; the fix is a frontend `minic.y` change → no emit audit; test-dos 381 → 386; conflicts UNCHANGED at 115 shift/reduce, 0 reduce/reduce; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  minic's grammar accepted only `type '(' '*' IDENT ')' '(' fptpar0 ')'` for a function-pointer parameter — a `__far` or `__attribute__((...))` qualifier between the pointee's return type and the `(*name)` declarator was a hard parse error (after the return `type`, the `TFAR` was shifted toward the `type TFAR '*'` pointer-type extension, which has no `'('` continuation; the attribute likewise had no path).  **THE FIX (frontend `minic.y`, additive, designed conflict-free via distinct lookaheads so the 115 baseline is unchanged):** a new NON-EMPTY `fpquals` qualifier run (`TFAR` / `fp_attr` / `TFAR fp_attr`) added to TWO new `par1` function-pointer-parameter productions — `type fpquals '(' '*' IDENT ')' '(' fptpar0 ')'` with and without the `',' par1` tail; the qualifiers are accepted and DROPPED (the pointer type is computed identically to the existing unqualified rule, `IDIR(FUNC($1))`), exactly as the established `type TFAR attropt IDENT` function-HEADER rule drops `__far` on an ISR definition.  **CRITICAL sub-bug avoided (the reason this is more than a trivial grammar add):** a function-pointer-PARAMETER's `__attribute__((interrupt))` MUST NOT set `cur_fn_interrupt` — `ansi_func_proto` reads `cur_fn_interrupt` AFTER the parameter list is parsed to decide `interrupt function` vs `function` linkage, so a leak would wrongly give the ENCLOSING `isr_entry` the ISR prologue/epilogue + `iret`, crashing it when called through the normal call/ret ABI.  So `fp_attr` saves `cur_fn_interrupt`/`cur_fn_weak` (into new globals `fp_saved_interrupt`/`fp_saved_weak`) in a mid-rule action before its `attrlist` runs and restores them in its reduce action — verified three ways: an interrupt-attributed param leaves a plain enclosing function PLAIN (`function $isr_entry`), a genuinely-`interrupt` enclosing function KEEPS its `export interrupt function` linkage with NO param, AND a genuinely-`interrupt` function with such a param keeps `interrupt` linkage (save/restore preserved it).  **GATED `isr_fnptr_param_probe` small+medium+compact+large+huge** (`minic/dos/examples/isr_fnptr_param_probe.c` + `minic/dos/tests/isr_fnptr_param_probe.golden.txt`, 5 entries in `tools/test-dos.sh`) — bug-loud TWO ways: on the unfixed compiler every `isr_entry`-shaped function is a parse error → the program will not build; and if the param attribute leaked, the enclosing `store_far_attr` would end in `iret` and crash before the final prints (so reaching `returned=1` proves it stayed an ordinary function).  The probe exercises all three qualifier forms (far+attr / far-only / attr-only fn-ptr params), two-call stability, the pointer address round-trip through the param, declarator distinctness, and normal return; the golden is booleans only → model-independent (verified byte-identical across all 5 models).  **`interrupts.c`'s REMAINING failure is now the NASM bucket** (`isr_entry`'s `#else`-branch gas/AT&T `__asm__ volatile("movw %%cs, %0" : "=r"(cs))` — the §8j operand resolution worked (`%0` → `[bp-18]`), only the instruction mnemonic is gas syntax) — that is the §8k gas→nasm PORTING track (newlibc tree), NOT a compiler bug, and `interrupts.c` is linked into NO image (the §8l..§8q tests each supply LOCAL ISRs precisely to avoid `interrupts.c`'s colliding `timer_isr`/`keyboard_isr`), so there is no consumer to run/verify a port against yet — deliberately out of scope this session.  The remaining minic-PARSE-bucket TUs are `vshell.c` + the 6 Watcom-`_asm{}` `dos_tests` (a separate park/rewrite track).  **VERIFICATION:** parse confirmed (the real `isr_entry` shape + all three qualifier forms parse; plain `int (*cb)(int,int)` and `void (*p)(void)` regression-parse unchanged); `cur_fn_interrupt` non-corruption (Cases A/B/C above); conflicts 115/0 unchanged; `make check` green; MP compact body 689,760 byte-identical (the new grammar never fires for MP); full gate **386/386**.  Frontend-only (`minic.y`) → no emit audit.  **⇒ Next session (consumer-driven, pick with the user):** (1) the remaining minic-PARSE bucket — `vshell.c` + 6 Watcom-`_asm{}` `dos_tests` (park or rewrite; these are a DIFFERENT porting track, not a bounded compiler feature); (2) FINISH `interrupts.c` end-to-end by porting its lone `#else`-branch gas asm to the `__MINIC__` Intel fork (a small §8k-track newlibc edit — but model-dependent CS-vs-far-ptr logic and no running consumer, so low value until an integrated image links it); (3) merge the `minic-asm-port` newlibc branch into `main` + retire the now-redundant §6y `interrupts.h` shadow; (4) integrate the six upstream drivers into one bare-metal program (the capstone, §8q follow-up (3)).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8r session notes (2026-06-17)

### The pick
- §8q handoff: driver sweep COMPLETE (six upstream drivers running bare-metal);
  three consumer-driven follow-ups.  User (AskUserQuestion) chose option (1)
  the **minic-PARSE bucket** — `interrupts.c`'s ISR-function-pointer parameter
  declarator, framed by the handoff as "a real bounded minic FRONTEND
  parse-feature — the natural next compiler track".

### The gap (isolated)
- newlibc `drivers/interrupts.c`'s IVT-installer helper:
    static void isr_entry(ivt_entry_t *entry,
                          void __far __attribute__((interrupt)) (*isr)(void))
  Under `-D__MINIC__` (non-Watcom), `ISR_HANDLER` = `__far __attribute__((interrupt))`.
- Isolation tests: `void (*p)(void)` PASSES; `void __far (*p)(void)`,
  `void __attribute__((interrupt)) (*p)(void)`, and the combined form all FAIL.
  Root: minic's fn-ptr-param production is `type '(' '*' IDENT ')' '(' fptpar0 ')'`
  with no slot for a `__far`/attribute qualifier between the return `type` and
  the `(*name)` declarator (the `type TFAR '*'` pointer extension dead-ends at `(`).

### The fix (frontend minic.y, additive)
- New non-empty `fpquals` run: `TFAR` | `fp_attr` | `TFAR fp_attr`, wired into
  TWO new `par1` productions (`type fpquals '(' '*' IDENT ')' '(' fptpar0 ')'`
  with and without `',' par1`).  Qualifiers accepted-and-dropped; pointer type
  `IDIR(FUNC($1))` exactly like the existing unqualified rule.  Distinct
  first-tokens (TFAR after `type` → shift; ATTRIBUTE → fp_attr) keep it
  conflict-free — verified conflicts stay 115 s/r, 0 r/r.
- CRITICAL: `fp_attr` = `ATTRIBUTE '(' '(' fp_attr_save attrlist ')' ')'` with
  `fp_attr_save` (empty mid-rule action) saving cur_fn_interrupt/cur_fn_weak and
  the reduce action restoring them — so a fn-ptr-PARAM's interrupt attribute
  does NOT leak onto the enclosing function's QBE linkage (ansi_func_proto reads
  cur_fn_interrupt AFTER params).  New globals `fp_saved_interrupt`/`fp_saved_weak`.

### Verification
- Parse: real isr_entry shape + far/attr/far+attr forms all parse; plain
  fn-ptr params regression-parse unchanged.
- Linkage non-corruption (the sub-bug):
    Case A: `static void isr_entry(...interrupt param...)` → `function $isr_entry`
            (NOT interrupt — param attr did NOT leak).
    Case B: `void __attribute__((interrupt)) myisr(void)` → `export interrupt function`.
    Case C: interrupt fn WITH an interrupt-attributed fn-ptr param → keeps
            `export interrupt function` (save/restore preserved it).
- Triage sweep: minic FAIL bucket 8 → 7 at small AND medium (interrupts.c gone
  from it; now in the nasm bucket = the §8k gas-asm porting track, not a
  compiler bug).
- `make check` green; conflicts 115/0; MP compact body 689,760 BYTE-IDENTICAL
  (no codegen drift — new grammar never fires for MP) → no Victor run;
  frontend-only → no emit audit.
- Gate: `isr_fnptr_param_probe` small+medium+compact+large+huge (golden
  model-independent booleans); full test-dos 381 → **386/386**.

### What was NOT done (scope, deliberate)
- `interrupts.c` end-to-end: its `#else`-branch `movw %%cs, %0` gas asm is
  unported (§8k track, newlibc tree); `interrupts.c` is linked into no image
  (the §8l..§8q tests use local ISRs to dodge its colliding ISRs), so no
  consumer to run a port against.
- The 6 Watcom-`_asm{}` dos_tests + vshell (separate park/rewrite track).

### ⇒ Next session (consumer-driven, with the user)
- (1) remaining minic-PARSE bucket: vshell + 6 Watcom-`_asm{}` dos_tests
  (park/rewrite — a porting track, not a bounded compiler feature);
- (2) finish interrupts.c end-to-end (port its lone gas asm to the __MINIC__
  fork — small newlibc edit, but model-dependent CS-vs-far-ptr, no running
  consumer yet);
- (3) merge minic-asm-port into newlibc main; retire the §6y interrupts.h shadow;
- (4) integrate the six upstream drivers into one bare-metal program (capstone).
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8q and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
