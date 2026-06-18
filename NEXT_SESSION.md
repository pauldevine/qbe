# Next session (the §8y handoff made the §8w-ported dos_tests RUNNABLE — `test_memory_layout` DOSBox-gated — and left consumer-driven follow-ups; the user (AskUserQuestion) chose **(1) gate the six HARDWARE dos_tests on MAME victor9k**.  §8z [2026-06-18, this session] **GATED the two Victor-runnable hardware dos_tests on MAME (`test_es_preservation`, `test_display_dos`) — AND, in the process, FOUND + FIXED A REAL QBE CODEGEN BUG: inline-asm register clobbers were parsed by minic but emitted only as a comment, so the register allocator freely kept live values in clobbered registers across the asm.**  Gating es/ss_preservation surfaced spurious "FAIL"s on real Victor hardware: a `cmp` against a register (BX) that an `int 0x21` clobbered, because minic emitted the clobber list (`"ax","bx",…`) only as a `# clobbers:` comment that QBE never parsed.  BX/SI/DI are callee-saved here (the allocator's preferred home for values live across calls), so an asm declaring `"bx"` silently trashed a live value.  **THE FIX (the §8z headline, qbe master, two commits):** plumb a per-`Oasm` clobber mask through register allocation, mirroring the existing `divclob` (AX:DX) handling — except the asm's set may include callee-saves: (1) **minic.y** emits the clobber list as a parseable `asm "code", <mask>` operand (`BIT(reg)` of the i8086 GP regs; nonzero-only so memory-only clobbers keep the `.ssa` byte-stable); `asm_clobber_bit` maps names→the i8086 reg numbers.  (2) **parse.c / all.h** store the mask in a parallel `Fn.asmclob` table keyed by the asmstr index in `Oasm` `arg[0]`.  (3) **spill.c** for an `Oasm` with clobbers reserves a register per clobbered GP in the live-across `limit2` (clamped so the always-live RGLOB BP/SP stay kept — else `limit` tries to spill a fixed reg → "cannot spill register" abort) and adds the clobber set to the `sethint` avoid mask.  (4) **rega.c** frees each clobbered register at the `Oasm` (the definitive guarantee; mirrors the `Ocall` `T.rsave` loop, via a new file-scope `curfn`).  (5) **i8086/all.h** `MAKESURE` pins `RAX..RDI = 1..6` (the numbers minic emits).  **GATED bug-loud** by `minic/dos/examples/asm_clobber_probe.c` (small+medium+compact+large+huge): five values kept live across an asm that clobbers ALL GP regs — the UNFIXED compiler reads the junk (`sum=8856`, the hex constants), the FIXED one is exact (`sum=140`); verified by reverting the compiler via `git stash`.  **VALIDATION:** `test-dos` 397 → **402/402** (5 new probe entries); `make check` green; **emit audit clean** (126,885 regions, 0 clobbers — spill/rega are core regalloc, audited though emit.c untouched); **MP compact body 689,760 BYTE-IDENTICAL** (MP has no inline-asm clobbers → the fix never fires there, no Victor run); verified on **real MAME victor9k** (es_preservation Test 2/4 print "PASS"/"All tests PASSED", were "FAIL"/"WARNING").  `test_memory_layout`'s DOSBox golden was re-captured (its inline-asm functions' frame layout shifted a few bytes → loader-derived addresses moved, all PASS verdicts intact — the §6v re-capture pattern, deterministic).  **THE MAME GATE (`tools/test-victor.sh`, a new `DOS_TESTS` section + `run_dos_test_victor`):** build via `build-newlibc-test.sh`, run via `run-victor-sasi.sh`, diff serial stdout vs a golden.  **Gated: `test_es_preservation` + `test_display_dos`** — Victor-native MMIO only (CRTC/screen/font/ES-save), deterministic (verified by a determinism re-run through the gate path), all-PASS; goldens `minic/dos/tests/dos_test_{es_preservation,display_dos}.golden.txt`.  **THE OTHER FOUR ARE NOT GATEABLE — a genuine Victor/IBM-PC HARDWARE INCOMPATIBILITY finding (NOT a toolchain defect):** they call IBM-PC BIOS services the Victor 9000 does not implement and CRASH (the 8088 triple-faults, reboots, AUTOEXEC re-runs the program → the serial capture shows the banner + early tests LOOPED 14-19× and never the later ones).  Confirmed by counting per-`Test N:` header repetitions: `test_timer_dos` and `test_integration` crash at the FIRST `get_dos_ticks()` (INT 1Ah, the IBM-PC time-of-day service — `test_integration`'s "Timer baseline" line NEVER prints; timer dies in Test 2, integration in Test 4); `test_keyboard_dos` crashes in Test 3 at `kbhit()`/`getch()` (INT 16h conio); `test_serial_dos` crashes in Test 2 (and its Test 5 also reprograms the 7201 channel A / 8253 channel-0 baud = the very serial line the harness captures through).  This is the SAME class as §8y's note that `test_memory_layout` "hangs only at Test 6's INT 0x12".  All four BUILD and RUN — the §8z newlibc vector-intrinsic / softfloat / `clock()` fills WORK (`test_timer_dos`'s `_dos_getvect` prints the live INT 0x42 handler `D832:2706` before the INT 1Ah crash) — they are just not Victor-runnable AS WRITTEN.  **ENABLING WORK so timer/keyboard at least LINK + run-to-crash (newlibc `victor9K_newlibc` PR #24, branch `minic-dostest-hw-gate`, additive `#else`=Watcom byte-for-byte):** `v9k_hardware.h` defines `_dos_getvect` (INT 21h AH=35h→ES:BX, ES restored to DGROUP), `_dos_setvect` (AH=25h, DS:DX via `lds`, DS push/pop-preserved), `_chain_intr` (`pushf` + far call so the chained DOS handler's `iret` returns to us; observably ≡ Watcom's tail-jump for the tests) — all declare their clobbers, now honored by the §8z fix; `test_timer_dos.c` gets a `clock()` `__MINIC__` fill (INT 1Ah, `CLOCKS_PER_SEC=18`).  `tools/build-newlibc-test.sh` gained a `--softfloat` flag (links `minic/dos/softfloat.c` for the `call far _sf_*` lowerings test_timer_dos needs; it builds MEDIUM at 82,975 B with softfloat).  **git scope:** qbe master — `4b5cb90` (the clobber fix: all.h/i8086-all.h/parse.c/rega.c/spill.c/minic.y + asm_clobber_probe.c+golden + test-dos.sh + test_memory_layout golden re-capture) and `afd355d` (the MAME gate: test-victor.sh + build-newlibc-test.sh `--softfloat` + es/display goldens); newlibc `victor9K_newlibc` **PR #24** (`minic-dostest-hw-gate` `e013ecf`: v9k_hardware.h intrinsics + test_timer_dos clock()).  **⇒ Next session — the dos_tests-on-MAME track is COMPLETE (2 of 6 cleanly gateable; 4 documented as Victor-incompatible IBM-PC-BIOS callers).  Remaining consumer-driven follow-ups (pick with the user):** (1) the FILE-SCOPE function-pointer VARIABLE grammar (`void (*v)(void);` at file scope is a parse error — a real general minic grammar hole; a typedef-based fn-ptr already works); (2) IF Victor-native timer/keyboard coverage is wanted, author Victor-native (INT 1Ah/16h-free) timing/keyboard probes against `bm_timer`/`bm_keyboard` or the upstream drivers — the §8l/§8n bare-metal pattern already covers this ground, so DOS-hosted versions add little; (3) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (4) merge newlibc PR #24.  NO QBE/minic codegen bug open (the §8z one is FIXED); NO carried compiler track remains.)

