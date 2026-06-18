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

# Next session (the §8w handoff PORTED the 6 Watcom-`_asm{}` `dos_tests` to compile under minic, emptying the minic-reachable triage bucket, and left five consumer-driven follow-ups; the user (AskUserQuestion) chose **(1) the block-scope char-array string-init gap**.  §8x [2026-06-18, this session] **CLOSED the BLOCK-scope char-array string initializer — minic now parses `char NAME[] = "s";`, `char NAME[N] = "s";`, and `static char NAME[N] = "s";` inside a function body (the §8v file-scope fix's natural follow-on); the fix is a frontend `minic.y` change → no emit audit; test-dos 391 → 396; conflicts UNCHANGED at 115 shift/reduce, 0 reduce/reduce; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  §8v had fixed only the FILE-scope sized form.  Inside a function body, EVERY string-array init except the already-working static-unsized `static char a[] = "x"` was a hard parse error — in BOTH the function-top (`dcls`) context AND the mid-block (statement) context: `char a[] = "x"`, `char a[N] = "x"`, and `static char a[N] = "x"` all hit `parse error`.  (The brace forms `int a[N] = {…}` and the static-unsized string form already parsed; the file-scope `char a[N] = "x"` was §8v.)  **THE FIX (frontend `minic.y`, additive, +194 lines, designed conflict-free):** SIX new productions — the three forms × {dcls, stmt} contexts.  ● The NON-static forms (`char a[]`/`char a[N]`) allocate a stack array and initialize it at RUNTIME via a deferred store chain: a new `mk_local_string_init(v, str_idx, total)` decodes the literal's bytes (via a new `strlit_decode`, which mirrors `strlit_bytelen`'s escape handling — `\xHH`, `\NNN` octal, single-char escapes — but emits the decoded byte VALUES) and lays one `v[i] = byte` assignment per byte (literal bytes then a zero-filled tail to `total`), chained into a comma node; in the dcls context it is `expr()`'d at parse time (entry block == lexical order), in the stmt context it is deferred as `mkstmt(Expr, chain)` so it re-runs in control-flow order on each block re-entry.  ● The STATIC-sized form (`static char a[N] = "s"`) reuses §8v's `emit_string_array_sized(...)` with `static_local=1` (a mangled file-scope `{ b "...", b 0 , z PAD }` data block) — identical to the §8v file-scope path.  The unsized size is the literal length incl NUL; the sized form dies clearly if the declared size is shorter than the natural length (the drop-NUL edge — no consumer).  Each new production's lookahead after `'=' ` (STR vs the brace/`gaggr` `'{'` sibling) is DISTINCT, so the conflict count stays 115/0.  **THE HEADLINE CORRECTNESS PROPERTY — RE-ENTRANCY:** a non-static local string-array MUST re-initialize on every call (a naive "store it in a shared global" mis-implementation would not); the gate's `fresh_each_call()` (mutates a `char buf[6]="ab"`, returns whether `buf[0]=='a'` at entry, called twice → `1 1`) and `persist_across_calls()` (a `static char s[8]="x"` appended each call → `2 3`) pin both halves bug-loud.  Verified IR: non-static = `alloc` + per-byte `storeb` (literal bytes + zero-fill) emitted in control-flow order AFTER preceding statements; static = the `{ b "..", b 0 , z N }` block.  **GATED `blockscope_str_array_probe` small+medium+compact+large+huge** (`minic/dos/examples/blockscope_str_array_probe.c` + golden, 5 entries in `tools/test-dos.sh`) — exercises all SIX new productions (dcls/stmt × unsized/sized/static-sized) + escapes (`\t\x41` → 9,65,0…) + empty string + zero-filled slack + the re-entrancy pair; model-independent golden (contents/lengths/booleans).  Bug-loud verified: the §8v-era (unfixed) minic gives `parse error` on every form, so the build fails outright (a git-stashed-grammar minic build exits 1 on the probe; the direct `char a[]="hi"` / `char a[8]="hi"` / `static char a[8]="hi"` repros all errored pre-fix, parse + run correctly post-fix; the DOS run is byte-identical to the native `cc` run).  **VERIFICATION:** conflicts 115/0 unchanged; `make check` green; MP compact body 689,760 byte-identical (MP has NO block-scope string-array inits — they were parse errors, so the new productions never fire) → no Victor run; full gate **396/396**.  Frontend-only (`minic.y`) → no emit audit.  **KNOWN BOUNDED inefficiency (no consumer, left as-is):** a large non-static `char big[1024] = "x"` emits 1024 individual `storeb`s (the byte-store chain has no run-length collapse); fine for realistic buffer sizes (vshell-class `cwd[64]`), a `memcpy`-from-rodata lowering is the optimization if a big consumer ever appears.  **git scope:** qbe master (`minic.y` +194 = `strlit_decode` + `mk_local_string_init` helpers + 6 productions; new `blockscope_str_array_probe.c` + golden; 5 `test-dos.sh` entries — NO compiler/qbe/emit/build-script, NO newlibc-tree change).  **⇒ Next session — the block-scope string-init gap is CLOSED; the remaining follow-ups are all consumer-driven (pick with the user):** (1) make the §8w-ported `dos_tests` genuinely RUNNABLE — define `v9k_hardware.h`'s `#pragma aux` helpers under `__MINIC__` (register-input asm — minic input-operand-constraint territory) + a DOSBox build/run harness (a substantial new gate, beyond the §8k/§8w compile-only bar); (2) the FILE-SCOPE function-pointer VARIABLE grammar (`void (*v)(void);` at file scope is a parse error — a real general grammar hole, not Watcom-specific, surfaced by `test_timer_dos`'s handler var which §8w sidestepped with `void __far *`; a typedef-based fn-ptr already works); (3) push the local newlibc port branches (`minic-dostest-asm-port` from §8w; the `minic-asm-port`/`minic-isr-entry-port` are already on `origin/main`) via a PR if/when the user wants them published ([[feedback_newlibc_use_prs]]); (4) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8x session notes (2026-06-18)

