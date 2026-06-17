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

# Next session (the §8k handoff left NO carried compiler track and offered three consumer-driven directions; the user (AskUserQuestion) chose the **functional bare-metal gate** — prove a §8k-translated upstream phase3 driver RUNS on hardware, not just compiles.  §8l [2026-06-17, this session] **WIRED newlibc's OWN `drivers/timer.c` into a bare-metal image IN PLACE OF the hand-mirrored `bm_timer.c` and ran it on MAME victor9k — the FIRST proof an upstream phase3 driver RUNS bare-metal (§8k was compile-only); the new `timer_upstream_bm` battery test is a FIRST-RUN PASS (bare-metal battery 41 → 42), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/timer_upstream_bm.c`, modeled on §6d's `timer_bm.c`) drives the UPSTREAM `timer_init`/`timer_get_ticks`/`timer_delay_ms`/`timer_get_frequency`/`timer_tick_handler` API; the interrupt plumbing is the GENERIC toolchain feature (not a driver) — a local `timer_isr` is the compiler-emitted ES-safe iret ABI (`__attribute__((interrupt))` → QBE `interrupt` linkage → the §6d i8086 prologue/epilogue) routing each IR2 tick to upstream `timer_tick_handler()` + the specific EOI, installed via an inline IVT write (mirroring `bm_install_isr`'s model-agnostic seg:off / `qbe_get_cs()` logic), with `bm_pic_init()` doing the mandatory 8259 re-init before `sti`.  **NOTHING from `bm_timer.c` is linked** — verified via the link map: every `timer_*` symbol resolves to `mod=timer.obj` (upstream), and `bm_timer.obj` is absent.  **Two build-glue changes to `tools/build-newlibc-baremetal.sh` ONLY (additive):** (1) added `-D__MINIC__` to the `clang -E` line — the §8k convention extended to the real bare-metal build path — so `timer.c`'s `intel_dev_write_byte` takes the Intel `#if defined(__MINIC__)` → `HW_WRITE_BYTE` fork (verified in the generated asm: `mov es,0xE000` + `mov byte es:[bx]` byte stores in the 8253-control-then-count order, ZERO gas `movb`/`pushw`/`%%es` leftovers); harmless for every existing test because none of the linked `bm_*.c` or portable stdio TUs test `__MINIC__`, and the §8k-ported upstream driver `.c` files + `drivers/interrupts.h` are NOT linked by the bare-metal build (the §6y `minic/dos/newlibc/interrupts.h` shadow wins the include path) — re-confirmed by `timer_bm`'s golden being byte-unmoved.  (2) a SUPPORT_TUS rule: a non-test-host, non-`bm_stdio` program that includes upstream `"timer.h"` (NOT `bm_timer.h`) links `$NL/drivers/timer.c`, guarded `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'`, because test-host / `bm_stdio` tests already pull `bm_timer.c` + `bm_shim.c`'s unprefixed `timer_*` aliases, which would DUPLICATE-SYMBOL-collide with upstream `timer.c`'s `timer_*` — verified bug-free: `pic_test` (a test-host test that ALSO `#include "timer.h"`) links only `bm_timer.obj`, never `timer.obj`.  The test adds a `timer_get_frequency()==100` phase — an upstream function `bm_timer.c` does not even have.  The golden (`minic/dos/tests/timer_upstream_bm.golden.txt`) is deterministic booleans/ranges only (the `[50..80]` elapsed window absorbs MAME's 125 KHz channel-2 clock vs the documented 100 KHz, so it is toolchain-stable, NOT timing-derived); bare-metal ONLY (the DOS host has no live 8253); small model (6609 B code); 30 s budget; FIRST-RUN PASS on MAME — all 8 phases (freq==100 / live ticks advance / 500 ms delay measured / 1500 ms sustained under live interrupts / cli freezes ticks).  **VERIFICATION:** `timer_upstream_bm` `[ok]` AND `timer_bm` `[ok]` through the battery harness (which builds, runs on MAME, and golden-diffs); only build scripts + a new test + golden + one battery entry changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — the bare-metal-RUN end-state is now proven for ONE driver; all follow-ups consumer-driven (pick with the user):** (1) wire the other §8k-translated drivers (`display`/`keyboard`/`pic`/`sasi`/`console`) bare-metal IN PLACE OF their `bm_*.c` mirrors — each needs its hand-mirrored battery test re-pointed at the upstream API, the exact `timer_upstream_bm` pattern (a self-contained test + the upstream-driver SUPPORT_TUS rule generalized, OR per-driver rules); (2) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8l session notes (2026-06-17)

