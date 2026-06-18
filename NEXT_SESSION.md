# Next session (the §8v handoff CLOSED the file-scope sized char-array string initializer — `char NAME[N] = "string"`, the form `vshell.c` needed — leaving the 6 Watcom-`_asm{}` `dos_tests` as the only remaining minic-PARSE-bucket failures and four consumer-driven follow-ups; the user (AskUserQuestion) chose **the remaining minic-PARSE bucket — the 6 Watcom-`_asm{}` `dos_tests`**.  §8w [2026-06-18, this session] **PORTED all six dos_tests (`test_es_preservation`, `test_integration`, `test_keyboard_dos`, `test_memory_layout`, `test_serial_dos`, `test_timer_dos`) to compile END-TO-END under minic — the §6a triage `minic` bucket (and the `nasm` bucket) dropped 6 → 0 at small AND medium (triage PASS 67 → 73 each), emptying the dos_tests porting track; the work is entirely in the newlibc tree behind `#if defined(__MINIC__)` forks, NO qbe/minic/emit compiler source touched (→ `make check` green by construction, compiler byte-identical, MP/stevie/test-dos provably unaffected, no emit audit, no MP byte-compare).**  This is a §8k-style COMPILE-ONLY porting track (the established bar; there is no dos_tests build/run harness), verified through the full triage pipeline (`minic` → `qbe -t i8086` → `asm_to_omf.py` → `nasm`) at both models, with the generated inline-asm operands hand-checked to resolve to the correct frame slots/sizes and the asm-block semantics matching the Watcom originals.  **The handoff's "just fork `_asm{}`" framing was incomplete — the files carried THREE Watcom-isms, discovered iteratively because the triage reports only the FIRST error per TU:** (1) the `_asm { mov ah,0 … }` brace blocks themselves (all six files; the obvious one); (2) `test_timer_dos`'s Watcom `interrupt far` ISR machinery — a file-scope `interrupt far *` handler VARIABLE + an `interrupt far` ISR DEFINITION + `_chain_intr`/`_dos_getvect`/`_dos_setvect`; (3) `test_keyboard_dos`'s `_dos_getvect` returning an `int` (implicit) assigned to a `void far *` ("invalid assignment", a far-vs-int size mismatch, NOT a parse error).  **THE FORKS (all `__MINIC__`-gated, additive — the `#else` keeps the Open Watcom source byte-for-byte so the phase-1 Watcom build is unaffected; +192 lines across 7 files, 0 deletions):**  ● `_asm{}` → minic `__asm__ volatile(...)` Intel form, `%0`/`%1` bound to the locals' frame slots (§8j operand resolution), outputs via `"=m"`, immediate inputs loaded inside the asm, `int`/`ax`/`bx`/`cx`/`dx`/`memory` clobbers declared (the `keyboard.c` `flags_save` pattern); side-effect-only blocks (`int 0x21` DOS-version, `mov dl,'*'/int 0x21`) take no operands.  ● `test_timer_dos`: the `interrupt far` ISR def → `void __far __attribute__((interrupt))` (the §6d ISR ABI); the file-scope `static void (interrupt far *old_timer_handler)(void)` → a plain `static void __far *old_timer_handler` (minic CANNOT parse a qualified function-pointer type — `void __far (*)(void)` and even a plain file-scope `void (*)(void)` are parse errors, though a typedef'd or block-local fn-ptr works — and the test only stashes/installs the handler, never calls it through C, so a bare far pointer is sufficient); `CLOCKS_PER_SEC` defined `18` (the DOS BIOS-tick rate; minic's `<time.h>` shim declares `clock()` but omits the macro).  ● `dos_tests/v9k_hardware.h` (shared by all 7 dos_tests): a `__MINIC__` block declaring the Watcom DOS-vector intrinsics `_dos_getvect` (→ `void __far *`), `_dos_setvect`, `_chain_intr` as far-pointer functions so `test_keyboard_dos`/`test_timer_dos` type-check (the `#pragma aux` register-asm helpers `v9k_get_es`/etc. already pass through minic harmlessly as `extern` decls — `test_display_dos` proves the header compiles — so they were left as-is per the §8k compile-only bar, undefined at link, no consumer linking these yet).  **TWO toolchain quirks worked around PORT-SIDE (no compiler/toolchain change):** (a) `asm_to_omf.py` promotes a size-less slot store `mov [bp-10], al` → `mov word [bp-10], al` (its symbol-promotion regex `(_?[A-Za-z][\w]*)` also matches register names; harmless for word regs, but `word`+`al` is an invalid-operand-size nasm error) — fixed by writing the byte store explicitly as `mov byte %0, al` (a pre-existing, narrow `asm_to_omf` quirk that only bites a size-less byte-register slot store from inline asm; left unfixed because the script is shared by MP/stevie and a fix would need a full MP byte-compare re-verification — disproportionate, no other consumer); (b) the duplicate-local-name §8k es_ss lesson did NOT bite (each `__asm__` operand is a distinct local within its function).  **VERIFICATION:** triage small AND medium both PASS 73 / 0 fails (minic+nasm buckets empty); the `get_dos_ticks` block resolves to `int 0x1A; mov [bp-30], dx; mov [bp-26], cx` (dx→ticks_low, cx→ticks_high — matches Watcom); the byte store is `mov byte [bp-10], al`; the qbe tree is untouched (`git status` clean but for `.claude/`); `make check` green.  **git scope:** newlibc feature branch `minic-dostest-asm-port` `04ffa1e` (the 6 `dos_tests/*.c` forks + `v9k_hardware.h`; +192 lines, all-additive, `__MINIC__`-gated) — committed LOCALLY, NOT pushed ([[feedback_newlibc_use_prs]]: publish via PR, the user's call, the §8u/§8t precedent); qbe master gets only this handoff + memory (NO compiler/qbe/emit/build-script/gate change).  **⇒ Next session — the dos_tests porting track is COMPLETE, the entire §6a triage `minic` bucket is now EMPTY (all phase3 TUs that minic can reach compile end-to-end); remaining follow-ups are consumer-driven (pick with the user):** (1) the BLOCK-scope / static-local sized-and-unsized char-array string-array init gap (a bounded `minic.y` follow-on to §8v — even `void f(){ char a[]="hi"; }` is a parse error in a function body; no consumer yet); (2) a minic grammar feature for the FILE-SCOPE function-pointer VARIABLE (`void (*v)(void);` at file scope is a parse error — a real general grammar hole, not Watcom-specific, surfaced by `test_timer_dos`'s handler var which §8w sidestepped with a `void __far *`; a typedef-based fn-ptr already works); (3) make the dos_tests genuinely RUNNABLE — define the `v9k_hardware.h` `#pragma aux` helpers under `__MINIC__` (register-input asm — minic input-operand-constraint territory) + a DOSBox build/run harness (a substantial new gate, beyond the §8k compile-only bar); (4) push the merged newlibc ports (`minic-asm-port`/`minic-isr-entry-port`, already on `origin/main` per the §8v note) plus this `minic-dostest-asm-port` branch via a PR if/when the user wants it published; (5) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8w session notes (2026-06-18)

