# Next session (the §8m handoff proved the bare-metal-RUN end-state for two upstream drivers (timer §8l, display §8m) and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **continue the driver sweep** — wire the next §8k-translated driver bare-metal IN PLACE OF its `bm_*.c` mirror, starting with `keyboard` (interrupt-driven on IR6, closest to the proven timer ISR path).  §8n [2026-06-17, this session] **WIRED newlibc's OWN `drivers/keyboard.c` into a bare-metal image IN PLACE OF the hand-mirrored `bm_keyboard.c` and RAN it on MAME victor9k — the THIRD proof a §8k-translated upstream phase3 driver RUNS bare-metal (after §8l's timer.c and §8m's display.c), and the FIRST interrupt-driven one to route a non-timer IRQ to an upstream handler; the new `keyboard_upstream_bm` battery test is a FIRST-RUN PASS (bare-metal battery 43 → 44), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/keyboard_upstream_bm.c`, modeled on §6e's `keyboard_bm.c` re-pointed at the upstream API + §8l's `timer_upstream_bm.c` ISR pattern) drives the UPSTREAM `keyboard_init`/`keyboard_getc_nonblock`/`keyboard_irq_handler` API.  Like §8l's timer (and unlike §8m's polled display) it is INTERRUPT-DRIVEN: the keyboard's dedicated KBINT line is IR6, and the interrupt plumbing is the GENERIC toolchain feature (not a driver) — a local `keyboard_isr` is the compiler-emitted ES-safe iret ABI (`__attribute__((interrupt))` → QBE `interrupt` linkage → the §6d i8086 prologue/epilogue) routing each KBINT to the UPSTREAM `keyboard_irq_handler()` + the specific EOI (`0x60|IRQ_KEYBOARD`), installed via the same model-agnostic IVT write (`qbe_get_cs()`) `timer_upstream_bm` uses.  Upstream `timer.c` (§8l) is ALSO linked, supplying the deterministic timeout clock as a SECOND compiler-emitted ISR on IR2 — so this is **TWO §8k-translated upstream drivers running together under live interrupts**, the keyboard ring drained by `keyboard_getc_nonblock` while the timer ISR fires.  **NOTHING from `bm_keyboard.c` / `bm_timer.c` is linked** — verified via the link map: `keyboard_init`/`keyboard_irq_handler`/`keyboard_getc_nonblock` resolve to `mod=keyboard.obj` and `timer_init`/`timer_tick_handler` to `mod=timer.obj`, with the `bm_*` mirrors absent.  What is exercised live is the §8k port of `keyboard.c`'s `keyboard_flags_save` → the Intel `#if defined(__MINIC__)` → `pushf` / `pop word %0` / `cli` fork, with the §8j extended-asm operand `%0` resolving to a frame slot — verified in the emitted asm as `pushf` / `pop word [bp-10]` / `cli`, ZERO gas leftovers — and its `SAVE_ES`/`RESTORE_ES` collapsing to no-ops via the §6y shadow `interrupts.h` (the §6d ISR ABI owns ES).  **ONE link-time gap supplied in the test, not the driver:** upstream `keyboard.c`'s flags-restore calls `interrupts_enable()`, which is defined only in upstream `drivers/interrupts.c` — which we deliberately do NOT link (it carries its own `timer_isr`/`keyboard_isr` that would collide with the local ISRs), so the test supplies the one-liner the §6y shadow `interrupts.h` declares: `void interrupts_enable(void) { __asm__ volatile ("sti"); }`.  Upstream `keyboard.c` also has no ISR-entry accessor (the hand-mirrored `bm_keyboard.c` added `bm_keyboard_isr_count` for §6e), so the local ISR wrapper counts entries itself — the `timer_upstream_bm` pattern.  **ONE build-glue change to `tools/build-newlibc-baremetal.sh` ONLY (additive, mirroring the §8l/§8m timer/display SUPPORT_TUS rules):** a non-test-host, non-`bm_stdio` program that includes upstream `"keyboard.h"` (NOT `bm_keyboard.h`) links `$NL/drivers/keyboard.c`, guarded `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'` because test-host / `bm_stdio` tests already pull `bm_keyboard.c` + `bm_shim.c`'s unprefixed `keyboard_*` aliases, which would DUPLICATE-SYMBOL-collide with upstream `keyboard.c`'s `keyboard_*` (the §6w `keyboard_raw_test`, §6x `keyboard_nonblock_test`, and the cooked-console tests are all test-host).  The grep pattern `'"keyboard\.h"'` (leading quote) does NOT match `"bm_keyboard.h"`, so the hand-mirrored `keyboard_bm` (which includes `bm_keyboard.h`) is unaffected — verified bug-free: `keyboard_raw_test` (a test-host program that includes upstream `"keyboard.h"`) links only `bm_keyboard.obj`, never `keyboard.obj`.  The `-D__MINIC__` on the `clang -E` line (added by §8l) makes `keyboard.c` take the Intel flags-save fork; the §8l timer + §8m display SUPPORT_TUS rules are untouched.  The golden (`minic/dos/tests/keyboard_upstream_bm.golden.txt`) is deterministic (fixed phase text plus the received chars `v9k`), so it is fully toolchain-stable; bare-metal ONLY (the DOS host has no live VIA CS2 / IR6 ring / 8253); small model (11,291 B code); 25 s budget; FIRST-RUN PASS on MAME with `V9K_KEYPOST=v9k` — all 9 phases (PIC re-init + install both ISRs / upstream timer_init / upstream keyboard_init / sti / no key pending at start / got "v9k" / chars match / keyboard ISR entered / timer still ticking alongside IR6).  **VERIFICATION:** `keyboard_upstream_bm` `[ok]` AND `keyboard_bm` `[ok]` through the battery harness (which builds, runs on MAME, and golden-diffs); only build script + a new test + golden + one battery entry changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — three upstream drivers (timer §8l, display §8m, keyboard §8n) now proven RUNNING bare-metal; all follow-ups consumer-driven (pick with the user):** (1) continue the sweep — wire the remaining §8k-translated drivers bare-metal IN PLACE OF their `bm_*.c` mirrors: `serial`/`console` (7201 polled + RX-ISR, the §7i loopback path covers the cooked side already), `pic` (the SUPPORT_TUS rule needs care: `pic_test` is a test-host program that already pulls `bm_pic.c`, so a non-testhost hand-mirrored test like `pic_bm` re-pointed at upstream `pic.c` is the path — the §8m/§8n guard already handles this, but `bm_pic.c` is linked by EVERY interrupt-driven test so the upstream `pic.c` rule must NOT also fire for those; design carefully), `sasi` (the §6i disk path, medium model); each is the exact §8l/§8m/§8n pattern (self-contained test + a guarded upstream-driver SUPPORT_TUS rule); (2) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8n session notes (2026-06-17)