### The pick
- §8k handoff: NO carried compiler track; three consumer-driven options.
  User (AskUserQuestion) chose option (1) the **functional bare-metal gate** —
  prove a §8k upstream driver RUNS on hardware, not just compiles.

### What was built
- New test `minic/dos/newlibc/timer_upstream_bm.c` (modeled on §6d timer_bm.c):
  drives the UPSTREAM timer_* API (timer_init/get_ticks/delay_ms/
  get_frequency/tick_handler), links newlibc's OWN drivers/timer.c IN PLACE
  OF bm_timer.c.  The ISR plumbing is the generic compiler-emitted ABI: a
  local `__attribute__((interrupt))` timer_isr routes each IR2 tick to
  upstream timer_tick_handler() + the specific EOI; installed via an inline
  IVT write (mirrors bm_install_isr); bm_pic_init() re-inits the 8259.
  NOTHING from bm_timer.c linked (map: every timer_* from mod=timer.obj).
- Adds a timer_get_frequency()==100 phase (an upstream fn bm_timer.c lacks).

### build-newlibc-baremetal.sh changes (build-glue ONLY, additive)
- (1) `clang -E` line gained `-D__MINIC__` (the §8k convention, now on the
  REAL bare-metal build path) → timer.c's intel_dev_write_byte takes the
  Intel HW_WRITE_BYTE fork.  Verified codegen: `mov es,0xE000` + `mov byte
  es:[bx]` in control-then-count order, 0 gas leftovers (movb/pushw/%%es).
  Harmless for existing tests: no linked bm_*.c / portable stdio TU tests
  __MINIC__, and the §8k-ported upstream driver .c + drivers/interrupts.h
  aren't linked (the §6y minic/dos/newlibc/interrupts.h shadow wins the
  include path).  timer_bm golden byte-unmoved confirms it.
- (2) SUPPORT_TUS rule: non-test-host, non-bm_stdio program including
  upstream "timer.h" (not bm_timer.h) links $NL/drivers/timer.c.  Guard
  `[ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h'` is REQUIRED: test-host /
  bm_stdio tests pull bm_timer.c + bm_shim.c's timer_* aliases, which would
  duplicate-symbol-collide with upstream timer.c.  Verified: pic_test (a
  test-host test that ALSO #include "timer.h") links only bm_timer.obj.

### Verification
- timer_upstream_bm: FIRST-RUN PASS on MAME victor9k, 8 phases (freq==100 /
  ticks advance / 500ms delay in [50..80] / 1500ms survived / cli freezes).
- Battery harness (build + MAME run + golden-diff): timer_upstream_bm [ok]
  AND timer_bm [ok] (the -D__MINIC__ addition didn't move timer_bm).
- pic_test build inspected: links bm_timer.obj only, NOT timer.obj (guard ok).
- Only build scripts + new test + golden + battery entry changed → compiler
  byte-identical → make check green, no emit audit, no MP byte-compare,
  test-dos unchanged.  Golden is deterministic ([50..80] absorbs MAME's
  125 KHz ch2 clock), toolchain-stable.  Bare-metal ONLY (no live 8253 on
  DOS); small; 30 s budget.  newlibc tree untouched (§8k port already
  committed on minic-asm-port @ 5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- The bare-metal-RUN end-state is proven for ONE driver (timer.c).
- (1) wire the other §8k drivers (display/keyboard/pic/sasi/console)
  bare-metal in place of their bm_*.c — same timer_upstream_bm pattern
  (self-contained test + the upstream-driver SUPPORT_TUS rule generalized);
- (2) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: merge minic-asm-port into newlibc main; retire the redundant
  §6y minic/dos/newlibc/interrupts.h shadow.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8k and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
