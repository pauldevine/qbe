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

# Next session (the §8p handoff proved the bare-metal-RUN end-state for five upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p) and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **finish the driver sweep — pic**, the LAST §8k-translated driver.  §8q [2026-06-17, this session] **WIRED newlibc's OWN `drivers/pic.c` (the 8259A PIC driver) into a bare-metal image IN PLACE OF the hand-mirrored `bm_pic.c` and RAN it on MAME victor9k — the SIXTH and FINAL proof a §8k-translated upstream phase3 driver RUNS bare-metal (after timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p), COMPLETING the driver sweep; the new `pic_upstream_bm` battery test is a FIRST-RUN PASS (bare-metal battery 46 → 47), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/pic_upstream_bm.c`, modeled on §6f's `pic_bm.c` re-pointed at the upstream API + the §8n/§8o ISR pattern) drives the UPSTREAM `pic_init`/`pic_enable_irq`/`pic_disable_irq`/`pic_send_eoi`/`pic_get_mask`/`pic_set_mask` API.  **The collision worry the §8o/§8p handoffs flagged ("bm_pic.c is so widely linked the guard must be tight") turned out clean — NO if/elif needed:** upstream `pic.c` defines `pic_*` and the mirror `bm_pic.c` defines `bm_pic_*` (NO name overlap, unlike §8o's sasi where both define `sasi_register`, which forced the if/elif), so a standalone guarded `if` rule (the §8l/§8m/§8n/§8p shape) suffices — even an accidental double-link would not clash.  **The whole test runs UNDER A LIVE TIMER ISR:** a local `timer_isr` is the compiler-emitted ES-safe iret ABI (`__attribute__((interrupt))` → QBE `interrupt` linkage → the §6d i8086 prologue/epilogue) routing each IR2 tick through the UPSTREAM `timer_tick_handler()` (drivers/timer.c, §8l, ALSO linked) and acknowledging it with the UPSTREAM `pic_send_eoi(IRQ_TIMER)` — so pic.c's EOI path is exercised every tick (continuous ticks across the run are the EOI proof; a broken EOI yields one tick then silence).  `pic_send_eoi` loads ES (0xE000) for the memory-mapped command register inside the ISR; that is ES-safe ONLY because the §6d prologue saved ES — the same §8k SAVE_ES-drop story §8o validated for sasi, here re-validated for pic, **two §8k-translated upstream drivers (pic + timer) running together under live interrupts**.  What is also exercised live is the §8k port of `pic.c`'s `interrupt_flags_save` → the Intel `#if defined(__MINIC__)` → `pushf` / `pop word %0` / `cli` fork, with the §8j extended-asm operand `%0` resolving to a frame slot — verified in the emitted asm as `pushf` / `pop word [bp-10]` / `cli`, ZERO real gas leftovers (the lone `%0`-bearing line is the `.ascii` template-string region, not an instruction) — and `pic.c`'s `pic_delay` taking the nasm `jmp short $+2` two-jump fork + its `SAVE_ES`/`RESTORE_ES` collapsing to no-ops via the §6y shadow `interrupts.h`.  **TWO link-time gaps supplied as STUBS in the test (the §8n pattern):** upstream `pic.c`'s `pic_init()` calls `interrupts_disable()` and its `interrupt_flags_restore()` calls `interrupts_enable()` — both defined only in upstream `drivers/interrupts.c`, which we deliberately do NOT link (it carries its own `timer_isr` that would collide with the local one), so the test supplies the one-liners the §6y shadow `interrupts.h` declares: `void interrupts_enable(void){ __asm__ volatile("sti"); }` + `void interrupts_disable(void){ __asm__ volatile("cli"); }`.  Note pic.c provides its OWN full 8259A re-init (`pic_init`: ICW1 0x17, base 0x40, clear in-service bits, mask all 0xFF), so the test calls upstream `pic_init()` for the §6d-mandatory pre-`sti` re-init rather than `bm_pic_init()` — `bm_pic.c` is NOT linked at all.  **ONE build-glue change to `tools/build-newlibc-baremetal.sh` (additive, mirroring the §8l/§8m/§8n/§8p SUPPORT_TUS rules):** a non-test-host, non-`bm_stdio` program including upstream `"pic.h"` (the leading-quote `'"pic\.h"'`, which does NOT match `"bm_pic.h"`) links `$NL/drivers/pic.c`; the guard `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'` keeps the rule off the mirror tests, which pull `bm_pic.c` via their own `bm_*.h` grep rules (`bm_interrupts.h`/`bm_pic.h`/`bm_keyboard.h`/`bm_serial.h`/`bm_tty.h`/`bm_stdio.h`).  Verified the routing via the link map across neighbors: `pic_upstream_bm` → `pic.obj`+`timer.obj` (NO `bm_pic.obj`); `pic_bm` (includes `bm_pic.h`) → `bm_pic.obj`; `keyboard_upstream_bm` (includes `bm_pic.h`) → `bm_pic.obj`; the §6y upstream `pic_test` (test-host, includes `"pic.h"` but `TESTHOST=1` fails the guard) → `bm_pic.obj` via the bm_stdio set — so my upstream rule fires ONLY for `pic_upstream_bm`.  `-D__MINIC__` (added by §8l) makes `pic.c` take the Intel forks.  The golden (`minic/dos/tests/pic_upstream_bm.golden.txt`) is deterministic (fixed phase text + booleans, the IMR is the deterministic 0xFB after `pic_init`+`timer_init` — all masked, only IR2 open); bare-metal ONLY (the DOS host has no live 8259/8253); small model (7281 B code); 35 s budget; FIRST-RUN PASS on MAME — all 12 phases (upstream pic_init + install timer ISR / upstream timer_init / sti / IMR==0xFB / ticks advance via pic_send_eoi / pic_disable_irq(5) sets IR5 bit / timer undisturbed / pic_enable_irq(5) clears it / pic_set_mask round-trip / pic_disable_irq(2) FREEZES ticks / pic_enable_irq(2) RESUMES ticks / continuous-ticks EOI proof).  **VERIFICATION:** `pic_upstream_bm` `[ok]` (battery 46 → 47), `pic_bm` `[ok]` (the direct mirror, undisturbed) AND `console_upstream_bm` `[ok]` (the §8p neighbor, undisturbed) through the battery harness (build + MAME run + golden-diff), build-routing confirmed for `keyboard_upstream_bm`/`pic_test` (still `bm_pic.obj`), `make check` green; only build script + a new test + golden + one battery entry changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — the driver sweep is COMPLETE: all six §8k-translated drivers (timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p, pic §8q) now proven RUNNING bare-metal; the remaining follow-ups are consumer-driven (pick with the user):** (1) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature — the natural next compiler track; `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (2) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware (the shadow's get_interrupt_vector/SAVE_ES no-ops are exactly what the ported upstream needs); (3) wire the upstream drivers together into a real integrated bare-metal program (rather than one-driver-at-a-time tests) — the natural capstone now that all six run individually.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8q session notes (2026-06-17)

