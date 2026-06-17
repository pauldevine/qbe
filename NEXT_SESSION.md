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

# Next session (the §8i handoff said NO carried compiler track remained, so this session took on the user-chosen Phase-6 frontier **"compile newlib proper"** — scoped (per the user's correction) to `~/projects/newlibc/phase3_newlib`'s OWN sources, NOT external/upstream newlib.  §8j [2026-06-16, this session] **RE-RAN the §6a triage sweep on the current phase3_newlib tree (now 53/66 TUs compile, up from §6a's 46 — the portable subset all lands) and FIXED the one genuine codegen bug it flushed: GCC extended-asm OUTPUT/INPUT operands (`"=r"`/`"=m"`/`"r"`/`"m"` bound to a local) now work END-TO-END; `tools/test-dos.sh` 376 → 381, the triage qbe failure bucket went 4 → 0, `make check` green, emit audit clean (0 clobbers / 124,955 regions — i8086/emit.c changed so the audit was REQUIRED and run), MP compact body 689,760 BYTE-IDENTICAL → no Victor run.**  The triage's 20 failures split into three clean buckets: **qbe (4)** — `display`/`keyboard`/`pic`/`sasi`, the `"=r"`/`"=m"` output-operand bug (the LONE genuine codegen defect); **nasm (8→12 after the fix)** — gas/AT&T mnemonics (`pushw %%es`, `movb`, `$imm`, AT&T operand order) that nasm rejects; **minic-parse (8)** — `interrupts.c` (an ISR-function-pointer parameter declarator), `vshell.c`, and 6 `dos_tests` (Watcom `_asm{}` blocks).  KEY FINDING: the `"=r"` bug was an **incomplete feature**, not a one-line abort — minic parsed extended-asm operands and substituted the local as `[bp-%name]` into the opaque asm string, but (1) QBE's `promote` ABORTED on an output slot (`slot %x is read but never stored to`, mem.c:108 — the asm's write is invisible to QBE dataflow), (2) an INPUT slot was the dual defect (stored-but-IR-never-read → `promote` forwarded it into a register and removed the alloc, leaving `[bp-%name]` dangling), and (3) the i8086 backend printed the asm string VERBATIM — it never resolved `%name` to a real frame offset, so even past the abort `[bp-%name]` reached nasm as an undefined symbol.  The existing `minic/test/asm_clobber_test.c` (same `"=m"(result)` pattern) was NEVER gated, confirming the feature had never worked end-to-end.  **FIX (3 parts, COPY/ADD-style, additive):** (A) **minic.y** — lower a local-Var operand `%0` to the bare temp ref `%name` (was the never-resolved `[bp-%name]` guess; output branch ~5309, input branch ~5323; the `[_glo%d]` global branch left as-is, no consumer); (B) **QBE `mem.c` new pass `asmvol(fn)`** (declared in all.h, called in main.c right before `markvol`, after `filluse`) — scans every `Oasm` instruction's string for `%name` tokens (skips `%%` escapes), matches each to an alloc temp by name, and sets that alloc's `vol=1` so BOTH `promote` (mem.c:65 vol-skip) and `coalesce` (mem.c:275 vol-skip) keep the slot in memory; `markvol` (run next) then propagates the bit to the slot's own readback load/store.  Handles inputs AND outputs uniformly with NO `promote`-logic change; a no-op unless an asm names a slot, so all currently-compiling code (MP/stevie/the gate) is byte-identical; (C) **i8086/emit.c** `Oasm` handler — resolve each `%name` token to `[bp±N]` via `fn->tmp[t].slot` + the existing `slot(SLOT(idx), fn)` offset formatter; a name matching no slot-resident temp (e.g. a gas `%reg` surviving `%%` handling) is emitted verbatim, so the `%%`→`%` path is byte-identical.  Gated by the all-new `asm_output_probe` (`minic/dos/examples/asm_output_probe.c`, small+medium+compact+large+huge, NASM-valid Intel-syntax mnemonics so it compiles end-to-end and runs) — bug-loud: on the unfixed toolchain the build aborts at the QBE stage → no `.exe` → golden diff fails.  Golden `out_r=4660 / out_m=22136 / dbl21=42 / dblr=9320 / PASS`, byte-identical across all five .EXE models (verified each in DOSBox; `out_r` = `"=r"` immediate write, `out_m` = sasi's `"=m"`, `dbl` = `"=r"` output + `"r"` input round trip).  Generated asm confirms the slot survives + resolves: `mov word [bp-10], 0x1234` (the asm write) then `mov ax, word [bx]` reading the SAME `[bp-10]` (the readback).  **WHAT THIS DOES AND DOESN'T BUY for "compile newlib proper":** the fix eliminates the entire qbe failure CLASS — the 4 drivers no longer abort at QBE and advance one full stage to the nasm bucket — but they STILL don't compile end-to-end because their inline asm is gas/AT&T syntax (minic faithfully compiles C but passes asm templates through VERBATIM; it does not translate syntax).  So PASS stayed at 53; the fix is a real codegen-correctness gain and a PREREQUISITE for any ported driver (a nasm-ported `keyboard.c` still uses `"=r"(flags)`), but the real drivers need source porting to Intel/nasm — exactly what the project already did via the hand-mirrored `bm_*.c` ports, and what §6a flagged as "driver asm needs per-target porting" (step-4 work).  STRATEGY: the fix is additive (new QBE pass + emit-handler extension + 2-line minic substitution change), and provably codegen-neutral for everything that compiles today (MP byte-identical; gate's existing entries all unchanged at 381/381).  **⇒ Next session: NO carried compiler track is open again.**  The "compile newlib proper" frontier for phase3_newlib is now portable-subset-COMPLETE (53/66) with the lone codegen bug fixed; the remaining 13 fails are ALL non-compiler-bug porting work — (1) **gas→nasm inline-asm porting/translation** for the 12 nasm-bucket TUs (drivers display/keyboard/pic/sasi/console/timer + board_init + 6 diagnostic tests) — either an AT&T→Intel translation layer in minic (big, speculative) or per-TU source porting (the `bm_*.c` approach); (2) the **ISR-function-pointer parameter declarator** parse gap in `interrupts.c` (`void ISR_HANDLER (*isr)(void)`); (3) **Watcom `_asm{}` blocks** in the 6 phase-1-style `dos_tests` (park or rewrite).  None is a QBE/minic codegen bug.  A natural next direction is consumer-driven: pursue gas→nasm porting if more real phase3 drivers are wanted under minic, OR pick a different frontier (a parked MicroPython feature track) — chosen with the user.)