## §8z session notes (2026-06-18)

### The pick
- §8y handoff: dos_tests RUNNABLE (test_memory_layout DOSBox-gated); the user
  (AskUserQuestion) chose **(1) gate the six HARDWARE dos_tests on MAME victor9k**.

### The headline: a real QBE codegen bug (inline-asm clobbers ignored)
- Gating test_es_preservation/test_ss_preservation surfaced spurious "FAIL"s on
  real hardware: `cmp si/bx, ax` against a register an `int 0x21` clobbered.
- ROOT CAUSE: minic parsed the asm clobber list into a struct but emitted it
  only as a `# clobbers:` COMMENT.  QBE's Oasm had zero clobber info, so the
  allocator kept live values (BX/SI/DI are CALLEE-saved here = its preferred
  home for values live across calls) in registers an asm declared clobbered.
- FIX (mirrors divclob, but the set can include callee-saves):
  - minic.y: emit `asm "code", <mask>` (BIT of the GP regs; nonzero-only →
    .ssa byte-stable for memory-only clobbers).  asm_clobber_bit: name→regnum.
  - parse.c/all.h: parallel Fn.asmclob table, keyed by the asmstr index.
  - spill.c: limit2 reserves a reg per clobbered GP (CLAMPED so keep >= nrglob,
    else limit() spills BP/SP → "cannot spill register" abort) + sethint avoid.
  - rega.c: rfree each clobbered reg at the Oasm (definitive; new curfn global).
  - i8086/all.h: MAKESURE RAX..RDI == 1..6.
