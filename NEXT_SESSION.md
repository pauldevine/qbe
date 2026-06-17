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

# Next session (the §8o handoff proved the bare-metal-RUN end-state for four upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o) and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **continue the driver sweep**, specifically serial/console — wire the §8k-translated upstream `drivers/console.c` bare-metal IN PLACE OF the hand-mirrored `bm_console.c`.  §8p [2026-06-17, this session] **WIRED newlibc's OWN `drivers/console.c` (the 7201 channel-A polled serial console) into a bare-metal image and RAN it on MAME victor9k — the FIFTH proof a §8k-translated upstream phase3 driver RUNS bare-metal (after timer §8l, display §8m, keyboard §8n, sasi §8o); the new `console_upstream_bm` battery test PASSES (bare-metal battery 45 → 46), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/console_upstream_bm.c`, modeled on §8m's `display_upstream_bm.c` — pure polled MMIO, NO interrupts) drives the UPSTREAM `console_init`/`console_putc`/`console_puts`/`console_tx_ready`/`console_rx_ready`/`console_getc_nonblock` API.  **The collision worry turned out clean:** `bm_console.c` is in the BASE `SUPPORT_TUS` set (linked into EVERY bare-metal image), but it defines ONLY `bm_*`-prefixed symbols (`bm_putc`/`bm_puts`/`bm_putu`/`bm_puthex`/`bm_console_init`/`bm_board_init`/`bm_console_*`); upstream `console.c` defines `console_*`/`tty_dev_*` — NO symbol overlap, so they coexist.  The unprefixed `console_*` aliases live ONLY in `bm_shim.c`, which is test-host/bm_stdio only, so the §7i `serial_loopback_test` (test-host, the one program that uses those aliases) is excluded by the guard and links its `console_*` from `bm_shim.c` over `bm_console.c` as before — verified `serial_loopback_test` still `[ok]`.  **ONE build-glue change to `tools/build-newlibc-baremetal.sh` (additive, mirroring the §8l/§8m/§8n timer/display/keyboard `SUPPORT_TUS` rules):** a non-test-host, non-`bm_stdio` program including upstream `"console.h"` (the leading-quote `'"console\.h"'`, which does NOT match `"bm_console.h"`) links `$NL/drivers/console.c`, guarded `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'`.  `-D__MINIC__` (added by §8l) makes `console.c`'s `intel_dev_write_byte` take the Intel `HW_WRITE_BYTE` fork and the `serial_write_control`/`serial_read_status` sites' `SAVE_ES`/`RESTORE_ES` collapse to no-ops via the §6y shadow `interrupts.h` (verified in the generated asm: 24 ES-load far-MMIO accesses, 0 gas leftovers).  **TWO link-time gaps supplied as STUBS in the test (the §8n "supply it in the test, not the driver" pattern):** `console.c` is small enough to be ONE TU code segment, so `--gc-sections` is per-TU and keeps its unused cooked-console paths live — `console_dev_read` reads the keyboard (`keyboard_getc`) and `console_echo_input` echoes to the display (`display_putc`); the test provides `int keyboard_getc(void){return -1;}` + `void display_putc(char){}` for the dead code (the cooked `/dev/console` path itself is covered by the §6n/§6o/§6t cooked-console tests; this test exercises the RAW serial console API only).  **TRAP recorded (cost ~10 min):** the §8m/§8n build rules grep the RAW `$SRC` INCLUDING COMMENTS, so my test's first comment — which literally contained the quoted strings `"display.h"`/`"keyboard.h"` while explaining why I do NOT include them — tripped those rules into linking `display.c`/`keyboard.c`, giving a duplicate `_display_putc` (my stub + display.obj); fixed by rewording the comment to not contain the quoted header names.  **TWO TEST bugs found and fixed (the driver was correct both times):** (1) re-running `console_init()` mid-stream re-resets the already-live 7201 channel A (crt0 already ran `bm_console_init`), emitting one transient garbage byte on the wire — FIX: call `console_init()` FIRST, before `__V9BEGIN__`, so the harness (which keeps only the `__V9BEGIN__`..`__V9END__` region) trims the transient, exactly as crt0's own `bm_console_init` reset precedes the boot banner; (2) `console_tx_ready()` is racily 0 immediately after `console_putc` (the last byte is still in the TX buffer) — FIX: a bounded spin until it BECOMES ready, then assert.  Because the captured harness serial IS channel A — the same channel `console_putc`/`console_puts` drive — the driver under test produces the captured output DIRECTLY: the `console_puts`/`console_putc` lines in the golden ARE the proof their TX path ran (a broken TX path drops the line → a loud golden diff), while the framing/result lines use `bm_puts` (the proven-good harness path) so a TX break still prints readable diagnostics.  **NOTHING from the `console_*` aliases in `bm_shim.c` is linked** (this is not a bm_stdio program; the map shows `console_init`/`console_putc`/`console_puts` resolving to `mod=console.obj`, `bm_console_*` to `mod=bm_console.obj`, and `display_putc`/`keyboard_getc` to the test's own stubs).  The golden (`minic/dos/tests/console_upstream_bm.golden.txt`) is deterministic clean ASCII (verified 3× identical: rc=0, 8 lines, PASS); bare-metal ONLY (the DOS host has no Victor 7201 serial channel A); small model (7077 B code); 20 s budget; PASS on MAME — all 6 phases (console_init ran + channel A alive in captured region / console_puts output / console_putc + CR/LF / console_tx_ready signals ready after drain / console_rx_ready with no input is 0 / console_getc_nonblock with no input is -1).  **VERIFICATION:** `console_upstream_bm` `[ok]` (battery 45 → 46), `serial_bm`/`display_upstream_bm`/`keyboard_upstream_bm`/`serial_loopback_test` all `[ok]` (neighbors + the excluded test-host alias user undisturbed), `make check` green; only build script + a new test + golden + one battery entry changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — five upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o, console §8p) now proven RUNNING bare-metal; all follow-ups consumer-driven (pick with the user):** (1) finish the sweep — the LAST §8k-translated driver is `pic` (8259 init/mask); `bm_pic.c` is linked by EVERY interrupt-driven test (and pulled in by `bm_interrupts.c`/`bm_keyboard.c`/`bm_serial.c`/`bm_tty.c`/`bm_stdio.h`), so the upstream `pic.c` rule must NOT fire for those — a non-testhost hand-mirrored `pic_bm` re-pointed at upstream `pic.c` is the path, with the §8o `sasi.h` if/elif as the mutual-exclusion template (the `bm_pic.h`-grep branch is the `elif`); note `pic.c`'s symbols (`pic_init`/`pic_enable_irq`/`pic_disable_irq`/`pic_get_mask`/`pic_set_mask`) vs `bm_pic.c`'s `bm_pic_*` do not name-collide, but `bm_pic.c` is so widely linked that the guard must be tight; (2) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8p session notes (2026-06-17)