### The pick
- §8m handoff: bare-metal-RUN proven for two drivers (timer §8l, display §8m);
  three consumer-driven follow-ups.  User (AskUserQuestion) chose option (1)
  "continue the driver sweep".  Chose keyboard (IR6 interrupt-driven) as the
  next driver — closest to the proven §8l timer ISR path, and the first to
  route a NON-timer IRQ to an upstream handler.

### What was built
- New test `minic/dos/newlibc/keyboard_upstream_bm.c` (modeled on §6e
  keyboard_bm.c re-pointed at the upstream API + §8l timer_upstream_bm.c's
  ISR/IVT pattern): drives the UPSTREAM keyboard_* API (keyboard_init /
  getc_nonblock / irq_handler), links newlibc's OWN drivers/keyboard.c IN
  PLACE OF bm_keyboard.c.
- INTERRUPT-DRIVEN: a local __attribute__((interrupt)) keyboard_isr routes
  each KBINT (IR6) to upstream keyboard_irq_handler() + EOI; installed via the
  model-agnostic IVT write (qbe_get_cs()).  Upstream timer.c (§8l) ALSO linked
  for the deterministic timeout clock (a 2nd compiler-emitted ISR on IR2) —
  TWO §8k drivers running together under live interrupts.  NOTHING from
  bm_keyboard.c / bm_timer.c linked (map: keyboard_* from keyboard.obj,
  timer_* from timer.obj).