### The pick
- §8v handoff: vshell closed; the 6 Watcom-`_asm{}` dos_tests were the only
  remaining minic-PARSE-bucket failures + four consumer-driven follow-ups.
  User (AskUserQuestion) chose **the 6 Watcom-`_asm{}` dos_tests** porting track.

### What the files actually needed (the triage reports only the FIRST error/TU)
- All 6: `_asm { ... }` Watcom brace-block inline asm.
- test_timer_dos: ALSO `interrupt far` ISR def + file-scope `interrupt far *`
  handler VARIABLE + `_chain_intr`/`_dos_getvect`/`_dos_setvect` + CLOCKS_PER_SEC.
- test_keyboard_dos: ALSO `_dos_getvect` (implicit int) -> `void far *` =
  "invalid assignment" (far-vs-int, not a parse error).
- The `#pragma aux` register-asm helpers (v9k_get_es etc.) already pass through
  minic as harmless `extern` decls (test_display_dos compiles with them).

### The forks (all __MINIC__-gated, additive; #else keeps Watcom byte-for-byte)
- `_asm{}` -> `__asm__ volatile(...)` Intel form, %0/%1 = local frame slots
  (§8j), outputs "=m", immediates loaded in-asm, int/ax/bx/cx/dx/memory clobbers
  (the keyboard.c flags_save pattern).  Side-effect blocks take no operands.
- timer ISR def -> `void __far __attribute__((interrupt))` (§6d ABI); handler
  var -> plain `void __far *` (minic can't parse a qualified fn-ptr type, and
  even plain file-scope `void (*)(void)` is a parse error — the test only
  stashes/installs the handler, never calls it through C); CLOCKS_PER_SEC 18.
