# Next session (the §8n handoff proved the bare-metal-RUN end-state for three upstream drivers (timer §8l, display §8m, keyboard §8n) and listed three consumer-driven follow-ups; the user (AskUserQuestion) chose option (1) **continue the driver sweep**, then chose `sasi` as the next driver (the §6i disk path, medium-model coverage, the §8k SAVE_ES-drop the headline to validate).  §8o [2026-06-17, this session] **WIRED newlibc's OWN `drivers/sasi.c` into a bare-metal image IN PLACE OF the hand-mirrored `bm_sasi.c` and RAN it on MAME victor9k reading and writing real `-scsi:0` sectors — the FOURTH proof a §8k-translated upstream phase3 driver RUNS bare-metal (after timer §8l, display §8m, keyboard §8n) and the FIRST block-storage one; the new `sasi_upstream_bm` battery test is a FIRST-RUN PASS (bare-metal battery 44 → 45), NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, test-dos/MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The new test (`minic/dos/newlibc/sasi_upstream_bm.c`, modeled on §6i's `sasi_bm.c` re-pointed at the upstream API + §8l/§8n's ISR pattern) drives the UPSTREAM `sasi_register` + the block registry (`block_init`/`block_read_sector`/`block_write_sector`/`block_cache_invalidate`) at the BLOCK level — no FAT/VFS/printf (that layer is already covered by §6i/§6p), so it uses raw serial output (`bm_puts`/`bm_puthex`/`bm_putu`) and avoids `bm_stdio` entirely, exactly the §8l/§8m/§8n shape.  **The §8k SAVE_ES drop is the headline being validated, so the SASI transfers run UNDER A LIVE TIMER ISR:** a local `timer_isr` is the compiler-emitted ES-safe iret ABI (`__attribute__((interrupt))` → QBE `interrupt` linkage → the §6d i8086 prologue/epilogue) routing each IR2 tick to the UPSTREAM `timer_tick_handler()` (drivers/timer.c, §8l, ALSO linked) + the specific EOI, installed via the model-agnostic IVT write (`qbe_get_cs()`) after the mandatory `bm_pic_init()`.  `sasi.c`'s `HW_READ_BYTE`/`HW_WRITE_BYTE` load ES (`mov es, word [bp-12]` + `es:[bx]`, verified in the generated asm) for every far MMIO access; a tick landing between the `mov es` and the access is ES-safe ONLY because the §6d prologue saves/restores ES — so a clean read/write/round-trip under the live ISR is the proof the §8k decision to DROP `SAVE_ES` (the ISR ABI owns ES) is correct.  Two §8k-translated upstream drivers (sasi + timer) running together under live interrupts.  What is also exercised live is the §8k port of `sasi.c`'s `sasi_save_flags_cli`/`sasi_restore_flags` → the Intel `#if defined(__MINIC__)` → `pushf` / `pop word %0` / `cli` fork, with the §8j extended-asm operand `%0` resolving to a frame slot — verified in the emitted asm as `pushf` / `pop word [bp-10]` / `cli`, ZERO gas leftovers (no `pushfw`/`popfw`/`popw`) — and its `SAVE_ES`/`RESTORE_ES` collapsing to no-ops via the §6y shadow `interrupts.h` (it wins via `-I$NLC_DIR` first).  **Unlike §8n's keyboard there is NO link-time gap** — `sasi.c` calls no `interrupts_enable`/`get_interrupt_vector`, only the self-contained flags-save asm and the no-op'd `SAVE_ES`.  **NOTHING from `bm_sasi.c` / `bm_timer.c` is linked** — verified via the link map: `sasi_register` resolves to `mod=sasi.obj`, `timer_init`/`timer_tick_handler` to `mod=timer.obj`, and the `bm_*` mirrors are absent.  **ONE build-glue change to `tools/build-newlibc-baremetal.sh` (additive):** the single `grep -q 'sasi\.h'` rule (which matched BOTH `"sasi.h"` and `"bm_sasi.h"` → always `bm_sasi.c`) became an if/elif — a non-test-host, non-`bm_stdio` program including upstream `"sasi.h"` (the leading-quote `'"sasi\.h"'`, which does NOT match `"bm_sasi.h"`) links the UPSTREAM `$NL/drivers/sasi.c` + `block.c`; the `elif grep -q 'sasi\.h'` keeps `bm_sasi.c` + `block.c` for everything else, MUTUALLY EXCLUSIVE so the two SASI drivers never both link.  Verified bug-free across all three SASI tests: `sasi_upstream_bm` (non-testhost, no bm_stdio, `"sasi.h"`) → `sasi.obj`+`timer.obj`; `sasi_bm` (bm_stdio, `"bm_sasi.h"`) → `bm_sasi.obj` via the elif; `sasi_sector_test` (test-host, upstream `"sasi.h"`) → `bm_sasi.obj` via the elif (the `TESTHOST=0` guard fails the first branch).  The golden (`minic/dos/tests/sasi_upstream_bm.golden.txt`) is deterministic (the fixed image bytes — LBA-0 `tandon_703_mame` label — + booleans), toolchain-stable; bare-metal ONLY (the DOS host has no raw `-scsi:0`); small model (16,781 B code); `hd` disk field (V9K_HARD_DISK scratch copy, so WRITE(6) is safe); 90 s budget; FIRST-RUN PASS on MAME — all 10 phases (PIC re-init + timer ISR / upstream timer_init / sti / register upstream sasi + block_init / geometry 59058 / read LBA 0 under live ISR / label is "tandon_703_mame" / uncached re-read checksum matches / WRITE(6) round-trip @ LBA 59057 under live ISR verified / timer still ticking after transfers).  **ALSO FIXED A PRE-EXISTING BATTERY REGRESSION (independent of §8o, discovered while verifying neighbors):** `sasi_bm` (the §6i hand-mirrored full-FAT-stack test) had silently gone over the small-model 64 KB `_TEXT` ceiling — upstream newlibc FAT/VFS/block growth pushed its small-model code to 66,315 B, 779 B over the 65,536 ceiling, so it WRAPPED and hung at startup (the §6p/§6q symptom; manifested as a phase-1 truncation that did NOT advance even at a 300 s budget).  `sasi_bm.bin` is byte-identical under my if/elif change (same obj set via the elif), so this regression predates §8o; the documented fix is the model bump small → medium (multi-CS, no single-`_TEXT` ceiling): code 71,615 B, golden UNCHANGED, runs to completion (entry `sasi_bm:90:::hd` → `sasi_bm:180:::hd:medium`, budget bumped for the heavier image).  A ceiling sweep of the other small FAT/disk battery entries found `sasi_bm` was the ONLY one over — `sasi_fat_smoke_test` is closest at 65,221 B (still under), `fat_victor_label_test`/`block_test`/`snprintf_test`/`fat_bpb_test` all ~61 KB — so the bump is bounded to `sasi_bm`.  **VERIFICATION:** `sasi_upstream_bm` `[ok]` (battery 44 → 45) AND `sasi_bm` `[ok]` (now medium) through the battery harness (build + MAME run + golden-diff); the environment baseline was confirmed healthy throughout (`timer_bm`/`timer_upstream_bm`/`stdio_bm` all `[ok]`, which is what isolated the `sasi_bm` hang to the ceiling rather than my change).  `make check` green; only build script + a new test + golden + two battery entries changed, so the compiler is byte-identical → test-dos UNCHANGED, MP/stevie provably unaffected, no emit audit, no MP byte-compare.  newlibc tree is UNTOUCHED this session (the §8k port is already committed on branch `minic-asm-port` @ `5b6b261`).  **⇒ Next session — four upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o) now proven RUNNING bare-metal; all follow-ups consumer-driven (pick with the user):** (1) continue the sweep — the remaining §8k-translated drivers are `serial`/`console` (7201 polled + RX-ISR; the §7i loopback covers the cooked side, and `bm_console.c` is in the BASE SUPPORT_TUS set linked into EVERY image, so the upstream `console.c` rule must be carefully scoped not to collide) and `pic` (8259 init/mask; `bm_pic.c` is linked by EVERY interrupt-driven test, so the upstream `pic.c` rule must NOT fire for those — a non-testhost hand-mirrored `pic_bm` re-pointed at upstream `pic.c` is the path; the §8o if/elif is the template for the mutual-exclusion structure); each is the exact §8l/§8m/§8n/§8o pattern (self-contained test + a guarded upstream-driver SUPPORT_TUS rule); (2) the minic-PARSE bucket (a DIFFERENT track than §8k's nasm bucket): `interrupts.c`'s ISR-function-pointer parameter declarator (`void ISR_HANDLER (*isr)(void)`) is a real bounded minic FRONTEND parse-feature, `vshell.c` + 6 `dos_tests` carry Watcom `_asm{}` blocks (park or rewrite); (3) decide whether to MERGE the `minic-asm-port` newlibc branch into `main` and retire the now-redundant §6y `minic/dos/newlibc/interrupts.h` shadow now that upstream `interrupts.h` is minic-aware.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8o session notes (2026-06-17)