- GATE: asm_clobber_probe.c (5 models) — unfixed sum=8856 (junk), fixed sum=140;
  bug-loudness confirmed by reverting the compiler via git stash.
- VALIDATION: test-dos 397→402; make check green; emit audit clean (126885
  regions, 0 clobbers); MP body 689,760 byte-identical; es_preservation
  Test 2/4 PASS on MAME (were FAIL/WARNING).
- test_memory_layout DOSBox golden re-captured (frame layout shifted; all PASS
  intact; §6v deterministic re-capture).

### The MAME gate (test-victor.sh)
- New DOS_TESTS section + run_dos_test_victor (build-newlibc-test.sh →
  run-victor-sasi.sh → diff golden).  Entry: name|flags|secs|golden-stem.
- GATED: test_es_preservation (--no-libstub, 120s) + test_display_dos
  (--no-libstub, 120s).  Both deterministic (verified via a re-run through the
  gate path) and all-PASS.  goldens dos_test_{es_preservation,display_dos}.

### Why only 2 of 6 — Victor/IBM-PC hardware incompatibility (NOT a toolchain bug)
- The other four call IBM-PC BIOS services the Victor lacks → CPU triple-fault
  → reboot → AUTOEXEC re-runs → serial capture shows early tests LOOPED 14-19×,
  never the later ones (proven by per-Test-header repetition counts):
  - timer / integration: crash at the first get_dos_ticks() = INT 1Ah (IBM-PC
    time service).  "Timer baseline" never prints; timer dies Test 2, integ
    Test 4.  Same class as §8y's test_memory_layout INT 0x12 hang.
  - keyboard: crashes Test 3 at kbhit()/getch() = INT 16h conio.
  - serial: crashes Test 2 (+ Test 5 reprograms the captured serial channel).
- All four BUILD + RUN; the intrinsics/softfloat/clock work (timer's
  _dos_getvect prints the live INT 0x42 handler before the INT 1Ah crash).

### Enabling work for timer/keyboard to LINK (newlibc PR #24)
- v9k_hardware.h __MINIC__: _dos_getvect (AH=35h, ES restored), _dos_setvect
  (AH=25h, lds, DS push/pop), _chain_intr (pushf + far call); all declare
  clobbers (now honored).  test_timer_dos.c: clock() __MINIC__ fill (INT 1Ah).
- build-newlibc-test.sh: --softfloat (links minic/dos/softfloat.c); timer needs
  medium (82,975 B with softfloat, over the small 64 KB _TEXT ceiling).

### git scope
- qbe master: 4b5cb90 (clobber fix + probe + golden re-capture),
  afd355d (MAME gate + --softfloat + es/display goldens).