- v9k_hardware.h: `__MINIC__` decls for _dos_getvect (void far*)/_dos_setvect/
  _chain_intr so keyboard/timer type-check.

### Toolchain quirks worked around port-side (NO compiler/toolchain change)
- asm_to_omf.py promotes size-less `mov [bp-10], al` -> `mov word [bp-10], al`
  (its symbol regex matches register names too; word+al = nasm invalid-operand-
  size).  Fix: write `mov byte %0, al` explicitly.  Left the script unfixed — it
  is shared by MP/stevie (a fix needs a full MP byte-compare), only bites a
  size-less byte-register slot store from inline asm, no other consumer.
- The §8k es_ss duplicate-local-name lesson did NOT bite (operands are distinct
  locals within each function).

### Verification
- triage small AND medium: PASS 73, 0 fails (minic + nasm buckets empty).
- get_dos_ticks block: `int 0x1A; mov [bp-30], dx; mov [bp-26], cx` (matches
  Watcom dx->ticks_low / cx->ticks_high); keyboard byte store `mov byte
  [bp-10], al`; operands all resolve to [bp-N] slots, correct sizes.
- qbe tree untouched (git status clean but for .claude/); make check green.
- Compile-only port (the §8k bar): no dos_tests build/run harness exists.

### git scope
- newlibc branch minic-dostest-asm-port 04ffa1e: 6 dos_tests/*.c forks +
  v9k_hardware.h; +192 lines, all-additive, __MINIC__-gated.  Committed LOCALLY,
  NOT pushed ([[feedback_newlibc_use_prs]] — PR, user's call).
- qbe master: this handoff + memory only.  NO compiler/qbe/emit/build/gate change.

### ⇒ Next session (consumer-driven, with the user)
- dos_tests porting track COMPLETE; the §6a triage minic bucket is now EMPTY.
- (1) block-scope / static-local char-array string init (bounded minic.y
  follow-on to §8v; no consumer);
- (2) file-scope function-pointer VARIABLE grammar (`void (*v)(void);` at file
  scope is a parse error — general hole, not Watcom; timer sidestepped it);
- (3) make dos_tests RUNNABLE — define v9k_hardware.h #pragma aux helpers under
  __MINIC__ (register-input asm) + a DOSBox harness (substantial, beyond §8k bar);
- (4) push newlibc ports via PR if/when wanted (minic-dostest-asm-port +
  the already-published minic-asm-port/minic-isr-entry-port);
- (5) deepen the capstone (cooked /dev/console; far-code interrupts.c model).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

# Next session (the §8u handoff DROVE the `all_upstream_bm` capstone on the UPSTREAM interrupt framework (`drivers/interrupts.c`) and left four consumer-driven follow-ups — and one of them, (3) publishing the merged newlibc work, has since landed: newlibc `main` now shows `08637c3 Merge pull request #22 from pauldevine/minic-isr-entry-port` and is IN SYNC with `origin/main` (the `minic-asm-port` + `minic-isr-entry-port` ports are published).  The user (AskUserQuestion) chose **the minic-PARSE bucket**.  §8v [2026-06-17, this session] **CLOSED the file-scope sized char-array string initializer — minic now parses `char NAME[N] = "string";` (e.g. `static char cwd[64] = "/";`), the form newlibc `tests/vshell.c` uses for its path buffers; the minic triage FAIL bucket dropped 7 → 6 at small AND medium (vshell now compiles END-TO-END); the fix is a frontend `minic.y` change → no emit audit; test-dos 386 → 391; conflicts UNCHANGED at 115 shift/reduce, 0 reduce/reduce; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  minic's file-scope declaration grammar had `'[' ']' '=' STR ';'` (the UNSIZED `char a[] = "x"` form) but NO `'[' expr ']' '=' STR ';'` sibling, so an EXPLICIT-dimension char array initialized from a string literal was a hard parse error.  (The brace forms `int a[3] = {1,2,3}` / `T a[N] = {…}` and the unsized string form `char a[] = "x"` all already parsed; ONLY sized + string-literal was missing.)  **THE FIX (frontend `minic.y`, additive, designed conflict-free):** a new `emit_string_array_sized(elemtyp, name, str_idx, count, static_local)` lays the literal's QBE data block bytes (incl. its NUL) at the front of the array and zero-fills the remaining `N*sizeof(T) - natural` bytes — reusing the literal's block `{ b "...", b 0 }` VERBATIM when the declared size fits exactly (`pad == 0`), and splicing `, z PAD` before the closing brace otherwise (verified byte-correct: `char a[8]="hi"` → `{ b "hi", b 0 , z 5 }` = 8 B; `char b[3]="hi"` → `{ b "hi", b 0 }` exact; `char c[64]="/"` → `… , z 62 }` = 64 B); a declared size SHORTER than the natural length (the exact-fit drop-NUL edge `char a[3]="abc"`, where C drops the NUL) dies clearly — no consumer, a documented bounded gap.  Wired to the new `'[' expr ']' '=' STR ';'` file-scope production placed next to the existing `'[' ']' '=' STR ';'`; its lookahead after `'[' expr ']' '='` (STR vs the `gaggr` `'{'` sibling) is DISTINCT, so the conflict count stays 115/0.  The `static` keyword at file scope is absorbed upstream (both `char a[N]="x"` and `static char a[N]="x"` flow through the same production — confirmed).  **SCOPE — file scope only (what vshell needs):** the BLOCK-scope string-array init is more broadly broken (even the unsized `void f(){ char a[] = "hi"; }` is a parse error there, and the dcls/statement-scope STATIC `'[' expr ']' '=' STR` variants are also missing), but vshell's six failing declarations were ALL file-scope statics, and fixing file scope makes vshell compile fully — so the block-scope and static-local sized-string variants are left as a documented bounded gap (no consumer yet; the natural follow-on if one appears).  **GATED `sized_str_array_probe` small+medium+compact+large+huge** (`minic/dos/examples/sized_str_array_probe.c` + `minic/dos/tests/sized_str_array_probe.golden.txt`, 5 entries in `tools/test-dos.sh`) — bug-loud: on the unfixed compiler the file does not parse (verified: a git-stashed-fix minic gives `parse error` on the probe, the fixed minic compiles clean), so the build fails outright; the probe exercises the headline vshell `static char cwd[64]="/"` form, a large zero-fill (`char greeting[16]="hi"`), exact fit (`char exactfit[3]="ab"`), an empty initializer (`char emptied[8]=""`), verifies the NUL + zero-filled slack, and rewrites the full N bytes to prove the declared size is real storage; the golden is contents/lengths/booleans → model-independent (verified byte-identical small/compact/huge).  **`vshell.c` now compiles END-TO-END small AND medium** (triage PASS 66 → 67 each); the 6 REMAINING parse-bucket TUs are all the Watcom-`_asm{}` `dos_tests` (`test_es_preservation`/`test_integration`/`test_keyboard_dos`/`test_memory_layout`/`test_serial_dos`/`test_timer_dos` — `_asm { mov ah,0 … }` brace-block inline asm with bare local-variable operands), a PORTING track (rewrite to minic's GNU `__asm__` Intel form / a `#if defined(__MINIC__)` fork, the §8k convention), NOT a bounded compiler feature.  **VERIFICATION:** minimal forms parse (`static char cwd[64]="/"`, `char a[8]="hi"`, `char z[1]=""`); too-long dies; data blocks byte-correct (above); conflicts 115/0 unchanged; `make check` green; MP compact body 689,760 byte-identical (MP has NO such decls — they were parse errors, so the new production never fires); full gate **391/391**.  Frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master `4c210a3` (`minic.y` helper + production, the new probe `.c` + golden, 5 `test-dos.sh` entries — NO compiler/qbe/emit, NO newlibc-tree change).  **⇒ Next session (consumer-driven, pick with the user):** (1) the remaining minic-PARSE bucket — the 6 Watcom-`_asm{}` `dos_tests` (a §8k-style gas/Watcom→minic inline-asm PORTING track in the newlibc tree, not a bounded compiler feature); (2) the BLOCK-scope / static-local sized-and-unsized string-array init gap (a bounded `minic.y` follow-on to §8v — but no consumer yet, since vshell only needed file scope); (3) deepen the capstone — wire `interrupts.c`'s `set_interrupt_vector` for additional vectors / a real IR1 serial-RX ISR, or add the cooked `/dev/console` path (needs a different harness shape — `bm_stdio` aliases collide with the upstream drivers); (4) a far-code minic model for `interrupts.c` (the `__MINIC__` `isr_entry` currently handles only the near/small-code path).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §8v session notes (2026-06-17)

### The pick
- §8u handoff: capstone on the upstream interrupt framework COMPLETE; four
  consumer-driven follow-ups.  At session start, confirmed follow-up (3) had
  already landed — newlibc main = 08637c3 (PR #22 merged), in sync with
  origin/main.  User (AskUserQuestion) chose **the minic-PARSE bucket**.
- Surveyed the bucket via build/newlibc-triage/sweep.sh: 7 TUs failed minic
  parse — vshell + 6 dos_tests.  vshell had NO _asm (a real grammar gap); the
  6 dos_tests are all Watcom `_asm { … }` blocks (a porting track).  Picked
  the vshell grammar gap (a clean, gateable compiler feature).

### The bug
- Bisected vshell's `error:334: parse error` to line 335:
  `static char cwd[64] = "/";` — a file-scope SIZED char array with a
  string-literal initializer.  Minimal repro matrix:
    - `char a[8] = "hi"`        -> PARSE ERROR  (the gap; static irrelevant)
    - `char a[]  = "hi"`        -> OK            (unsized string init)
    - `int  a[4] = {1,2,3,4}`   -> OK            (sized brace init)
  So ONLY sized + string-literal had no production.  Grammar: file-scope decl
  block had `'[' ']' '=' STR ';'` (minic.y ~8196) but no `'[' expr ']' '=' STR`.

### The fix (frontend minic.y, additive)
- New emit_string_array_sized(): natural = strlit_bytelen(idx) (incl NUL),
  total = count*SIZE(elem), pad = total-natural.  pad==0 -> reuse ini[idx]
  block verbatim; pad>0 -> splice `, z PAD` before the `}`; pad<0 -> die
  ("string initializer too long for array").  Routes static_local via
  emit_static_local, else adds a data global; var_set_arraybytes(name,total).
- New production `'[' expr ']' '=' STR ';'` next to the unsized STR rule,
  calling emit_string_array_sized(parsed_type, parsed_ident, $5->u.n,
  const_eval($2), 0).  Distinct lookahead vs the gaggr sibling -> conflicts
  stay 115/0.
- Data blocks verified byte-correct: char a[8]="hi" -> { b "hi", b 0 , z 5 };
  char b[3]="hi" -> { b "hi", b 0 } (exact); char c[64]="/" -> … z 62.

### Scope
- File scope only — what vshell needs (all 6 vshell errors were file-scope
  statics; the fix makes vshell compile end-to-end small AND medium).  Block
  scope is more broadly broken (even unsized `char a[]="hi"` fails in a fn
  body; dcls/stmt STATIC sized-STR also missing) — left as a documented
  bounded gap, no consumer yet.

### Gate
- sized_str_array_probe small+medium+compact+large+huge, one golden.
  Bug-loud: a git-stashed-fix minic gives parse error on the probe; the fixed
  one compiles clean.  Exercises cwd[64]="/", greeting[16]="hi" (big
  zero-fill), exactfit[3]="ab", emptied[8]="", NUL+slack zero checks, full-N
  rewrite.  Golden = contents/lengths/booleans, model-identical (verified
  small/compact/huge).

### Verification
- triage minic bucket 7 -> 6 (vshell PASS small + medium); remaining 6 = the
  Watcom-_asm{} dos_tests (porting track).
- conflicts 115/0 unchanged; make check green; MP compact body 689,760
  byte-identical (MP has no such decls); full gate 391/391.
- Frontend-only (minic.y) -> no emit audit, no Victor run.

### git scope (qbe)
- master 4c210a3: minic.y (emit_string_array_sized + the production), new
  sized_str_array_probe.c + golden, 5 test-dos.sh entries.  No emit/qbe, no
  newlibc-tree change.

### ⇒ Next session (consumer-driven, with the user)
- (1) the 6 Watcom-_asm{} dos_tests (a §8k-style inline-asm PORTING track in
  the newlibc tree, not a compiler feature);
- (2) block-scope / static-local sized-and-unsized string-array init (a bounded
  minic.y follow-on to §8v — no consumer yet);
- (3) deepen the capstone (interrupts.c set_interrupt_vector for more vectors /
  an IR1 serial ISR; or the cooked /dev/console path — needs a different
  harness shape, bm_stdio aliases collide);
- (4) a far-code minic model for interrupts.c (isr_entry currently near/small
  only).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

Older session headers (§8u and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