### The pick
- §8w handoff: dos_tests porting track COMPLETE, minic-reachable triage bucket
  EMPTY; five consumer-driven follow-ups.  User (AskUserQuestion) chose
  **(1) the block-scope char-array string-init gap** — a bounded minic.y
  follow-on to §8v (which fixed only FILE scope).

### The gap (reproduced before touching the grammar)
- Inside a function body, every string-array init except static-unsized was a
  parse error, in BOTH dcls (function-top) and stmt (mid-block) contexts:
    - `char a[]  = "hi"`         -> PARSE ERROR
    - `char a[8] = "hi"`         -> PARSE ERROR
    - `static char a[8] = "hi"`  -> PARSE ERROR
    - `static char a[]  = "hi"`  -> OK (pre-existing dcls/stmt rule)
- Six productions were missing: {unsized, sized, static-sized} x {dcls, stmt}.

### The fix (frontend minic.y, additive, +194 lines)
- New strlit_decode(idx, out, max): decodes literal bytes (escapes mirror
  strlit_bytelen) into a byte buffer; returns the content count.
- New mk_local_string_init(v, str_idx, total): builds a deferred comma-chain
  of `v[i] = byte` (mkidx scales by sizeof(elem)==1 for char) — decoded bytes
  then zero-fill to total.  Used by the NON-static forms.
- NON-static (char a[] / char a[N]): alloc the stack array + varadd, then
  the store chain — expr()'d at parse time in dcls context, mkstmt(Expr) in
  stmt context (control-flow order, re-inits on block re-entry).
- STATIC-sized (static char a[N]): emit_string_array_sized(..., 1) — the §8v
  file-scope path, a mangled `{ b "..", b 0 , z PAD }` data block.
- Sized form dies if declared size < natural length (drop-NUL edge; no
  consumer).  Lookahead after `'='` (STR vs `{`/gaggr) distinct -> 115/0.

### The headline correctness property: re-entrancy
- A non-static local string-array must re-init each call; a static persists.
- fresh_each_call(): `char buf[6]="ab"`, mutate buf[0]='Z', return buf[0]=='a'
  at entry; called twice -> `1 1` (a shared-global mis-impl would give `1 0`).
- persist_across_calls(): `static char s[8]="x"` appended each call -> `2 3`.

### Gate
- blockscope_str_array_probe small+medium+compact+large+huge, one golden.
  Exercises all 6 productions + escapes (\t\x41 -> 9 65 0) + empty string +
  zero-filled slack + the re-entrancy pair.  Model-independent golden.
- Bug-loud: a git-stashed-grammar (unfixed) minic build exits 1 on the probe;
  direct char a[]/a[8]/static a[8] repros parse-errored pre-fix.
- DOS run byte-identical to native cc run.

### Verification
- conflicts 115/0 unchanged; make check green; MP compact body 689,760
  byte-identical (MP has no block-scope string-array inits) -> no Victor run;
  full gate 396/396.  Frontend-only -> no emit audit.

### Known bounded inefficiency (no consumer, left as-is)
- A large non-static `char big[1024]="x"` emits 1024 individual storeb's (no
  run-length collapse in the byte-store chain).  Fine for realistic buffers;
  a memcpy-from-rodata lowering is the optimization if a big consumer appears.

### git scope (qbe)
- master: minic.y +194 (strlit_decode + mk_local_string_init + 6 productions),
  new blockscope_str_array_probe.c + golden, 5 test-dos.sh entries.  No
  emit/qbe/build-script, no newlibc-tree change.

### ⇒ Next session (consumer-driven, with the user)
- (1) make the §8w-ported dos_tests RUNNABLE (v9k_hardware.h #pragma aux under
  __MINIC__ + a DOSBox harness; substantial, beyond the compile-only bar);
- (2) file-scope function-pointer VARIABLE grammar (`void (*v)(void);` at file
  scope is a parse error — general hole, not Watcom; typedef fn-ptr works);
- (3) push the local newlibc branch minic-dostest-asm-port (§8w) via a PR if
  wanted ([[feedback_newlibc_use_prs]]);
- (4) deepen the capstone (cooked /dev/console; far-code interrupts.c model).
- NO QBE/minic codegen bug open; NO carried compiler track remains.

---

Older session headers (§8w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