- newlibc victor9K_newlibc PR #24 (minic-dostest-hw-gate e013ecf).

### ⇒ Next session (consumer-driven, with the user)
- (1) file-scope function-pointer VARIABLE grammar (`void (*v)(void);` parse
  error — general hole; typedef fn-ptr works);
- (2) Victor-native (INT 1Ah/16h-free) timing/keyboard probes if wanted (but
  §8l/§8n bare-metal already cover that ground);
- (3) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (4) merge newlibc PR #24.
- NO QBE/minic codegen bug open (the §8z one is FIXED); NO carried compiler track.
---

# Next session (the §8x handoff CLOSED the block-scope char-array string initializer and left five consumer-driven follow-ups; the user (AskUserQuestion) chose **(3) make the §8w-ported `dos_tests` genuinely RUNNABLE**.  §8y [2026-06-18, this session] **MADE the dos_tests RUNNABLE — the `v9k_hardware.h` hardware helpers got `__MINIC__` definitions (they were `#pragma aux` declarations minic ignores → undefined at link), so the §8w-ported tests now LINK and run, not just compile; `test_memory_layout` runs DOS-hosted with ALL 7 tests PASS, deterministic, and is GATED via DOSBox (test-dos 396 → 397); the same `.exe` also runs on REAL MAME victor9k (the SASI path is proven for dos_tests); NO qbe/minic/emit compiler source touched (→ `make check` green, compiler byte-identical, MP/stevie provably unaffected, no emit audit, no MP byte-compare).**  The §8k/§8w ports made the dos_tests COMPILE under minic by forking the Watcom `_asm{}` blocks behind `#if defined(__MINIC__)`, but the `v9k_hardware.h` hardware helpers (`v9k_get_es/ss/sp/ds/cs`, `v9k_set_es`, `v9k_read/write_far_byte/word`, `v9k_enable/disable_interrupts`, `v9k_delay_us`) stayed `#pragma aux` register-calling intrinsics that minic silently ignores → declared-`extern`-but-undefined, so any test using them failed at LINK.  **THE ENABLING WORK (newlibc tree, additive — `#else` keeps the Watcom `#pragma aux` byte-for-byte):** define the helpers under `__MINIC__` — the register reads (`mov %0, es` etc.) and `set_es` (`mov es, %0`) via Intel inline asm (§8j: `%0` → the local's frame slot; `asm_to_omf` promotes the size-less seg-reg slot store `mov [bp-N], es` → `mov word [bp-N], es`, which is valid), and the far MMIO (`read/write_far_byte/word`) via `MK_FP` `volatile __far` derefs (the `v9k_hw.h` idiom — minic emits the ES-load + `es:[bx]` access itself; the §6d ISR ABI owns ES, so the Watcom `push es`/`pop es` dance is moot); `sti`/`cli` and a busy-loop `delay_us`.  Validated through the full pipeline (clang → minic → qbe → `asm_to_omf` → nasm) at small AND medium before wiring anything.  **THE BUILD PATH (`tools/build-newlibc-test.sh`, +16, additive):** a `dos_tests/` source resolver (after the `tests/` and `minic/dos/newlibc/` fallbacks) + `-D__MINIC__` passed to the test TU only (the portable newlibc support set is `__MINIC__`-neutral; the quote-include `"v9k_hardware.h"` resolves relative to the source's own `dos_tests/` dir).  Built `--no-libstub` (the §7w real-program default — supplies the `__heap_start` symbol the test reads for heap-start; `test_memory_layout.c`/`test_integration.c` referenced the Unix linker symbol `_end`, which this toolchain has no equivalent for, so a `__MINIC__` fork uses newlibc's `__heap_start` = `_sbrk`'s "end of static data" bracket).  **THE GATE (`tools/test-dos.sh`, +25):** one new entry `dos_test (test_memory_layout)` — built `--no-libstub`, diffed against `minic/dos/tests/dos_test_memory_layout.golden.txt` (the DOSBox output, 63 lines, all PASS).  **CRITICAL GOLDEN LESSON (cost one gate cycle):** the golden captured via standalone `run-dos-exe.sh` carried a TRAILING BLANK LINE (the test's final `printf("\n")`) that `run-dos-batch.sh` (the gate's single-boot batch path) STRIPS — so the first gate run RED'd on a one-line trailing-blank diff (NOT a segment/value mismatch — the batch loads the `.exe` at the SAME segment as standalone, so PSP/CS/DS/SS matched perfectly); fixed by regenerating the golden with trailing blank lines removed (`awk 'NF{p=NR}{a[NR]=$0}END{for(i=1;i<=p;i++)print a[i]}'` — keeps the LEADING blank, which the batch preserves).  Lesson: capture dos-hosted goldens to match `run-dos-batch.sh`'s trailing-newline handling, not `run-dos-exe.sh`'s.  **WHY ONLY `test_memory_layout` IS GATED (DOSBox-portable):** it validates DOS PSP / segment regs / stack / heap / `INT 0x12` conventional memory (all real DOS) and its one Victor-specific check (font RAM at `0:0C00`, Test 7) degrades to a WARNING off-Victor, never a failure → all 7 PASS under DOSBox, run-to-run deterministic (the load-segment/heap-address lines are DOSBox-loader-derived → run-stable, re-capture on a layout change, the §6v pattern; the PASS verdicts robust).  **THE SIX HARDWARE TESTS NEED MAME victor9k, NOT DOSBox** (confirmed empirically): `test_es_preservation`/`test_integration`/`test_serial_dos`/`test_display_dos` (+ `test_timer_dos`/`test_keyboard_dos`, which also need the `_dos_getvect`/`_dos_setvect`/`_chain_intr` vector intrinsics still undefined) poke Victor MMIO (CRTC `0xE800`, NEC-7201 serial `0xE040`, VIA, screen `0xF000`) that DOSBox lacks — they hang (newlibc stdio is exit-flushed, so they show empty output) or DIVERGE (e.g. `test_es_preservation` Test 1 prints "ES was preserved" under DOSBox's DOS, the OPPOSITE of Victor's documented ES corruption).  **MAME victor9k IS PROVEN for dos_tests** — `tools/run-victor-sasi.sh build/newlibc-tests/test_memory_layout/test_memory_layout.exe` boots Victor MS-DOS from SASI and captures the .exe's serial stdout: it reports the AUTHENTIC 816 KB (`CC31` paragraphs, vs DOSBox's 639 KB `9FFF`), and the heap offsets (`14D4`/`0x1870`/…) are byte-identical to DOSBox (DGROUP-relative); it hangs ONLY at Test 6's `INT 0x12` (an IBM-PC BIOS conventional-memory call the Victor 9000 doesn't implement — a genuine Victor-vs-PC difference, which is exactly why DOSBox is the right gate target for THIS test and the MAME run loops the suite).  **VERIFICATION:** full gate **397/397 ok**; `make check` green; §8w triage still PASS 73 small AND medium (the `v9k_hardware.h` restructure broke no dos_test COMPILE); the helper asm hand-checked correct (`mov word [bp-10], es`, `mov es, [bp-10]`, far access `mov es, word [bp-12]` + `es:[bx]`); MP NOT rebuilt (build scripts don't affect MP — it builds via `build-micropython.sh`, links none of these).  No compiler/qbe/emit/`asm_to_omf` change → no emit audit, no MP byte-compare.  **git scope:** qbe master (`tools/build-newlibc-test.sh` +16, `tools/test-dos.sh` +25, new `minic/dos/tests/dos_test_memory_layout.golden.txt` — NO compiler/qbe/emit/build-logic-affecting-MP change); newlibc feature branch `minic-dostest-asm-port` `f27d468` (`v9k_hardware.h` +84 helper defs, `test_memory_layout.c`/`test_integration.c` +7 each `_end`→`__heap_start` fork; PUSHED + **PR #23** on `victor9K_newlibc` carrying both the §8w port (`04ffa1e`) and §8y runnable (`f27d468`) commits, per [[feedback_newlibc_use_prs]] — the user asked for the PR at session end).  **⇒ Next session — the dos_tests are RUNNABLE (1 of 7 gated via DOSBox; the dos_test build path + MAME-victor9k SASI path both proven).  Remaining follow-ups are consumer-driven (pick with the user):** (1) **gate the six HARDWARE dos_tests on MAME victor9k** — the build path works and `run-victor-sasi.sh` is proven; needs per-test goldens captured from the real Victor (slow, minutes each; output may carry non-deterministic bits like CRTC cursor positions → may need filtering), the two vector-intrinsic tests (`timer`/`keyboard`) need `_dos_getvect`/`_dos_setvect`/`_chain_intr` DEFINED under `__MINIC__` (real DOS INT 21h AH=35h/25h via inline asm), and `test_serial_dos` CONFLICTS with the serial console it captures through (it reprograms the NEC-7201 channel A — needs the display-only capture path or a skip); a `test-victor.sh` entry (the on-target gate), not `test-dos.sh`; (2) the FILE-SCOPE function-pointer VARIABLE grammar (`void (*v)(void);` at file scope is a parse error — a real general minic grammar hole, surfaced by `test_timer_dos`'s handler var which §8w sidestepped with `void __far *`; a typedef-based fn-ptr already works); (3) ~~push the newlibc branch via a PR~~ DONE — PR #23 on `victor9K_newlibc` (`minic-dostest-asm-port`, both §8w+§8y commits), opened at §8y session end; (4) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8y session notes (2026-06-18)

