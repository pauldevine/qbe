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

Older session headers (§8v and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