### The pick
- §8n handoff: bare-metal-RUN proven for three drivers (timer §8l, display
  §8m, keyboard §8n); three consumer-driven follow-ups.  User
  (AskUserQuestion) chose option (1) "continue the driver sweep", then chose
  `sasi` over `pic`/`serial`: distinct subsystem (real `-scsi:0` disk I/O),
  exercises the §8k SASI-handshake inline-asm port, MEDIUM-model coverage was
  expected (timer/display/keyboard were all small), §6i `sasi_bm` template,
  cleanest collision story (only `bm_sasi.c` via the `sasi\.h` probe).

### What was built
- New test `minic/dos/newlibc/sasi_upstream_bm.c` (modeled on §6i sasi_bm.c
  re-pointed at the upstream API + §8l/§8n ISR pattern): drives the UPSTREAM
  `sasi_register` + block registry at the BLOCK level (register / block_init /
  geometry / read LBA 0 / label check / uncached re-read checksum / WRITE(6)
  round-trip / timer-still-ticking).  Links newlibc's OWN drivers/sasi.c IN
  PLACE OF bm_sasi.c.  Raw serial output (bm_puts/bm_puthex/bm_putu), NO
  bm_stdio (the §8l/§8m/§8n shape) — the FAT/VFS layer is already covered by
  §6i/§6p, so block-level fully exercises the §8k port without it.