### The pick
- §8x handoff: block-scope char-array string-init CLOSED; five consumer-driven
  follow-ups.  User (AskUserQuestion) chose **(3) make the §8w-ported dos_tests
  genuinely RUNNABLE** — the biggest of the follow-ups (a new run capability +
  gate, beyond the §8k/§8w compile-only bar).

### The blocker (why they only COMPILED, never LINKED)
- §8k/§8w forked the Watcom `_asm{}` blocks under `#if defined(__MINIC__)`, so
  the tests COMPILE.  But v9k_hardware.h's hardware helpers (v9k_get_es/ss/sp/
  ds/cs, set_es, read/write_far_byte/word, enable/disable_interrupts, delay_us)
  stayed `#pragma aux` register-calling intrinsics — minic IGNORES `#pragma aux`,
  so they were extern-but-undefined → any test using them failed at LINK.

### The enabling work (newlibc tree, additive; #else keeps Watcom byte-for-byte)
- Define the helpers under `__MINIC__`:
  - register reads `mov %0, es` + set_es `mov es, %0` via Intel inline asm
    (§8j %0->frame slot; asm_to_omf promotes `mov [bp-N], es`->`mov word
    [bp-N], es`, valid for a 16-bit seg reg).
  - far MMIO read/write_far_byte/word via MK_FP volatile-far derefs (the
    v9k_hw.h idiom; minic emits the ES-load + es:[bx] itself; §6d ISR ABI owns
    ES so the Watcom push-es/pop-es is moot).
  - sti/cli + a busy-loop delay_us.
