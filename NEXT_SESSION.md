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

Older session headers (§8n and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