### The pick
- §8p handoff: bare-metal-RUN proven for five drivers (timer §8l, display §8m,
  keyboard §8n, sasi §8o, console §8p); three consumer-driven follow-ups.  User
  (AskUserQuestion) chose option (1) "finish the sweep: pic" — the LAST
  §8k-translated driver, the 8259A PIC.

### What was built
- New test `minic/dos/newlibc/pic_upstream_bm.c` (modeled on §6f pic_bm.c
  re-pointed at the upstream API + the §8n/§8o ISR pattern): drives the UPSTREAM
  pic_init / pic_enable_irq / pic_disable_irq / pic_send_eoi / pic_get_mask /
  pic_set_mask API.  Links newlibc's OWN drivers/pic.c IN PLACE OF bm_pic.c.
  Raw serial output (bm_puts/bm_puthex), NO bm_stdio (the §8l..§8p shape).
- Upstream pic.c provides its OWN full 8259A re-init (pic_init: ICW1 0x17, base
  0x40, clear in-service bits, mask all 0xFF), so the test calls upstream
  pic_init() for the §6d-mandatory pre-sti re-init — bm_pic_init is NOT used,
  bm_pic.c is NOT linked.
- HEADLINE: the whole test runs UNDER A LIVE TIMER ISR (upstream timer.c §8l,
  also linked) whose EOI goes through the UPSTREAM pic_send_eoi(IRQ_TIMER) every
  tick.  pic_send_eoi loads ES (0xE000) for the E000:0000 command register
  inside the ISR; ES-safe only via the §6d prologue's ES save — the §8k SAVE_ES
  drop re-validated for pic (as §8o did for sasi).  TWO §8k drivers (pic+timer)
  under live interrupts.  Continuous ticks across the run = the EOI proof.