- Validated through clang->minic->qbe->asm_to_omf->nasm at small AND medium
  BEFORE wiring (a throwaway helper probe), then confirmed the actual tests
  compile (5 no-vector tests + triage still PASS 73).

### The build path (build-newlibc-test.sh +16, additive)
- dos_tests/ source resolver (after tests/ and minic/dos/newlibc/) +
  -D__MINIC__ on the TEST TU only (support set is __MINIC__-neutral; the quote
  include "v9k_hardware.h" resolves from the source's own dir).
- Built --no-libstub (§7w real-program default).  test_memory_layout.c /
  test_integration.c used the Unix linker symbol `_end` (heap start) -> forked
  to newlibc's `__heap_start` (heap.asm's _sbrk bracket) under __MINIC__.

### The gate (test-dos.sh +25): dos_test (test_memory_layout), DOSBox
- All 7 tests PASS, deterministic; golden = minic/dos/tests/
  dos_test_memory_layout.golden.txt (63 lines).
- GOLDEN LESSON (cost one gate cycle): the run-dos-exe.sh capture had a TRAILING
  BLANK LINE (test's final printf("\n")) that run-dos-batch.sh (the gate's
  batch path) STRIPS -> first gate run RED on a 1-line trailing-blank diff (NOT
  a load-segment mismatch -- batch loads at the SAME segment as standalone, all
  PSP/CS/DS/SS matched).  Fix: regenerate golden with trailing blanks removed
  (awk 'NF{p=NR}{a[NR]=$0}END{for(i=1;i<=p;i++)print a[i]}', keeps leading
  blank).  Lesson: capture dos-hosted goldens to match run-dos-batch.sh.