### The pick
- §8o handoff: bare-metal-RUN proven for four drivers (timer §8l, display §8m,
  keyboard §8n, sasi §8o); three consumer-driven follow-ups.  User
  (AskUserQuestion) chose option (1) "continue the driver sweep", specifically
  the serial/console option — wire upstream drivers/console.c bare-metal in
  place of the hand-mirrored bm_console.c.

### What was built
- New test `minic/dos/newlibc/console_upstream_bm.c` (modeled on §8m
  display_upstream_bm.c — pure polled MMIO, NO ISR): drives the UPSTREAM
  console_init / console_putc / console_puts / console_tx_ready /
  console_rx_ready / console_getc_nonblock API.  Links newlibc's OWN
  drivers/console.c.
- console.c is the §8k gas->nasm in-place port: intel_dev_write_byte takes the
  Intel HW_WRITE_BYTE fork under -D__MINIC__, and its SAVE_ES/RESTORE_ES sites
  no-op via the §6y shadow interrupts.h (24 ES-load far-MMIO accesses in the
  generated asm, 0 gas leftovers).
- The captured harness serial IS channel A — the same channel console_putc /
  console_puts drive — so the driver under test produces the captured output
  directly (a console_* line in the golden proves its TX ran).  Framing/result
  lines use bm_puts (the proven-good harness path).