- HEADLINE: SASI transfers run UNDER A LIVE TIMER ISR (upstream timer.c §8l,
  also linked) to validate the §8k SAVE_ES DROP.  sasi.c's HW_READ/WRITE_BYTE
  load ES (`mov es, word [bp-12]` + `es:[bx]`) for far MMIO; a tick between the
  `mov es` and the access is ES-safe only because the §6d ISR prologue saves
  ES — so a clean round-trip under the live ISR proves dropping SAVE_ES (the
  ISR ABI owns ES) is correct.  TWO §8k drivers (sasi + timer) under live
  interrupts.  NOTHING from bm_sasi.c / bm_timer.c linked (map: sasi_register
  from sasi.obj, timer_* from timer.obj).
- §8k Intel forks exercised live, verified in generated asm: sasi_save_flags_cli
  → `pushf` / `pop word [bp-10]` / `cli` (§8j %0 operand → frame slot), ZERO
  gas leftovers; SAVE_ES/RESTORE_ES → no-ops via §6y shadow interrupts.h.
- Unlike §8n keyboard, NO link-time gap: sasi.c calls no interrupts_enable /
  get_interrupt_vector, only self-contained flags-save asm + no-op'd SAVE_ES.

### build-newlibc-baremetal.sh change (build-glue ONLY, additive)
- The single `grep -q 'sasi\.h'` rule (matched BOTH "sasi.h" and "bm_sasi.h" →
  always bm_sasi.c) became an if/elif: non-testhost + non-bm_stdio + leading-
  quote `'"sasi\.h"'` → UPSTREAM $NL/drivers/sasi.c + block.c; `elif grep -q
  'sasi\.h'` → bm_sasi.c + block.c.  MUTUALLY EXCLUSIVE so the two SASI drivers
  never both link.  `'"sasi\.h"'` does NOT match `"bm_sasi.h"` (the char before
  `sasi` is `_`, not a quote).