### Why only test_memory_layout is DOSBox-gateable
- It validates DOS PSP/segments/stack/heap/INT 0x12 (all real DOS); its one
  Victor-specific check (font RAM 0:0C00, Test 7) degrades to a WARNING
  off-Victor.  Load-segment/heap lines are DOSBox-loader-derived (run-stable;
  re-capture on layout change, §6v); PASS verdicts robust.

### The six HARDWARE tests need MAME victor9k (confirmed empirically)
- es_preservation/integration/serial/display (+ timer/keyboard, which also need
  the _dos_getvect/_dos_setvect/_chain_intr intrinsics, still undefined) poke
  Victor MMIO (CRTC 0xE800, 7201 0xE040, VIA, screen 0xF000) DOSBox lacks ->
  they HANG (empty output, stdio is exit-flushed) or DIVERGE (es_preservation
  Test 1 prints "ES preserved" under DOSBox, the opposite of Victor).
- MAME victor9k IS PROVEN for dos_tests: run-victor-sasi.sh runs
  test_memory_layout on real Victor -> 816 KB (CC31, vs DOSBox 639 KB);
  heap offsets byte-identical (DGROUP-relative).  It HANGS at Test 6's INT 0x12
  (IBM-PC BIOS call the Victor lacks) -> the suite loops -> exactly why DOSBox
  is the right gate target for THIS test.

### Verification
- Full gate 397/397 ok; make check green; §8w triage still PASS 73 small AND
  medium (v9k_hardware.h restructure broke no compile); helper asm hand-checked.
- No compiler/qbe/emit/asm_to_omf change -> no emit audit, no MP byte-compare
  (MP links none of these; build scripts don't affect MP).

### git scope
- qbe master: tools/build-newlibc-test.sh +16, tools/test-dos.sh +25, new
  minic/dos/tests/dos_test_memory_layout.golden.txt.  No compiler/qbe/emit.
- newlibc minic-dostest-asm-port f27d468: v9k_hardware.h +84 (helper defs),
  test_memory_layout.c/test_integration.c +7 each (_end->__heap_start fork).
  Committed LOCALLY, NOT pushed ([[feedback_newlibc_use_prs]]).

### ⇒ Next session (consumer-driven, with the user)
- (1) gate the six HARDWARE dos_tests on MAME victor9k (test-victor.sh, not
  test-dos.sh): build path + run-victor-sasi.sh proven; needs per-test Victor
  goldens (slow; may need filtering of nondeterministic CRTC bits), the two
  vector-intrinsic tests need _dos_getvect/_dos_setvect/_chain_intr DEFINED
  under __MINIC__ (INT 21h AH=35h/25h inline asm), and test_serial_dos conflicts
  with the serial console it captures through (display-only capture or skip);
- (2) file-scope function-pointer VARIABLE grammar (`void (*v)(void);` parse
  error -- general hole; typedef fn-ptr works);
- (3) DONE: PR #23 (victor9K_newlibc, minic-dostest-asm-port, §8w+§8y) opened at session end;
- (4) deepen the capstone (cooked /dev/console; far-code interrupts.c model).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

Older session headers (§8w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
