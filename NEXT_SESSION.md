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

# Next session (the §8j handoff left NO carried compiler track open and offered "gas→nasm driver porting" as track #1 of the consumer-driven Phase-6 frontier; the user (AskUserQuestion) chose it, and chose to **translate the gas/AT&T inline asm in-place in the upstream `~/projects/newlibc/phase3_newlib` tree** (behind a minic-detection `#ifdef`, alongside the existing ia16-gcc/Watcom branches) rather than maintain ported copies in the qbe tree.  §8k [2026-06-17, this session] **PORTED the entire nasm-failure bucket — the triage sweep's gas/AT&T inline-asm TUs now compile END-TO-END under minic: triage PASS 53 → 65 at BOTH small AND medium, the nasm bucket 12 → 0; NO qbe compiler source touched (minic/qbe/emit byte-identical → `make check` green, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The 12 nasm-bucket TUs were the 6 drivers (`console`/`display`/`keyboard`/`pic`/`sasi`/`timer`), `libgloss/board_init`, and 5 tests (`crtc_test`/`es_ss_diagnostic`/`font_ram_test`/`pic_test`/`serial_debug`); every one failed nasm at a gas/AT&T construct minic passes through VERBATIM (it compiles C faithfully but does not translate asm syntax — the §8j conclusion).  **CONVENTION introduced:** the minic-driving preprocess now defines `-D__MINIC__` (added to `build/newlibc-triage/sweep.sh`'s `clang -E` line; the existing `-D__ia16__` stays — ia16-gcc ALSO defines `__ia16__`, so it can't distinguish minic, and the far-pointer `MK_FP` GCC branch is still wanted), so upstream forks its inline asm `#if defined(__MINIC__)` (minic/nasm/Intel) `#else` (gas/AT&T) — ia16-gcc and Watcom take the unchanged `#else`, leaving their builds BYTE-IDENTICAL.  **THE IDIOMS + their minic translations (all additive, all `#if defined(__MINIC__)`-gated):** (1) **shared `drivers/interrupts.h`** — `SAVE_ES`/`RESTORE_ES` (`movw %%es,%0`) → no-ops (the §6d ISR ABI owns ES; a volatile far access carries its own segment — the §6e/§6i/§6y reasoning), which makes `get_interrupt_vector` collapse to a plain far-pointer IVT read with an unused `static`, exactly the §6y `minic/dos/newlibc/interrupts.h` shadow; THIS HEADER ALONE unblocked board_init + crtc_test + font_ram_test + pic_test and the `get_interrupt_vector` copy every driver carries (PASS 53→57); (2) **`intel_dev_write_byte`** (timer.c, console.c, serial_debug.c — the order-sensitive 8253 byte store the ia16 compiler must not merge) → `HW_WRITE_BYTE(0xE000, offset, value)` (the v9k_hw.h volatile far MMIO macro; minic does not coalesce volatile far stores, so ordering holds — the bm_*.c approach); (3) **`write_crtc_reg`/`read_crtc_reg`** (display.c, ES byte access to 0xE800:0/1) → `HW_WRITE_BYTE`/`HW_READ_BYTE(0xE800, …)` with the `delay()` between, ordering preserved; (4) **`pic_delay`** (pic.c, gas local labels `jmp 1f;1:jmp 2f;2:`) → `jmp short $+2` twice (nasm has no `1f`/`2f`; same two fall-through jumps); (5) **flags-save/cli** (pic.c `interrupt_flags_save`, keyboard.c `keyboard_flags_save`, sasi.c `sasi_save_flags_cli`/`sasi_restore_flags`) → Intel `pushf`/`pop word %0`/`cli` (and `push word %0`/`popf`), the §8j extended-asm operand `%0` resolving to the local's frame slot — verified in the generated asm as `pushf / pop word [bp-10] / cli`; (6) the empty `__asm__ volatile("" ::: "memory")` barrier in sasi.c compiles to nothing, fine.  **THE ONE NON-OBVIOUS BUG (the es_ss_diagnostic lesson): the §8j extended-asm operand resolution matches a local by NAME and needs a uniquely-named slot-resident temp.**  `CAPTURE_REGISTERS` is a macro that reuses local names (`_cap_es`, …) and is expanded at SEVERAL call sites in one function → multiple same-named allocs → emit's `%name`→`[bp±N]` scan can't pick a slot-resident temp → the `%_cap_es` token reached nasm unresolved.  FIX: read each register through its OWN `static` helper function (`cap_seg_es()`/…), so the operand local is unique within each function scope — one operand, one frame slot, the documented §8j single-operand pattern.  (Faithful-enough for the diagnostic, which is moot under minic anyway: CS/SP now reflect the helper frame but consistently before/after, so no false "corruption".)  **VERIFICATION:** triage at small AND medium both PASS 65, nasm bucket empty; the ported codegen inspected and sound (flags-save resolves to `[bp-10]`; the CRTC/dev-write emit `mov ax,0xE800/0xE000; mov es,ax; mov byte ptr es:[bx],cl` with the address→delay→data ordering intact; `pic_delay` emits `jmp short $+2` ×2).  NO qbe-tree source changed (only the gitignored `build/newlibc-triage/sweep.sh` got `-D__MINIC__`; the newlibc edits are a separate repo) → `make check` green, the compiler is byte-identical, so test-dos/MP/stevie can't regress (no emit audit, no MP byte-compare needed).  newlibc-repo diff: 9 files, +127 lines, ALL additive + `__MINIC__`-gated (left UNCOMMITTED on `~/projects/newlibc` branch `main` pending the user — separate repo).  **WHAT THIS DOES AND DOESN'T BUY:** the user's ask — "real phase3 drivers compiling end-to-end under minic" — is MET (12/12 nasm-bucket TUs, small + medium).  The honest limitation is COMPILE-correctness, not yet HARDWARE-correctness: the translations mirror the already-MAME-verified bm_*.c / §6y idioms and the codegen is sound, but nothing here was RUN bare-metal — the upstream drivers are not yet wired into a bare-metal image (the build path currently links the hand-mirrored bm_*.c).  **⇒ Next session — pick consumer-driven (with the user):** (1) **functional bare-metal gate** — wire ONE upstream driver (e.g. `timer.c`) into a bare-metal image IN PLACE OF its `bm_*.c` and run an existing battery test on MAME victor9k, proving the in-place-translated driver RUNS (the real Phase-6 end-state: newlibc's own drivers replace the bm_*.c mirrors); (2) the **minic-parse bucket** (8 TUs, a DIFFERENT track than this session's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to commit the newlibc ports and/or retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream is minic-aware.  NO QBE/minic codegen bug is open; NO carried compiler track remains.)