## §8j session notes (2026-06-16)

### The pick
- §8i handoff: NO carried compiler track.  User (AskUserQuestion) chose the
  Phase-6 frontier "compile newlib proper".  I initially mis-scoped it to
  external/picolibc newlib; user corrected: it is `~/projects/newlibc/
  phase3_newlib`'s OWN sources.

### Triage re-run (foundation = §6a build/newlibc-triage/sweep.sh, still present)
- Current tree: PASS 53/66 (was 46 at §6a — the portable subset all lands now).
  20 fails: qbe(4) display/keyboard/pic/sasi; nasm(8) gas mnemonics; minic(8)
  interrupts.c ISR-fn-ptr param + vshell + 6 Watcom-_asm dos_tests.
- The qbe bucket was the ONLY genuine codegen bug; the rest is gas-syntax
  porting / toolchain-specific source (the §6a "driver asm needs porting"
  conclusion, re-confirmed).

### The bug = an INCOMPLETE feature (not a one-line abort)
- minic substituted a local operand as `[bp-%name]` but: (1) QBE promote ABORTS
  on the output slot (read-never-stored, mem.c:108); (2) input slots are the
  dual defect (stored-never-read-in-IR → promoted into a register, alloc gone,
  `[bp-%name]` dangling); (3) i8086 emit printed the asm string VERBATIM —
  `%name` was never resolved to a frame offset.  `minic/test/asm_clobber_test.c`
  (same pattern) was never gated → the feature never worked end-to-end.

### The fix (3 parts, additive)
- minic.y: local-Var operand `%0` → bare `%name` (not `[bp-%name]`).
- mem.c asmvol(fn): new pass before markvol; scans Oasm strings for `%name`,
  marks the matching alloc vol=1 → promote+coalesce keep it in memory (inputs
  AND outputs, no promote-logic change).  No-op unless an asm names a slot.
- i8086/emit.c Oasm: resolve `%name` → [bp±N] via tmp->slot + slot(); a name
  matching no slot-resident temp is emitted verbatim (%%→% path unchanged).

### Gate + verification
- NEW minic/dos/examples/asm_output_probe.c + golden, gated small+medium+
  compact+large+huge (NASM-valid mnemonics → runs end-to-end).  Bug-loud:
  unfixed aborts at QBE stage → no .exe → diff fails.
- All 5 .EXE models PASS in DOSBox (out_r=4660/out_m=22136/dbl21=42/dblr=9320).
- test-dos 376 → 381/381 ok; make check green; conflicts unchanged (frontend
  change adds no productions — only action edits + a QBE pass).
- emit audit: i8086/emit.c CHANGED → ran it → 0 clobbers / 124,955 regions
  (the mathfns_probe "build failed" lines are pre-existing soft-float link
  gaps in the standalone audit build, zero inline asm, unrelated).
- MP compact body 689,760 BYTE-IDENTICAL → no Victor run.
- Triage re-run after fix: qbe bucket 4 → 0 (drivers advance to nasm bucket,
  8 → 12, now blocked only on gas syntax).  PASS stays 53 (drivers need asm
  porting to compile end-to-end).

### ⇒ Next session
- NO carried compiler track open.  "Compile newlib proper" portable-subset is
  COMPLETE (53/66); the one codegen bug is fixed.  Remaining 13 fails are all
  porting / toolchain-specific, NOT compiler bugs:
  - gas→nasm porting/translation for the 12 nasm-bucket TUs (the bm_*.c path);
  - interrupts.c ISR-function-pointer param declarator parse gap;
  - Watcom `_asm{}` in the 6 dos_tests (park/rewrite).
- The asm-operand fix is a prerequisite for any nasm-ported driver (ported
  keyboard.c still uses "=r"(flags)).
- Next direction is consumer-driven (gas→nasm porting, or a parked MicroPython
  track) — pick with the user.
---

Older session headers (§8i and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