- Verified all three SASI tests route correctly via the link map:
  sasi_upstream_bm (non-testhost, no bm_stdio, "sasi.h") → sasi.obj + timer.obj;
  sasi_bm (bm_stdio, bm_sasi.h) → bm_sasi.obj (elif); sasi_sector_test
  (test-host, upstream "sasi.h") → bm_sasi.obj (elif, TESTHOST=0 guard fails
  the first branch).
- The -D__MINIC__ on clang -E (added by §8l) makes sasi.c take the Intel
  pushf/pop/cli fork; the §8l timer / §8m display / §8n keyboard rules untouched.

### Pre-existing battery regression FIXED (independent of §8o)
- Discovered while verifying neighbors: sasi_bm (§6i full-FAT-stack hand-mirror)
  hung at startup — upstream newlibc FAT/VFS/block growth pushed its SMALL-model
  code to 66,315 B, 779 B over the 65,536 _TEXT ceiling → WRAPPED and hung (the
  §6p/§6q symptom; phase-1 truncation that did NOT advance even at 300 s).
- sasi_bm.bin is byte-identical under my if/elif (same obj set via elif), so the
  regression PREDATES §8o.  Fix = documented model bump small → medium (multi-CS,
  no single-_TEXT ceiling): code 71,615 B, golden UNCHANGED, full run.  Entry
  `sasi_bm:90:::hd` → `sasi_bm:180:::hd:medium` (budget bumped for the heavier
  image; verified completes at 180 s).
- Ceiling sweep: sasi_bm was the ONLY small FAT/disk entry over.  Nearest is
  sasi_fat_smoke_test 65,221 B (under); fat_victor_label_test / block_test /
  snprintf_test / fat_bpb_test all ~61 KB.  Bump bounded to sasi_bm.

### Verification
- sasi_upstream_bm: FIRST-RUN PASS on MAME victor9k, 10 phases (PIC re-init +
  timer ISR / upstream timer_init / sti / register upstream sasi + block_init /
  geometry 59058 / read LBA 0 under live ISR / label "tandon_703_mame" /
  uncached checksum match / WRITE(6) round-trip @ LBA 59057 under live ISR
  verified / timer still ticking).  small (16,781 B); hd disk (scratch copy);
  90 s budget.  Golden deterministic (fixed image bytes + booleans).
- Battery harness (build + MAME run + golden-diff): sasi_upstream_bm [ok]
  (battery 44 → 45) AND sasi_bm [ok] (now medium).
- Environment baseline confirmed healthy throughout (timer_bm /
  timer_upstream_bm / stdio_bm all [ok]) — this isolated the sasi_bm hang to
  the ceiling, NOT my change.
- make check green; compiler byte-identical (only build script + new test +
  golden + two battery entries changed) → no emit audit, no MP byte-compare,
  test-dos unchanged.  Bare-metal ONLY (no raw -scsi:0 on DOS).  newlibc tree
  untouched (§8k port already committed on minic-asm-port @ 5b6b261).

### ⇒ Next session (consumer-driven, with the user)
- Four upstream drivers (timer §8l, display §8m, keyboard §8n, sasi §8o) now
  RUNNING bare-metal.  Remaining §8k-translated drivers: serial/console + pic.
- (1) continue the sweep — serial/console (7201 polled + RX-ISR; bm_console.c
  is in the BASE SUPPORT_TUS set linked into EVERY image, so scope the upstream
  console.c rule carefully) and pic (8259; bm_pic.c is linked by EVERY
  interrupt-driven test → the upstream pic.c rule must NOT fire for those; a
  non-testhost hand-mirrored pic_bm re-pointed at upstream pic.c is the path,
  the §8o if/elif is the mutual-exclusion template); each is the exact
  §8l/§8m/§8n/§8o pattern (self-contained test + a guarded SUPPORT_TUS rule);
- (2) minic-PARSE bucket: interrupts.c ISR-fn-ptr param declarator (bounded
  frontend feature); vshell + 6 dos_tests Watcom _asm{} (park/rewrite);
- (3) decide: merge minic-asm-port into newlibc main; retire the redundant
  §6y minic/dos/newlibc/interrupts.h shadow.
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

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

Older session headers (§8m and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