### Collision analysis (the §8o handoff's worry) — turned out clean
- bm_console.c is in the BASE SUPPORT_TUS set (every image) but defines ONLY
  bm_*-prefixed names; upstream console.c defines console_*/tty_dev_* — NO
  overlap.  The unprefixed console_* aliases live only in bm_shim.c
  (test-host/bm_stdio only), so the §7i serial_loopback_test that uses them is
  excluded by the guard.  Verified serial_loopback_test still [ok].

### build-newlibc-baremetal.sh change (build-glue ONLY, additive)
- ONE new rule mirroring §8l/§8m/§8n: a non-test-host, non-bm_stdio program
  including upstream "console.h" (leading-quote `'"console\.h"'` does NOT match
  `"bm_console.h"`) links $NL/drivers/console.c, guarded
  `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'`.
- TWO link-time gaps supplied as STUBS in the test (the §8n pattern):
  console.c is one TU code segment, so --gc-sections is per-TU and keeps its
  unused cooked-console paths live — console_dev_read->keyboard_getc and
  console_echo_input->display_putc; the test provides
  `int keyboard_getc(void){return -1;}` + `void display_putc(char){}`.  The
  cooked /dev/console path itself is covered by §6n/§6o/§6t.

### TRAP recorded (cost ~10 min)
- The §8m/§8n build rules grep the RAW $SRC INCLUDING COMMENTS.  My test's
  first comment literally contained the quoted strings "display.h"/"keyboard.h"
  (explaining why I do NOT include them), which tripped those rules into
  linking display.c/keyboard.c → duplicate _display_putc (my stub + display.obj).
  Fixed by rewording the comment to not contain the quoted header names.

### TWO TEST bugs found + fixed (the driver was correct both times)
- (1) Re-running console_init() mid-stream re-resets the already-live 7201
  channel A (crt0 already ran bm_console_init), emitting one transient garbage
  byte on the wire.  FIX: call console_init() FIRST, before __V9BEGIN__, so the
  harness (keeps only __V9BEGIN__..__V9END__) trims the transient — exactly as
  crt0's own bm_console_init reset precedes the boot banner.
- (2) console_tx_ready() is racily 0 immediately after console_putc (the last
  byte is still in the TX buffer).  FIX: a bounded spin until it BECOMES ready,
  then assert.

### Verification
- console_upstream_bm: PASS on MAME, 6 phases (console_init ran; channel A alive
  in captured region / console_puts output / console_putc + CR/LF / tx_ready
  signals ready after drain / rx_ready with no input is 0 / getc_nonblock with
  no input is -1).  small (7077 B); 20 s budget.  Golden deterministic clean
  ASCII (verified 3× identical: rc=0, 8 lines, PASS).
- Battery harness: console_upstream_bm [ok] (battery 45 → 46); serial_bm,
  display_upstream_bm, keyboard_upstream_bm, serial_loopback_test all [ok]
  (neighbors + the excluded test-host alias user undisturbed).
- make check green; compiler byte-identical (only build script + new test +
  golden + battery entry changed) → no emit audit, no MP byte-compare,
  test-dos unchanged.  Bare-metal ONLY (no Victor 7201 serial on DOS).
  newlibc tree untouched (§8k port already committed on minic-asm-port @
  5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- Five upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o,
  console §8p) now RUNNING bare-metal.  The LAST §8k driver to sweep is pic.
- (1) finish the sweep — pic (8259 init/mask).  bm_pic.c is linked by EVERY
  interrupt-driven test (and pulled by bm_interrupts.c/bm_keyboard.c/
  bm_serial.c/bm_tty.c/bm_stdio.h), so the upstream pic.c rule must NOT fire
  for those: a non-testhost hand-mirrored pic_bm re-pointed at upstream pic.c
  is the path, the §8o sasi.h if/elif the mutual-exclusion template (the
  bm_pic.h-grep branch becomes the elif).  pic.c (pic_init/pic_enable_irq/
  pic_disable_irq/pic_get_mask/pic_set_mask) vs bm_pic.c (bm_pic_*) do not
  name-collide, but bm_pic.c is so widely linked the guard must be tight.
- (2) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: merge minic-asm-port into newlibc main; retire the redundant
  §6y minic/dos/newlibc/interrupts.h shadow.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8o and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