- §8k Intel forks exercised live, verified in generated asm: interrupt_flags_save
  → `pushf` / `pop word [bp-10]` / `cli` (§8j %0 operand → frame slot), ZERO real
  gas leftovers (the one %0-bearing line is the `.ascii` template string, not an
  instruction); pic_delay → `jmp short $+2`; SAVE_ES/RESTORE_ES → no-ops via the
  §6y shadow interrupts.h.

### Collision analysis (the §8o/§8p handoffs' worry) — turned out clean, NO if/elif
- Upstream pic.c defines pic_* ; the mirror bm_pic.c defines bm_pic_* — NO name
  overlap (unlike §8o sasi, where both define sasi_register and the if/elif was
  mandatory).  So a standalone guarded `if` rule (the §8l/§8m/§8n/§8p shape)
  suffices; even an accidental double-link would not clash.
- bm_pic.c is widely linked (bm_interrupts.h / bm_pic.h / bm_keyboard.h /
  bm_serial.h / bm_tty.h / bm_stdio.h all pull it), but the guard plus the
  leading-quote `'"pic\.h"'` pattern (≠ `"bm_pic.h"`) keeps the upstream rule
  firing ONLY for pic_upstream_bm.  Verified via the link map:
  pic_upstream_bm → pic.obj + timer.obj (no bm_pic.obj); pic_bm → bm_pic.obj;
  keyboard_upstream_bm → bm_pic.obj; pic_test (test-host) → bm_pic.obj.

### build-newlibc-baremetal.sh change (build-glue ONLY, additive)
- ONE new rule mirroring §8l/§8m/§8n/§8p: a non-test-host, non-bm_stdio program
  including upstream "pic.h" (leading-quote `'"pic\.h"'` does NOT match
  `"bm_pic.h"`) links $NL/drivers/pic.c, guarded
  `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'`.

### TWO link-time gaps supplied as STUBS in the test (the §8n pattern)
- pic.c's pic_init() calls interrupts_disable() and interrupt_flags_restore()
  calls interrupts_enable() — both defined only in upstream drivers/interrupts.c
  (NOT linked: it carries its own timer_isr that would collide).  The test
  supplies `void interrupts_enable(void){ __asm__ volatile("sti"); }` +
  `void interrupts_disable(void){ __asm__ volatile("cli"); }` (declared by the
  §6y shadow interrupts.h).

### Verification
- pic_upstream_bm: FIRST-RUN PASS on MAME, 12 phases (upstream pic_init + timer
  ISR install / upstream timer_init / sti / IMR==0xFB / ticks advance via
  pic_send_eoi / pic_disable_irq(5) sets IR5 / timer undisturbed /
  pic_enable_irq(5) clears IR5 / pic_set_mask round-trip / pic_disable_irq(2)
  FREEZES ticks / pic_enable_irq(2) RESUMES ticks / continuous-ticks EOI proof).
  small (7281 B); 35 s budget.  Golden deterministic (fixed text + booleans;
  IMR is the deterministic 0xFB after pic_init+timer_init).
- Battery harness: pic_upstream_bm [ok] (battery 46 → 47); pic_bm [ok] (direct
  mirror, undisturbed) and console_upstream_bm [ok] (§8p neighbor, undisturbed).
- make check green; compiler byte-identical (only build script + new test +
  golden + battery entry changed) → no emit audit, no MP byte-compare, test-dos
  unchanged.  Bare-metal ONLY (no live 8259/8253 on DOS).  newlibc tree
  untouched (§8k port already committed on minic-asm-port @ 5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- Driver sweep COMPLETE: all six §8k drivers (timer §8l, display §8m, keyboard
  §8n, sasi §8o, console §8p, pic §8q) now RUNNING bare-metal.
- (1) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature — the natural next compiler track); vshell + 6 dos_tests
  Watcom _asm{} (park/rewrite);
- (2) decide: merge minic-asm-port into newlibc main; retire the redundant §6y
  minic/dos/newlibc/interrupts.h shadow now upstream interrupts.h is minic-aware;
- (3) integrate the six upstream drivers into a real bare-metal program (capstone
  — they all run individually now).
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8p and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