- Exercises live the §8k Intel flags-save fork: keyboard_flags_save emits
  `pushf / pop word [bp-10] / cli` (§8j operand %0 → frame slot), 0 gas.
- Two link-time gaps supplied in the TEST, not the driver: (1) interrupts_enable()
  (defined only in upstream interrupts.c, which we don't link — it has its own
  timer_isr/keyboard_isr that would collide) → one-liner `sti` in the test;
  (2) no upstream ISR-count accessor → the local ISR wrapper counts entries.

### build-newlibc-baremetal.sh change (build-glue ONLY, additive)
- ONE new SUPPORT_TUS rule mirroring §8l/§8m: a non-test-host, non-bm_stdio
  program including upstream "keyboard.h" (NOT bm_keyboard.h) links
  $NL/drivers/keyboard.c.  Guard `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'`
  is REQUIRED: test-host / bm_stdio tests (§6w keyboard_raw_test, §6x
  keyboard_nonblock_test, cooked console) pull bm_keyboard.c + bm_shim.c's
  keyboard_* aliases → would collide with upstream keyboard.c.
- The `'"keyboard\.h"'` pattern (leading quote) does NOT match
  `"bm_keyboard.h"`, so keyboard_bm (includes bm_keyboard.h) is unaffected.
- Verified: keyboard_raw_test (test-host, includes upstream "keyboard.h")
  links only bm_keyboard.obj, NOT keyboard.obj.
- -D__MINIC__ (added by §8l) makes keyboard.c take the Intel pushf/pop/cli
  fork; the §8l timer + §8m display rules are untouched.

### Verification
- keyboard_upstream_bm: FIRST-RUN PASS on MAME victor9k with V9K_KEYPOST=v9k,
  9 phases (PIC re-init + both ISRs / upstream timer_init / upstream
  keyboard_init / sti / no key pending / got "v9k" / chars match / keyboard
  ISR entered / timer still ticking alongside IR6).
- Battery harness (build + MAME run + golden-diff): keyboard_upstream_bm [ok]
  AND keyboard_bm [ok] (battery 43 → 44).