## §8k session notes (2026-06-17)

### The pick
- §8j handoff: NO carried compiler track; offered consumer-driven frontiers.
  User (AskUserQuestion) chose "gas→nasm driver porting" (track #1), and chose
  to TRANSLATE THE UPSTREAM IN-PLACE in ~/projects/newlibc/phase3_newlib behind
  a minic `#ifdef` (not ported copies in the qbe tree).

### Scope = the triage nasm bucket (12 TUs)
- 6 drivers (console/display/keyboard/pic/sasi/timer) + libgloss/board_init +
  5 tests (crtc_test/es_ss_diagnostic/font_ram_test/pic_test/serial_debug).
- The minic-PARSE bucket (8: interrupts.c ISR-fn-ptr param, vshell, 6 dos_tests
  Watcom _asm{}) is a SEPARATE track, out of scope.

### Convention: -D__MINIC__
- Added to build/newlibc-triage/sweep.sh's clang -E (kept -D__ia16__: ia16-gcc
  also defines __ia16__, and the MK_FP GCC branch is wanted).  Upstream forks
  `#if defined(__MINIC__)` (Intel/nasm) #else (gas) — gcc/Watcom take #else,
  byte-identical.

### Idioms → minic translations (all additive, __MINIC__-gated)
- interrupts.h SAVE_ES/RESTORE_ES → no-op (ISR ABI owns ES); get_interrupt_vector
  collapses to a far-ptr IVT read (the §6y shadow).  ALONE unblocked 4 tests +
  every driver's get_interrupt_vector copy (PASS 53→57).
- intel_dev_write_byte (timer/console/serial_debug) → HW_WRITE_BYTE(0xE000,…).
- write_crtc_reg/read_crtc_reg (display) → HW_WRITE_BYTE/HW_READ_BYTE(0xE800,…),
  delay() between (ordering preserved).
- pic_delay (gas 1f/2f labels) → "jmp short $+2" ×2.
- flags-save/cli (pic/keyboard/sasi) → Intel "pushf / pop word %0 / cli" +
  "push word %0 / popf"; §8j operand %0 → [bp-10] (verified in asm).
- empty "" memory barrier (sasi) compiles to nothing.

### The es_ss_diagnostic lesson (the one non-obvious bug)
- §8j operand resolution matches a local by NAME → needs a uniquely-named
  slot-resident temp.  CAPTURE_REGISTERS (a macro reusing _cap_es across several
  call sites in one function) → multiple same-named allocs → emit couldn't pick
  a slot → %_cap_es reached nasm unresolved.
- FIX: read each register through its own static helper (cap_seg_es()/…), one
  unique operand-local per function scope.  Moot diagnostic under minic anyway.

### Verification
- Triage small AND medium: PASS 65, nasm bucket 0.
- Codegen inspected sound: flags → [bp-10]; CRTC/dev-write → mov es,0xE800/E000
  + es:[bx] with address→delay→data order; pic_delay → jmp short $+2 ×2.
- NO qbe-tree source changed (sweep.sh is gitignored build/; newlibc is a
  separate repo) → make check GREEN, compiler byte-identical → test-dos/MP/
  stevie can't regress; no emit audit, no MP byte-compare.
- newlibc diff: 9 files, +127, additive + __MINIC__-gated, UNCOMMITTED on
  ~/projects/newlibc branch main (separate repo, pending user).

### ⇒ Next session (consumer-driven, with the user)
- COMPILE-correctness is met (12/12, small+medium); HARDWARE-correctness is NOT
  yet proven (nothing run bare-metal; upstream drivers not wired into an image —
  the build links bm_*.c).
- (1) functional bare-metal gate: swap ONE upstream driver (timer.c) for its
  bm_*.c in a bare-metal image, run a battery test on MAME (the Phase-6 end-state
  — upstream drivers replace the bm_*.c mirrors);
- (2) minic-parse bucket: interrupts.c ISR-fn-ptr param declarator (real bounded
  frontend parse feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: commit the newlibc ports; retire the redundant §6y interrupts.h
  shadow now that upstream is minic-aware.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8j and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