- make check green; compiler byte-identical (only build script + new test +
  golden + battery entry changed) → no emit audit, no MP byte-compare,
  test-dos unchanged.  Golden deterministic (fixed text + received chars),
  toolchain-stable.  Bare-metal ONLY; small (11,291 B code); 25 s budget.
  newlibc tree untouched (§8k port already committed on minic-asm-port @
  5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- Three upstream drivers (timer §8l, display §8m, keyboard §8n) now RUNNING
  bare-metal.
- (1) continue the sweep — serial/console (7201 polled + RX-ISR), pic (CAREFUL:
  bm_pic.c is linked by EVERY interrupt-driven test, so an upstream pic.c rule
  must not collide — use a non-testhost hand-mirrored pic_bm re-pointed at
  upstream pic.c, and ensure the rule doesn't also fire for tests already
  pulling bm_pic.c), sasi (the §6i disk path, medium model); each is the exact
  §8l/§8m/§8n pattern (self-contained test + a guarded upstream-driver
  SUPPORT_TUS rule);
- (2) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: merge minic-asm-port into newlibc main; retire the redundant
  §6y minic/dos/newlibc/interrupts.h shadow.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

# Next session (the §8l handoff proved the bare-metal-RUN end-state for ONE upstream driver (timer.c) and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **more upstream drivers bare-metal** — wire the other §8k-translated drivers into bare-metal images IN PLACE OF their `bm_*.c` mirrors and run them on MAME.  §8m [2026-06-17, this session] **WIRED newlibc's OWN `drivers/display.c` (+ `drivers/font_data.c`) into a bare-metal image IN PLACE OF the hand-mirrored `bm_display.c`/`bm_font_data.c` and RAN it on MAME victor9k — the SECOND proof a §8k-translated upstream phase3 driver RUNS bare-metal (after §8l's timer.c); the new `display_upstream_bm` battery test is a FIRST-RUN PASS (bare-metal battery 42 → 43), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/display_upstream_bm.c`, modeled on §6e's `display_bm.c`) drives the UPSTREAM `display_init`/`display_clear`/`display_puts`/`display_putc`/`display_putc_at`/`display_set_cursor`/`display_get_cursor`/`display_scroll` API and verifies every effect by reading it back from the machine WITHOUT a host-side screen dump — VRAM words through a far pointer to F000:0000 (local `screen_word`/`read_cell`, the `(attr<<8)|(c+0x60)` Victor glyph-pointer encoding), the font from its 0000:0C00 RAM home (vs the upstream `victor_font[]` table from `font_data.c`), and the cursor via the upstream `display_get_cursor` (which reads CRTC R14/R15, the only registers a real 6845 lets you read back).  Unlike §8l's timer there is **NO interrupt plumbing** — the display driver is pure polled MMIO; what is exercised live is the §8k port of `display.c`'s `write_crtc_reg`/`read_crtc_reg` → the Intel `HW_WRITE_BYTE`/`HW_READ_BYTE` fork (verified in the generated asm: `mov es,0xE800` + `mov byte es:[bx]`, ZERO gas leftovers) and `display_load_fonts`'s 8192-byte copy of `victor_font[]` into font RAM.  **NOTHING from `bm_display.c`/`bm_font_data.c` is linked** — verified via the link map: every `display_*` symbol resolves to `mod=display.obj`, `victor_font` to `mod=font_data.obj`, and the `bm_*` mirrors are absent.  **ONE build-glue change to `tools/build-newlibc-baremetal.sh` ONLY (additive, mirroring §8l's timer SUPPORT_TUS rule):** a non-test-host, non-`bm_stdio` program that includes upstream `"display.h"` (NOT `bm_display.h`) links `$NL/drivers/display.c` + `$NL/drivers/font_data.c`, guarded `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'` because test-host / `bm_stdio` tests already pull `bm_display.c` + `bm_shim.c`'s unprefixed `display_*` aliases, which would DUPLICATE-SYMBOL-collide with upstream `display.c`'s `display_*`.  The grep pattern `'"display\.h"'` (leading quote) does NOT match `"bm_display.h"`, so the hand-mirrored `display_bm` (which includes `bm_display.h`) is unaffected — verified bug-free: `font_test` (a test-host program that includes both upstream display + font headers) links only `bm_display.obj`/`bm_font_data.obj`, never `display.obj`.  The `-D__MINIC__` on the `clang -E` line was already added by §8l (it makes `display.c` take the Intel CRTC fork); the timer SUPPORT_TUS rule from §8l is untouched.  The golden (`minic/dos/tests/display_upstream_bm.golden.txt`) is deterministic booleans only (no timing/screen-dependent values), so it is fully toolchain-stable; bare-metal ONLY (the DOS host has no Victor VRAM/CRTC); small model (8581 B code); 20 s budget; FIRST-RUN PASS on MAME — all 9 phases (display init / cursor homed / VRAM blank-word 0x80 / font RAM matches table at 'A' / puts writes glyph pointers + advances cursor / putc_at honors position+attribute / newline blanks rest of row / tab+backspace cursor movement / scroll from bottom row).  **VERIFICATION:** `display_upstream_bm` `[ok]` AND `display_bm` `[ok]` through the battery harness (which builds, runs on MAME, and golden-diffs); only build script + a new test + golden + one battery entry changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — two upstream drivers (timer §8l, display §8m) now proven RUNNING bare-metal; all follow-ups consumer-driven (pick with the user):** (1) continue the sweep — wire the remaining §8k-translated drivers bare-metal IN PLACE OF their `bm_*.c` mirrors: `keyboard` (interrupt-driven on IR6, like §8l's timer — needs the generic ISR ABI + a self-contained test re-pointed at the upstream `keyboard_*` API), `serial`/`console` (7201 polled + RX-ISR), `pic` (the SUPPORT_TUS rule needs care: `pic_test` is a test-host program that already pulls `bm_pic.c`, and the upstream `pic.c` would collide — a non-testhost hand-mirrored test like `pic_bm` re-pointed at upstream `pic.c` is the path), `sasi` (the §6i disk path); each is the exact §8l/§8m pattern (self-contained test + an upstream-driver SUPPORT_TUS rule, guarded against the test-host/bm_stdio `bm_*` aliases); (2) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8m session notes (2026-06-17)

### The pick
- §8l handoff: bare-metal-RUN proven for ONE driver (timer.c); three
  consumer-driven follow-ups.  User (AskUserQuestion) chose option (1)
  "more upstream drivers bare-metal" — start with display or keyboard.
- Chose display.c (pure polled MMIO, no ISR — the simplest second driver,
  isolates "does the §8k CRTC port RUN" from interrupt concerns).

### What was built
- New test `minic/dos/newlibc/display_upstream_bm.c` (modeled on §6e
  display_bm.c): drives the UPSTREAM display_* API (init/clear/puts/putc/
  putc_at/set_cursor/get_cursor/scroll), links newlibc's OWN drivers/
  display.c + drivers/font_data.c IN PLACE OF bm_display.c/bm_font_data.c.
- Verifies every effect by reading the machine back over serial: VRAM words
  via a far ptr to F000:0000 (local screen_word/read_cell, (attr<<8)|(c+0x60)),
  font RAM at 0000:0C00 vs upstream victor_font[], cursor via upstream
  display_get_cursor (CRTC R14/R15).  NO interrupts (polled MMIO).
- NOTHING from bm_display.c/bm_font_data.c linked (map: display_* all from
  display.obj, victor_font from font_data.obj).

### build-newlibc-baremetal.sh change (build-glue ONLY, additive)
- ONE new SUPPORT_TUS rule mirroring §8l's timer rule: a non-test-host,
  non-bm_stdio program including upstream "display.h" (NOT bm_display.h)
  links $NL/drivers/display.c + $NL/drivers/font_data.c.  Guard
  `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'` is REQUIRED: test-host /
  bm_stdio tests pull bm_display.c + bm_shim.c's display_* aliases, which
  would duplicate-symbol-collide with upstream display.c.
- The `'"display\.h"'` pattern (leading quote) does NOT match
  `"bm_display.h"`, so display_bm (includes bm_display.h) is unaffected.
- Verified: font_test (test-host, includes upstream display + font headers)
  links only bm_display.obj/bm_font_data.obj, NOT display.obj.
- The -D__MINIC__ on clang -E was already added by §8l (makes display.c take
  the Intel write/read_crtc_reg HW_*_BYTE fork); §8l's timer rule untouched.

### Verification
- display_upstream_bm: FIRST-RUN PASS on MAME victor9k, 9 phases (init /
  cursor home / VRAM 0x80 blank / font readback at 'A' / puts+advance /
  putc_at attr / newline blank / tab+backspace / bottom-row scroll).
- CRTC codegen inspected: mov es,0xE800 + mov byte es:[bx], 0 gas leftovers.
- Battery harness (build + MAME run + golden-diff): display_upstream_bm [ok]
  AND display_bm [ok] (battery 42 → 43).
- Only build script + new test + golden + battery entry changed → compiler
  byte-identical → make check green, no emit audit, no MP byte-compare,
  test-dos unchanged.  Golden deterministic booleans only, toolchain-stable.
  Bare-metal ONLY (no Victor VRAM on DOS); small; 20 s budget.  newlibc tree
  untouched (§8k port already committed on minic-asm-port @ 5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- Two upstream drivers (timer §8l, display §8m) now proven RUNNING bare-metal.
- (1) continue the sweep — keyboard (IR6 interrupt-driven, needs the generic
  ISR ABI like §8l's timer), serial/console (7201 polled + RX-ISR), pic
  (CAREFUL: pic_test is test-host + pulls bm_pic.c → collision; use a
  non-testhost hand-mirrored test re-pointed at upstream pic.c), sasi (the
  §6i disk path); each is the exact §8l/§8m pattern (self-contained test +
  a guarded upstream-driver SUPPORT_TUS rule);
- (2) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: merge minic-asm-port into newlibc main; retire the redundant
  §6y minic/dos/newlibc/interrupts.h shadow.
- NO QBE/minic codegen bug open; NO carried compiler track remains.


---

Older session headers (§8l and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
