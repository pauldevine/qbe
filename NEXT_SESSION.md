# Next session (§3r — Stevie found real MiniC/QBE correctness bugs; STOP working around them in Stevie and make compiler regression probes first)

## Context
We ported `stevie-orig` far enough to run on the Victor 9000 under unaltered MS-DOS 3.1:
- Victor console/keyboard/display path works without IBM BIOS interrupts.
- CR stripping on read and CRLF writeback now work.
- Insert mode redraw is performant.
- Normal-mode arrows, `ESC`, `:q`/`:x`, `x`, `X`, and `3x` are working.
- `build/stevie-orig/stevie.exe` currently builds and launches.

The current editor bug is **not** a Victor hardware issue:
- `w` by itself moves word-to-word correctly.
- `dw`, `de`, and `yw` operate from the cursor position through EOF.
- Earlier versions reported `yank too big for buffer`; after a direct-copy yank workaround, the same bad range became visible as "to EOF".

This means the word motion endpoint is correct in isolation, but the operator/range path corrupts or misinterprets the `LPTR` range when a pending operator is involved.

## Primary goal
Use Stevie as the integration test, but fix the compiler/backend/runtime bugs underneath it.  Do **not** keep broadening Stevie source workarounds unless they are platform porting fixes.  Start by creating tiny bug-loud probes, then fix MiniC/QBE/runtime and remove or reduce the Stevie `LPCOPY` workaround layer.

## Highest-priority suspected compiler bugs
1. **Struct assignment copies only the first word.**
   Evidence: `struct lptr { LINE *linep; int index; }` assignments like `top = startop`, `*Curschar = *pos`, and `tmp = *a` behaved as if only `linep` was copied and `index` was dropped.  `edit.c` already had a comment/workaround saying `*X = *Filemem` only copies the `linep` pointer.  The temporary `LPCOPY()` field-by-field workaround made `x` and `3x` work but should not be necessary.
2. **Pointer-to-struct assignment and temp-swap paths.**
   `pswap(a,b)` originally used:
   ```c
   tmp = *a;
   *a = *b;
   *b = tmp;
   ```
   This is exactly the kind of operation Stevie's operator ranges depend on.  Probe both local struct assignment and `*dst = *src`.
3. **Pending-operator range handling with small structs.**
   Since `w` alone works but `dw`/`de`/`yw` span to EOF, build a minimal C probe that saves an `LPTR`-like start position, mutates a current position through a helper, then yanks/copies the range.  This can be independent of Stevie and should expose whether the corruption happens in struct copy, static-return pointers, comparison, or function-call ABI.
4. **Far-call/runtime ctype was one confirmed non-QBE bug.**
   `3x` only worked after `_isdigit`/`_isalpha`/`_isspace` in `minic/dos/libstub.asm` were implemented.  Keep that fix, but separate runtime-stub bugs from QBE codegen bugs in the notes/tests.

## Suggested probes
Add focused DOS probes under `minic/dos/examples/` plus golden output under `minic/dos/tests/`, then wire them into `tools/test-dos.sh`.

1. `struct_copy_probe.c`
   - `struct P { void *p; int i; };`
   - Test `b = a`, `*dst = *src`, return/copy through a helper, and swap through a temp.
   - Golden should prove both pointer and integer fields survive.

2. `lptr_range_probe.c`
   - Model `LINE { next, prev, char *s, unsigned long num }` and `LPTR { LINE *linep; int index }`.
   - Save `startop`, move `cur` from word start to next word, then copy/yank only `[startop, cur]`.
   - Golden should be one word, not the rest of the file.

3. `static_lptr_return_probe.c`
   - Helper returns `static struct P *`.
   - Caller copies returned struct into a global/current struct and checks both fields.
   - This mirrors `fwd_word()`/`end_word()` returning static `LPTR *`.

4. `operator_pending_probe.c`
   - Simulate `normal('d')` saving `startop`, then `normal('w')` using an already-pending operator and finishing the operation.
   - Golden should confirm the saved start and moved end positions are distinct and bounded.

## 2026-06-06 Codex continuation notes
- Added and wired the first regression probes:
  - `struct_copy_probe.c`
  - `static_lptr_return_probe.c`
  - `lptr_range_probe.c`
  - `operator_pending_probe.c`
- Result: the simple suspected `LPTR` struct-copy failures do **not** reproduce in isolation.  Under `medium`, local aggregate assignment, `*dst = *src`, struct-return copy, temp-swap, static-return-pointer copy, pending `d` + `w`, and the original iterator-style yank loop all preserve both fields and stay bounded.
- Also checked the richer `operator_pending_probe` under `compact`; it stays bounded there too.
- A transient `compact` failure in the first `lptr_range_probe` version came from a static `struct LINE { char *s; ... }` initializer with a string pointer, not LPTR range logic.  The probe now initializes that field at runtime to avoid the known far static-data pointer relocation family.
- Superseded implication at this point in the investigation: the `dw`/`de`/`yw` through-EOF Stevie bug needed a closer integration reproducer (real Stevie command path, input/stuff buffer, or multi-TU/runtime behavior).  Do not assume "struct assignment copies only the first word" until a bug-loud probe shows it.

## 2026-06-06 Codex continuation notes (root cause found)
- Root cause: `tools/omf_link.py` handled NASM target-frame 16-bit offset fixups into grouped near data as if the target segment itself were the frame.  Generated i8086 code uses DS for ordinary near `_DATA`/`_BSS` references, and DS is DGROUP, so `_BSS` symbols were patched with BSS-relative offsets instead of DGROUP-relative offsets.
- Why the earlier probes missed it: they were single-TU and too small.  Stevie's failing path saves `startop` in one object and reads it from others, with enough DATA/BSS layout for the wrong BSS-relative offset to alias live initialized data.  The symptom looked like an LPTR/struct-copy bug because `startop.index` and `startop.linep` were read from the wrong DGROUP address.
- Fix: `_frame_para()` now uses the containing output group paragraph for target-frame `loc=1`/`loc=5` offset fixups whose target output segment is a DGROUP member, leaving segment/far-pointer fixups unchanged.
- Regression: `grouped_bss_probe.c` + `grouped_bss_def.c` build as two translation units.  The BSS object is padded so the old BSS-relative offset lands inside `data_guard`; the golden requires the BSS writes to work and the initialized data guard to remain unmodified.
- Victor/MAME confirmation: a guarded Stevie self-test of `y` then `w` on `alpha beta gamma` now finishes with `same_line=1`, `start_index=0`, and `op=0`.  Do not run this Victor-targeted Stevie under DOSBox.
- IBM DOSBox confirmation: the apparent DOSBox crash was from launching the macOS DOSBox app inside the Codex sandbox; crash reports show `EXC_CRASH (SIGABRT)` in `HIServices`/`AppKit` during `+[NSApplication sharedApplication]`, before the DOS payload runs.  User-captured log: `/Users/pauldevine/Desktop/dosbox_crash_6_jun_2026.txt`.  Running the DOS gate outside the sandbox completes cleanly: `tools/test-dos.sh` reports `208/208 ok`, including `grouped_bss_probe`.
- Manual Stevie discrepancy remains: user re-tested in MAME and still saw manual `dw` delete from cursor to EOF.  Current rebuilt `build/stevie-orig/stevie.exe` (146480 bytes) does **not** reproduce that under scripted Victor input:
  - `ialpha beta gamma\rsecond line\rthird line\e1G0dw:q!\r` leaves first line as `beta gamma`.
  - `ialpha beta gamma\rsecond line\rthird line\e1Gwdw:q!\r` leaves first line as `alpha gamma`.
  These use `build/repl-victor.sh` to run production Stevie through the real edit loop with redirected input on Victor/MAME.  If manual still fails, verify the exact EXE copied into the image and the exact file/cursor state; the direct `normal()`, stuffed `vgetc()` path, file-loaded self-test, and redirected-input edit loop all stayed bounded.

## Stevie state to preserve
Keep the Victor-specific platform changes:
- `VICTOR9000` terminal/key handling in `dos.c`, `term.h`, `env.h`.
- INT 21h console I/O and Z-19/Victor escape sequences.
- CR stripping on read and CRLF on write.
- Row-only redraw/performance fix.
- DOS display restoration on exit.
- Direct `x` implementation is acceptable as an editor behavior simplification, but revisit once compiler struct assignment is fixed.

Treat the broad `LPCOPY()` edits as provisional.  Once struct assignment/codegen is fixed, retest Stevie and decide whether to revert those source-level workarounds or leave only the minimal ones needed for old-compiler compatibility.

## Rebuild / current artifact
- Current build command: `tools/build-stevie.sh --exe`
- Current artifact: `build/stevie-orig/stevie.exe`
- Current marker: `STEVIE - Version 3.69b V9K-20260606-xfix`
- Current known failure: `dw`, `de`, `yw` affect through EOF even though `w` alone moves correctly.

<details><summary>§3q (prior) — software float on no-8087 target</summary>

# Next session (§3q DONE — WIRED single-precision software float (`Ks`) end-to-end on the no-8087 target.  Native C `float` arithmetic / comparison / int↔float conversion now lower to `call far _sf_*` helpers (the host-validated `softfloat.c`, moved in-tree to `minic/dos/softfloat.c` and compiled-through-minic into a linked `softfloat.obj` via `build-example.sh --softfloat`).  New `softfloat_probe.c` runs through minic→qbe→DOSBox and its add/sub/mul/div/compare/convert results **byte-match the host hardware-float oracle**.  `make check` green; DOS gate **+1** (`softfloat_probe`, medium).  **MEDIUM-ONLY** — far-data float and float-literal-as-double are deferred follow-ups (see below).  COMMITTED.

## What landed (§3q)
- **Helper shipped:** `build/sf-spike/softfloat.c` → `minic/dos/softfloat.c` (tracked).  `tools/build-example.sh` gained a `--softfloat` flag; stages 1-4 were refactored into a `compile_unit()` shell function so the helper goes through the identical minic→asm_to_omf→nasm pipeline, and `softfloat.obj` is added to the `omf_link` line.  Helpers: `_sf_add/_sf_sub/_sf_mul/_sf_div/_sf_from_int/_sf_to_int` (DX:AX result), `_sf_cmp` (-1/0/1, or 2 for NaN, in AX).
- **`i8086/emit.c`:** the DEAD 8087 op-table rows AND the big `fld/faddp/fcompp` block are GONE.  Replaced by: `Ks` arith → `emit_sf_binop` (`call far _sf_{add,sub,mul,div}`, save/push/call/`add sp`/store-DX:AX-to-slot/restore — mirrors the `_qbe_div32*` sequence); `Oneg Ks` → inline `xor hi,0x8000`; float comparisons (detected by op-range `Ocmps..Ocmps1`, since their result cls is `Kw`) → `call far _sf_cmp` + a per-op map of the -1/0/1/2 result to a boolean; `Oswtof/Ouwtof/Osltof` → `_sf_from_int` (sign/zero-extend the int into DX:AX first); `Ostosi/Ostoui` → `_sf_to_int` (low word → Kw dest); **all double (`Kd`) ops `die()` loudly**.  `Ks` load/copy/store — and the load-forwarded union-pun `cast` (Ks↔Kl, both 32-bit on i8086) — REUSE the existing `Kl` 32-bit move handlers (the Kl-block guard now admits `Ks` for Oload/Ocopy, `Ostores` shares the `Ostorel` case, and a new `case Ocast` shares `Ocopy`).
- **`i8086/abi.c`:** `selret` handles `Jrets` exactly like `Jretl` (32-bit value → DX:AX via Ofarseg/Ofaroff); `selcall` captures a `Ks` call return (KBASE(Ks)==1 so it's admitted explicitly alongside the integer path).
- **`spill.c`:** `Ks` is treated like `Kl` by the i8086 `force_kl_slot` machinery — slot-resident, given a **4-byte (2-word) slot** (the line-139 `KWIDE` test gained an i8086 `Ks` clause), and **evicted from every live set**.  This is MANDATORY: i8086 has `NFPR==0`, so any `Ks` temp left needing an FP register makes rega die "no more regs".
- **`i8086/isel.c`:** float comparisons are routed to `selfp` (emit does the lowering) instead of `selcmp`, which previously die'd "unsupported comparison 14".
- Probe `minic/dos/examples/softfloat_probe.c` + golden `minic/dos/tests/softfloat_probe.golden.txt`; wired into `tools/test-dos.sh` (medium).  minic grammar unchanged (115 s/r — action-only edits; the long-quoted "111" was stale, HEAD reports 115 too).

## Deferred follow-ups (clean, self-contained)
1. **Far-data single-precision float (compact/large/huge).**  minic's `loadfar`/`storefar` (`minic/minic.y`) have no `'s'` (Ks) case, so a `float` through a far pointer truncates via `loadfw`/`storefw` (16-bit) and `ret`-ing it mistypes a `=w` value from a `=s` function (qbe parse error).  The fix needs `Ks` to ride the 32-bit far path (`loadfl`/`storefl`), but those ops' type masks in `ops.h` forbid `Ks`, and a store instruction's cls is `Kw` (so `storefl` can't accept a `Ks` value without a new op or a Ks→Kl bitcast).  This is the `[[storefar-lacks-storefl]]` family extended to `Ks` — its own focused task.  (A spike that added the `'s'` minic cases + widened the masks got as far as a "no first operand expected in storefl" parse error and was reverted; the mask/usecheck/store-cls interaction is the crux.)
2. **minic float-literal + unary-minus typing.**  minic types a float literal `1.5f` as `double` (`=d copy d_1.5` + `exts`/`truncd`) and `-x` as `0.0 - x` in double, so any literal-mixed or negated float expression silently becomes `Kd` (→ the new `die()` / "no more regs").  The probe avoids both (operands built from 32-bit bit patterns via a `union`; negation via subtraction).  Fixing `1.5f`→`Ks` typing is the natural next step to make `float` actually ergonomic.
3. **Wire float into MicroPython** (`MICROPY_FLOAT_IMPL_FLOAT`) once (1)+(2) land — the original consumer (true division `/`, etc.).

## Verify
- `tools/build-example.sh --softfloat --model=medium minic/dos/examples/softfloat_probe.c` → `tools/run-dos-exe.sh build/examples/softfloat_probe/softfloat_probe.exe` diffs clean against the golden (and the host oracle `cc /tmp/sfgold.c` style).
- `make check` green; `tools/test-dos.sh` green.
- Inspect `build/examples/softfloat_probe/softfloat_probe.asm`: arithmetic = `call far _sf_*`, neg = `xor ..., 0x8000`, **no `fld`/`faddp`/`fild` anywhere**.)

</details>

<details><summary>§3p (prior) — fixed the dense-`Kl` miscompiles that blocked soft-float</summary>

# (§3p DONE — fixed the dense-`Kl` (32-bit) miscompiles that blocked software float on the no-8087 Victor.  The soft-float spike's "it's a rega/spill bug under pressure" diagnosis was WRONG — it was THREE independent, ordinary bugs (one minic frontend, one GCM×i8086 interaction, one i8086 emit), each found by tracing the wrong VALUE rather than trusting the pressure-shifts-symptom hunch.  All three soft-float probes (`build/sf-spike/sf_const_probe.c`, `sf_dos_probe.c`) now byte-match the host oracle; every `build/sf-spike/min/*` repro is correct; `make check` green; DOS gate **199→202** (new `kl_ternary_mul_probe` × medium/compact/large).  COMMITTED.)

## What was actually wrong (3 bugs — none was rega-under-pressure)

1. **minic ternary type truncation** (`minic/minic.y` `case '?'`).  `cond ? A : B` type-unification only recognised the EXACT signed `INT`/`LNG` pair, so a `0 : (unsigned long)x` arm pair (`unsigned long` = `LNG|UNSIGNED`, which is `!= LNG`) defaulted to the narrow `INT` arm and emitted a `=w` phi — TRUNCATING the 32-bit arm to its low word.  This is the real cause of `sf_add(2,2)→0`: `ma=(ea==0)?0:(FRAC|IMPLICIT)` (0x800000) truncated to 0x0000, then the `ma==0&&mb==0` early-out returned 0.  Fix: pick the WIDER arm's IL class via `irtyp_ret`; widen the narrow arm in its trailer block (`extsw`/`extuw` per the arm's own signedness, SSA-dominated there).  Pure action code, **0 grammar change** (still 111 s/r 0 r/r).

2. **GCM `sink()` × i8086 `force_kl_slot`** (`gcm.c::cheap()`).  GCM sinks "cheap" ops (incl. `shr`) to their point of use to cut register pressure.  On i8086, spill.c's `force_kl_slot` then evicts the sunk Kl result to a slot — so the def `Sxx = shr` lands IN the use-block while spill treats the value's register incarnation as **live-in** and inserts the reload (`R1 = copy Sxx`) in the PREDECESSOR edges, reading the slot BEFORE the in-block def writes it.  Hit `sf_to_int`'s `sign ? -v : v` (sign read garbage → sign-flip: `to_int(3.0)→-3`) and the `sf_cmp` results — both ONLY under caller pressure (so `cmptest` was right, `three.c`/`sf_const_probe` wrong, exactly the "caller-save" symptom).  Fix: don't sink Kl ops on i8086 (sinking is a pure optimization; gated `i->cls==Kl && T.name=="i8086"`).

3. **i8086 Kl multiply was a 16×16 `imul`** (`i8086/emit.c` `Omul` Kl).  It loaded only the LOW words of the two operands and did ONE signed `imul`, ignoring high words.  Correct for sign-extended operands (the low word's sign extension matches), but WRONG for zero-extended (`extuw`) operands whose low word has bit 15 set — `imul 0xCCCD` sign-extended to a negative high word (`0xCCCD*0xA0 → 0xFFE00020` instead of `0x00800020`).  `sf_mul`'s 24×24 partials are all `extuw`, so `0.1*10` came out `0x3f7ec000` (~0.9956) not `0x3f800000`.  Fix: a proper 32×32→32 multiply via three 16×16 UNSIGNED `mul` partials (`result = a_lo*b_lo + ((a_lo*b_hi + a_hi*b_lo)<<16)`) — the low 32 bits of signed and unsigned products are identical, so unsigned partials over the full operand words are correct for BOTH signednesses (guarded by the `sp = -3*5 → -15` case in the probe).  Uses CX (+BX for a const multiplier) as scratch, bracketed like the other Kl handlers.

## Files / verification
- `minic/minic.y`, `gcm.c`, `i8086/emit.c` (+ `klmul_movax`/`klmul_byword` helpers), `parse.c` (the kept §3p far-ret debug-printer fix), `tools/test-dos.sh`, `minic/dos/examples/kl_ternary_mul_probe.c` + `minic/dos/tests/kl_ternary_mul_probe.golden.txt`.
- `make check` green; DOS gate **202/202**; soft-float `sf_const_probe`/`sf_dos_probe` byte-match host; all `min/*` repros correct.
- The general method that cracked it: dump post-rega IR (`qbe -dR`, now usable on i8086 thanks to the parse.c fix) and trace the WRONG VALUE to its def site, instead of trusting "symptom shifts with pressure ⇒ rega."  Two of three bugs were visible directly in the `.ssa`/`-dR` dump (the `=w phi`, the predecessor `R1 = copy Sxx`); the third by instrumenting the real function (printf inside `sf_mul`).

## KNOWN pre-existing blocker found (NOT this session's regression, NOT fixed)
`tools/build-stevie.sh --exe` FAILS at `hexchars.c` — minic rejects the flat (unbraced) array-of-structs initializer `struct charinfo chars[] = { 1, 0, 2, "^A", … };` ("struct/union initializer must be braced").  Reproduced with this session's `minic.y` reverted, so it predates §3p.  Consequence: the DOS gate's "stevie.exe size" check passes on a STALE cached `build/stevie-orig/stevie.exe`, and stevie could NOT be rebuilt to re-verify render against the §3p codegen changes (the plan's stevie step).  The 202-probe gate + `make check` are the regression coverage instead.  Fixing the flat array-of-struct init (so stevie rebuilds) is a clean, self-contained minic frontend task for a future session.

## NEXT candidates
- **Resume float bring-up** (the spike's whole point is now unblocked): drop the host-validated `build/sf-spike/softfloat.c` in as a libstub-style helper and rewire `i8086/emit.c`'s DEAD 8087 `Ks`/`Kd` table (`faddp`/`fsubp`/`fmulp`/`fdivp`/`fcompp`/conversions — unused on the no-8087 Victor) to `call far` soft-float helpers (cdecl, args pushed, result DX:AX — the `_qbe_div32*` precedent at `i8086/emit.c` ~line 2512 + `minic/dos/libstub.asm`).
- **Fix the `chars[]` flat array-of-struct init** so stevie rebuilds and its render can be re-verified.
- The long-deferred 211-commit upstream-qbe rebase.

## Artifacts / references
`build/sf-spike/SPIKE_FINDINGS.md` (the spike writeup — note its rega diagnosis was wrong), `build/sf-spike/min/*` (repros), `build/sf-spike/{softfloat.c,sf_host_test.c}` (proven algorithm + host oracle).  Memory: [[softfloat-spike]] (update: not a rega bug), [[i8086-kl-add-sub-mul-r1-alias]].

</details>

<details><summary>§3p (prior) — original plan: "fix the rega/spill bug" (diagnosis turned out wrong; kept for context)</summary>

# Next session (§3p — FIX THE i8086 REGISTER-ALLOCATION / SPILL BUG that blocks software floating point on the no-8087 Victor.  This session is the FIX; the diagnosis is DONE (soft-float spike, this session) — see `build/sf-spike/SPIKE_FINDINGS.md` + [[softfloat-spike]].)

## Goal
Fix the i8086 backend's register-allocation/spill correctness bug that corrupts **dense 32-bit (`Kl`) computation under high register pressure**.  It gates bringing up single-precision *software* float (the Victor has no 8087), which is the first heavy `Kl` consumer.  It is almost certainly the same defect family as the long-open `[[qbe-rega-phi-slot-leak]]` (the stevie `filetonext` mis-render) and `[[qbe-caller-save-bug]]` / `[[qbe-rega-avoid-mask-ignored]]` — so a correct fix likely closes more than just float.

## The bug (deterministic, minimal, no-DOSBox repros in `build/sf-spike/min/`)
- `addonly.c`: `sf_add(2.0f, 2.0f)` → `0x00000000` (should be `0x40800000`).
- `addexp.c` (returns `exp`): `exp` computes to **0** (should be 129).
- `addslim.c` removes one branch → the corruption MOVES (`exp` 0 → 128).  [This "symptom shifts with pressure" was read as a rega smoking gun; it was actually two DIFFERENT ternary-truncation sites.]
- `cmp.c`/`cmptest.c`: `sf_cmp` correct standalone but WRONG (`three.c`) under more caller code.  [Actually the GCM-sink×Kl-slot bug, which only fires when a `shr` cond gets sunk under enough pressure.]

## Tooling (this session)
- `parse.c` debug printer now handles the i8086 far-return jump types (`Jretfw`/`Jretfl`/`Jretf0`) so `qbe -dR`/`-dS` no longer assert — this is what made the bugs traceable.
- Post-rega IR: `./qbe -t i8086 -m medium -d R build/sf-spike/min/addonly.ssa`.
- Fast asm loop: `build/sf-spike/min/run.sh <src.c> [model]`.
- Host oracle: `cc -DSF_HOST -O2 -o /tmp/t build/sf-spike/sf_host_test.c && /tmp/t`.

</details>

<details><summary>§3o (prior) — interactive line editor for the dos8086 MicroPython REPL</summary>

# Next session (§3o — INTERACTIVE LINE EDITOR for the dos8086 MicroPython REPL.  §3n made the REPL interactive but interactive input relied on DOS CON's cooked line discipline (handle-0 read via INT 21h AH=3Fh): backspace worked, but there was NO command history and NO arrow-key cursor movement.  §3o adds a DOS-native line editor — insert-at-cursor, Backspace, forward Delete, Left/Right/Home/End cursor movement, and Up/Down command history — for the interactive console, while KEEPING the redirected (`< file`) path byte-identical so the test harness still drives the REPL.  NO qbe-core / minic / i8086-backend change — pure MicroPython port-tree work (UNTRACKED, like all of `ports/dos8086/`, same pattern as §3n).  Files: `ports/dos8086/mphalport.c` (+ `.h`), `ports/dos8086/main.c`.  THE MODE SWITCH (mandatory, the §3n note's central concern): a raw keyboard read reads CON regardless of `< file` redirection, so the editor would BLOCK FOREVER on a redirected handle.  So `repl()` decides ONCE at entry via `g_repl_interactive = !mp_dos_stdin_is_redirected()`, which calls new HAL `mp_dos_stdin_devinfo(int *out)` — INT 21h **AH=44h AL=00h** (IOCTL get-device-info) on handle 0, returning the device-info word in DX; **bit 7 (0x80) set ⇒ character device (interactive CON), clear ⇒ disk file (redirected)**.  On CF (error) the asm stores 0 → bit 7 clear → "redirected" = the SAFE default (keeps the testable AH=3Fh byte path rather than blocking on the keyboard).  THE RAW KEY READ (mphalport.c, new `mp_dos_getch_raw(int *out)`): INT 21h **AH=07h** — Direct Console Input, no echo, and CRUCIALLY **NOT** AH=08h: 07h is *unchecked* so **Ctrl-C is delivered as a plain 0x03 byte** instead of triggering INT 23h program termination (the editor catches Ctrl-C itself).  An IBM-PC extended key arrives as the DOS two-read sequence (a 0x00 lead byte then the BIOS scancode); the asm does the second AH=07h read inline and **encodes the result into a 2-byte word: a normal key as its 0x01–0xFF value (high byte 0), an extended key as `0x0100 | scancode`** (AH=1).  **CRITICAL — the VICTOR keyboard is NOT IBM-PC**: per the Victor MS-DOS 3.1 BIOS source (`KEYS.ASM` `multitable` / `CI.PLM`, traced by the victor9000-engineer agent), arrow keys are a Z19/VT-52-family **3-byte sequence `ESC (0x1B) + ('x'=0x78 | 'y'=0x79) + ('A'=Up / 'B'=Down / 'C'=Right)`** (NOT 0x00+scancode, NOT plain 2-byte VT-52), forward-Delete is a lone **`0x7F`**, Backspace is `0x08`.  So the 0x00+scancode path is dead on the Victor (kept only for medium-model runs on a real PC) and the editor decodes ESC sequences instead (see `repl_decode_esc` below).  Written through the caller's out-pointer (the fragile-asm-return discipline shared with `mp_dos_getchar`/`mp_dos_load_file`: minic surfaces neither the asm result nor its clobbers, so we read args at fixed `[bp+N]` and push/pop every reg we touch).  Both HAL fns have near + `#if DOS_FAR_DATA` branches (far repoints DS at the out-pointer's segment for the word store, matching the existing console-HAL style).  THE EDITOR (main.c, `repl_edit_line(buf,maxlen,add_hist)`, all under `#if MICROPY_ENABLE_COMPILER`): maintains `len`/`pos`; printable insert reprints the tail buf[pos..] then backs the cursor up; Backspace/Delete shift-and-redraw via `repl_redraw_tail` (reprint tail + a space to wipe the vacated column + backspaces); Left/Right emit `\b`/the char; Home/End walk to col 0 / end; Up/Down browse an 8-deep history ring (`repl_replace_line` parks the cursor, blanks the old text, prints the recalled line; the live line is stashed in `repl_edit_saved` on the first Up).  Cursor/history keys arrive in THREE forms — IBM 0x00+scancode (a real PC), the Victor/VT-52/ANSI ESC sequence (`repl_decode_esc`), and the lone 0x7F Delete — all normalized to shared `ACT_*` codes dispatched by ONE handler.  `repl_decode_esc` (entered on a 0x1B) peeks with new HAL `mp_dos_kbhit` (INT 21h **AH=0Bh** input-status) BEFORE each continuation read so a bare ESC never blocks — the Victor queues the whole `ESC x A` sequence atomically, so its follow-up byte is already waiting; it then reads the selector ('x'/'y'/'['/'O') and the direction letter via AH=07h and maps A=Up/B=Down/C=Right/D=Left/H=Home.  Returns line length (≥0), **-1 on EOF** (Ctrl-Z 0x1A / Ctrl-D 0x04 at an empty line) or **-2 on Ctrl-C** (cancel); `repl()` re-prompts on -2 from the first line and ABANDONS the whole block on -2 mid-continuation.  `repl_readline(buf,maxlen,add_hist)` dispatches: interactive → `repl_edit_line`, redirected → the UNCHANGED `repl_readline_redirected` (the old AH=3Fh byte loop, renamed).  **HISTORY RING IS A FLAT 1D ARRAY** `repl_hist[REPL_HIST_N*REPL_BUF_MAX]` indexed with an explicit stride via `repl_hist_slot(i)` — deliberately NOT a `[N][M]` 2D array, because the curated py/*.c core never exercises minic's 2D-array stride codegen, so this stays on the simple proven pointer arithmetic; under far-data these statics land in main's own far segment, not DGROUP.  BUILD: `tools/build-micropython.sh --model=compact` → 106/106 TUs, 0 fail; image **814064 → 824928 B** (+11 KB: editor code + ESC decoder + the ~4.6 KB history ring + the three HAL fns), ~71 KB under the ~896 KB Victor ceiling.  **TESTING — the redirected path is the REGRESSION GUARD; the interactive editor needs a HUMAN on real Victor:** `build/repl-victor.sh build/mp-link/mpython.exe build/repl-input.txt 240` (drives `prog < input.txt`) reproduces §3n BYTE-FOR-BYTE — `>>> 3` / `25` / `[0, 1, 4, 9]` / `... ...` + `107` (multi-line def continuation) / `ZeroDivisionError: divide by zero` / `42` (auto-print AFTER the exception → recovery) / clean `C5` — confirming the IOCTL mode switch correctly picked the AH=3Fh path and the redirected reader is undisturbed.  The interactive editor itself **cannot be auto-tested** (arrow keys can't be driven through `< file`); it requires a human typing on a real Victor.  **ON-TARGET STATUS — FULLY USER-CONFIRMED on a real Victor:** basic REPL, printable insert, Up/Down history, **Left/Right cursor, and Backspace all work**.  The FIRST cut wrongly assumed IBM 0x00+scancode arrows (user reported arrows dead) → corrected to the Victor `ESC x/y A/B/C` decode; the user then confirmed **Left arrow is a DISTINCT key from Backspace** — Left does a non-destructive cursor move (its own `ESC…D` sequence), Backspace (0x08) does a destructive delete-left, so the BIOS-source worry that Left might collapse to 0x08 is resolved (it does not).  **The Victor keyboard has NO Home/End/Delete keys** (it is not an IBM layout), so those `ACT_*` mappings are dead on the Victor — kept only for the IBM-PC medium-model and ANSI-terminal paths (zero cost).  **Backspace (0x08), Ctrl-C (0x03), Enter (0x0D), and printable insertion are plain bytes and work regardless.**  §3o is COMPLETE and on-target validated.  `make check` unaffected (no core change); minic 111 s/r 0 r/r unchanged (no minic change — minic was rebuilt fresh per [[minic-make-staleness]] for hygiene only); **DOS gate UNCHANGED at 199** — the REPL is NOT DOSBox-gateable (825 KB image exceeds DOS's 640 KB conventional-memory limit; see [[victor9000-target]]), so it stays Victor-validated, no `test-dos.sh` entry.  NEXT candidates: enabling float (true division `/`) / longint / a `sys` module surface; or the long-deferred 211-commit upstream-qbe rebase (pure plumbing).  Reproducer: `build/repl-victor.sh build/mp-link/mpython.exe build/repl-input.txt 240`.  See [[mp-interactive-repl]].)

</details>

<details><summary>§3n (prior) — interactive REPL (cooked CON input)</summary>

# Next session (§3n — INTERACTIVE REPL for the dos8086 MicroPython port.  The MicroPython language + builtin surface was already COMPLETE + Victor-validated (§3d/§3e/§3g/§3i); §3n makes the port actually INTERACTIVE instead of "read PROG.PY once and exit".  NO qbe-core / minic / i8086-backend change — pure MicroPython port-tree work (UNTRACKED, like all of `ports/dos8086/`, per the established pattern) reusing the existing `py/repl.c` continuation helper.  Files: `ports/dos8086/mphalport.c` (+ `.h`), `ports/dos8086/main.c`.  THE STDIN WIRING (mphalport.c): new `mp_dos_getchar(char *out)` reads ONE byte from DOS **handle 0** via INT 21h **AH=3Fh** (EOF / read-error → stores `0x1A`); far-data + near-data `#if DOS_FAR_DATA` branches following the same void-with-an-out-pointer asm discipline as `mp_dos_load_file` (returning a value from a pure-asm body is fragile under minic — minic doesn't surface the asm result or clobbers, so we push/pop every reg we touch and write the byte through the caller's pointer).  `mp_hal_stdin_rx_chr()` now calls it and returns `(unsigned char)c`.  **CRITICAL CHOICE: read handle 0 via AH=3Fh, NOT the keyboard via AH=08h** — handle-0 reads honor DOS `< file` redirection, which is the ONLY way to drive the REPL non-interactively for testing (see TESTING).  THE REPL (main.c, all under `#if MICROPY_ENABLE_COMPILER`): `repl_readline(buf,max)` reads a line via `mp_hal_stdin_rx_chr` (strip `\r`, break on `\n`, `0x1A`/EOF with no pending chars → return -1); `repl_exec(src)` runs one (possibly multi-line) input under ONE `nlr_push` — `mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_,…)` → `mp_parse(…, MP_PARSE_FILE_INPUT)` → `mp_compile(…, /*is_repl=*/true)` → `mp_call_function_0` — and on the nlr-else branch prints `mp_obj_print_exception` and RETURNS (so SyntaxError from mp_parse AND any runtime exception are printed but the REPL CONTINUES, never exits); `repl()` prints a banner + `>>> `, reads a line, then `while (mp_repl_continue_with_input(buf))` prints `... ` and appends `'\n'`+next line until the block completes — a blank continuation line leaves buf ending in `'\n'`, which makes `mp_repl_continue_with_input` (py/repl.c — already in the curated build glob, pulled in + surviving `--gc-sections` once it's CALLED) return false.  `is_repl=true` makes a bare expression statement auto-print its repr via the `__repl_print__` builtin path (compile.c:2009-2011; `mp_builtin___repl_print__` is UNCONDITIONALLY defined and `MP_QSTR___repl_print__` is ALREADY in the genhdr qstr pool, so NO qstr regen was needed — unlike the §3g(B) min/max/enumerate flags).  WIRING (main): `main` enters `repl()` ONLY when `PROG.PY` is ABSENT; a present `PROG.PY` keeps the EXISTING `do_str` script path UNCHANGED, so every existing Victor probe/golden that ships a `PROG.PY` is undisturbed (still runs the script, prints `C5`, exits — no REPL).  BUILD: `tools/build-micropython.sh --model=compact` → 106/106 TUs, 0 fail; image **802128 → 814064 B** (+12 KB: the REPL code + py/repl.c), well under the ~896 KB Victor ceiling.  **TESTING — REAL VICTOR ONLY, NOT DOSBox:** the 814 KB image **cannot load in DOSBox** — DOS conventional memory caps at 640 KB (DOS reports "Program too big to fit in memory" → a SILENT empty OUT.TXT), which is exactly why this project targets the 896 KB Victor 9000 (see [[victor9000-target]]).  New reproducer **`build/repl-victor.sh`** (+ `build/repl-input.txt`; both gitignored under `build/`): copies `victor_python.img`, injects `mpython.exe` as PROG.EXE + an `INPUT.TXT` + a **custom AUTOEXEC.BAT** (same `portset`/`ctty seriala`/`__V9BEGIN__`/`__V9END__` as the base disk but running **`prog < input.txt`**), runs MAME `victor9k` with `-rs232a null_modem -bitbanger`.  The `< input.txt` redirects handle 0 to the file while stdout stays on the serial `ctty` → the REPL's output is captured between the sentinels exactly like the existing harness, but the REPL is now DRIVEN by the file.  **GOTCHA that cost a full ~3-min run: the custom AUTOEXEC.BAT MUST be CRLF** — a LF-only batch file makes COMMAND.COM fail to parse it, so `portset`/`ctty` never run and the serial capture comes back TOTALLY EMPTY (not even `__V9BEGIN__`); `printf 'echo off\r\n…'` fixed it.  Real-Victor capture (240 s, `build/repl-input.txt`): `MicroPython on dos8086` / `>>> 3` (`1 + 2` auto-print) / `25` (`print(x*x)`) / `[0, 1, 4, 9]` (list comprehension) / `... ...` multi-line `def f(n): return n+100` then `107` (`f(7)` — CONTINUATION WORKS) / `ZeroDivisionError: divide by zero` (`1 // 0`) / `42` (`6 * 7` auto-printed AFTER the exception → **recovery PROVEN**) / EOF → clean exit → `C5`.  **NOTE: true division `/` raises `TypeError: unsupported type for operator` here (float is OFF, MICROPY_FLOAT_IMPL_NONE) — use `//` for ZeroDivisionError; and a bare string-literal statement `"x"` is swallowed as the module docstring (no auto-print) since each REPL input compiles as a fresh FILE_INPUT module.**  `make check` unaffected (no core change); minic 111 s/r 0 r/r unchanged (no minic change); **DOS gate UNCHANGED at 199** — the REPL is NOT DOSBox-gateable (image too big), so no `test-dos.sh` entry was added; it is Victor-validated instead.  NEXT candidates: a real line editor (backspace/history/arrows) needs RAW keyboard reads (AH=07h/08h) which break the `< file` redirect-testability — would want a CON-vs-redirected-file mode switch; enabling float (for true division) / longint / a `sys` module surface; or the 211-commit upstream-qbe rebase (pure plumbing, long deferred).  Reproducer: `build/repl-victor.sh build/mp-link/mpython.exe build/repl-input.txt 240`.  See [[mp-interactive-repl]].)

</details>

<details><summary>§3m(b) (prior) — struct-to-struct COPY honors C `volatile`</summary>

# Next session (§3m(b) — CLOSED §3m limitation (b): a struct-to-struct COPY `*d = *s` / `d = s` now honors C `volatile` — the LOADS carry the QBE `volatile` keyword when the SOURCE is volatile, the STORES when the DESTINATION is volatile.  This is the last open item in the §3j–§3m(a) volatile feature, so the C `volatile` surface is now COMPLETE for scalars, pointers-to-volatile/MMIO, struct members, whole aggregates via pointer, direct volatile objects, AND aggregate copies.  COMMITTED (minic/minic.y, tools/test-dos.sh, volatile_copy_probe.c).  NO QBE-core change — pure minic frontend, ~10 lines in `emit_struct_copy`, building on the §3j–§3m QVOLATILE machinery.  THE GAP: the word-by-word `emit_struct_copy` (used for EVERY aggregate assignment — direct `d = s`, deref `*d = *s`, struct-by-value arg copy, struct return) predated the QVOLATILE work and emitted plain loadw/storew (loadfw/storefw under far-data) regardless of either operand's volatility, so a volatile struct copy (MMIO register-block snapshot `snap = *regblock` or program `*regblock = cfg`) was treated as ordinary memory.  THE FIX (emit_struct_copy, minic.y): compute `src_vol = symb_isvolatile(src) || ISVOLATILE(src.ctyp)` and `dst_vol = symb_isvolatile(dst) || ISVOLATILE(dst.ctyp)` at the top, then emit `"volatile "` after the opcode at the 4 access sites — the per-word load + byte-tail load gated on src_vol, the per-word store + byte-tail store gated on dst_vol.  HOW THE QUALIFIER REACHES THE COPY (already wired by §3l/§3m/§3m(a), no new plumbing): for `*d = *s` through a `volatile struct S *`, the deref (`lval case '@'`) does `sr.ctyp = DREF(sr.ctyp)|far` which brings the pointee's QVOLATILE (encoded one IDIR up at bit 28) down to bit 25, so the dereffed aggregate lvalue carries QVOLATILE; for a directly-declared `volatile struct S g`, `lval case 'V'` re-derives QVOLATILE (the §3m(a) edit); for a named volatile global, `symb_isvolatile` fires too.  `DREF(dst.ctyp)` inside emit_struct_copy already masks `~QVOLATILE` before the shift, so the struct INDEX (sidx/size) is unaffected — the QVOLATILE bit is read ONLY by the two new `ISVOLATILE` checks.  WHY NO BLAST RADIUS: emit_struct_copy reads ISVOLATILE but never WRITES a ctyp or feeds one into a comparison; src/dst keep their existing types; non-volatile copies are byte-identical (the keyword string is empty when vol=0).  TESTING — granularity is IR-PRESENCE, NOT an asm op-count (a deliberate, documented departure from the scalar volatile probes): volatile's observable effect is preventing a redundant access from folding, but QBE does NOT optimize a multi-word aggregate copy — it neither CSEs the copy's loads across the copy's own intervening stores nor dead-store-eliminates GLOBAL stores (verified empirically: a plain double-copy `a=g;b=g;` does NOT fold; a `g=src; return src.f` does NOT forward across the copy's stores) — so there is no asm fold to prevent and an op-count is identical with/without the keyword.  Under far-data the copy is loadfw/storefw which loadopt leaves untouched anyway.  So the fix is bug-loud at the .ssa level: `minic/dos/examples/volatile_copy_probe.c` (5-byte struct `{int ctrl; int data; char tag;}` so each copy hits BOTH the per-word AND byte-tail path) + `run_volatile_copy_asm_probe` asserts the emitted .ssa carries `volatile` on the copy's LOADS for src-volatile twins (vcsrc direct-global, vpcopy_src ptr-deref) and on the STORES for dst-volatile twins (vcdst, vpcopy_dst), and is ABSENT in the plain twins (ncsrc/ncdst).  Pre-fix the volatile twins carry no keyword → presence asserts FAIL.  VERIFIED the final asm reads the source EXACTLY ONCE PER MEMBER (3 reads: 2 words + byte) — identical to the plain twin: minic spuriously emits a dead first-word load of the RHS via `expr(n->r)` before the struct-copy path takes over (PRE-EXISTING, present in plain copies too), and although that dead load is now `volatile` and correctly survives QBE's IR passes (volatile loads aren't elided), it is dropped as a dead-register write at register allocation, so NO extra MMIO read occurs.  `make check` green (no core change); minic 111 s/r 0 r/r unchanged (pure action code, no grammar rule touched); DOS gate **198→199**; non-volatile codegen byte-identical.  BUILD GOTCHA (still applies): top-level `make minic/minic` reuses a STALE binary via GNU make's implicit bison rule — ALWAYS `rm -f minic/minic && touch minic/minic.y && make minic/minic` after editing minic.y, verify the .ssa.  See [[minic-make-staleness]].  THE C `volatile` SURFACE IS NOW COMPLETE — no remaining volatile sub-cases.  NEXT candidates: the 211-commit upstream-qbe rebase (pure plumbing, long deferred); or return to MicroPython feature work.  Reproducer: `minic/dos/examples/volatile_copy_probe.c`.  See [[minic-volatile]].)

</details>

<details><summary>§3m(a) (prior) — C `volatile` on a DIRECT (non-pointer) volatile struct object, offset>0 members</summary>

# Next session (§3m(a) — CLOSED §3m limitation (a): C `volatile` on a DIRECT (non-pointer) `volatile struct S s` OBJECT now honors volatile for EVERY member, including OFFSET>0 (was offset-0-only, and only for globals).  COMMITTED (minic/minic.y, tools/test-dos.sh, volatile_direct_probe.c).  NO QBE-core change — pure minic frontend, two small action-code edits, building on the §3j–§3m QVOLATILE machinery.  THE GAP: a directly-declared volatile aggregate had its QVOLATILE bit STRIPPED from the stored type by varadd (into varh[].isvolatile), so a member access at offset>0 — a computed `$s+off`/`%s+off` address — reached neither symb_isvolatile (the Tmp address isn't the named symbol) nor any QVOLATILE on s0.ctyp; the load/store carried no `volatile` keyword and was CSE'd / store-forwarded like plain memory.  (Offset-0 of a GLOBAL accidentally worked because addr==s0==the Glo symbol, so symb_isvolatile fired; a LOCAL honored nothing.)  THE FIX (2 edits, minic.y): (1) lval `case 'V'` — after `sr = *varget(...)`, RE-DERIVE the qualifier: `if ((KIND(sr.ctyp)==STRUCT_T||UNION_T) && var_isvolatile(n->u.v)) sr.ctyp |= QVOLATILE;`.  GATED to aggregates so scalar volatile locals/globals stay BYTE-IDENTICAL (handled by markvol via the volatile alloc / symb_isvolatile via the named symbol — adding QVOLATILE there would change their codegen).  The existing member-access sites (expr `.` 3440, lval `.` 4256/4265) already `OR ISVOLATILE(s0.ctyp)` onto each member's value type, and the lval `.` path propagates QVOLATILE onto a returned SUB-STRUCT lvalue, so nested `s.inner.x` composes for free; the read path STRIPS QVOLATILE from the rvalue (C11 6.3.2.1) so it never reaches a raw `ctyp==` site.  (2) `case 'A'` (address-of) — `sr.ctyp = IDIR(sr.ctyp & ~QVOLATILE)` so `&volatile_struct` yields a PLAIN (non-volatile-pointee) pointer, keeping pointer types byte-identical and away from raw pointer `ctyp ==` sites; mirrors §3l's documented `&named_volatile` behavior (code needing volatile through a pointer declares it `volatile T *p`, whose pointee bit is set independently by the VOLATILE type rules).  WHY THE BLAST RADIUS IS CONTAINED (the audit §3m flagged): every struct-aggregate handling site — struct assignment (KIND check + emit_struct_copy), struct return (cur_fn_sret + emit_struct_copy), struct by-value arg (is_aggr/emit_arg), and emit_struct_copy itself — keys off KIND()/DREF()/is_aggr()/ISFAR(), ALL of which mask or strip QVOLATILE (bit 25); the only raw full-`ctyp` comparison a struct value could reach is the pointer from `&`, neutralized by edit (2).  varadd's stored type is UNCHANGED (still strips QVOLATILE), so all varh[].ctyp `==`/`!=` redeclaration checks stay byte-identical.  TESTING: `minic/dos/examples/volatile_direct_probe.c` + `run_volatile_direct_asm_probe` (medium, asm-inspection — far-data goes through loadfw/storefw which loadopt never optimizes, so honored regardless there).  Three volatile/plain pairs at OFFSET>0 member `data` (offset 2 after a 16-bit `ctrl`): `vg_read`/`vg_fwd` (direct volatile GLOBAL struct) + `vl_read` (direct volatile LOCAL struct); each volatile twin keeps STRICTLY MORE `word [` ops than its identical-bodied plain twin (CSE + store-forward prevented).  Bug-loud: the GLOBAL pairs FOLD to match their plain twins against a pre-fix minic (`vg_read 1==ng_read 1`), post-fix `2>1`.  (The local pair already kept 2 pre-fix — coverage, not the bug-loud guard.)  `make check` green (no core change); minic 111 s/r 0 r/r unchanged; DOS gate **197→198**; non-volatile codegen byte-identical.  **BUILD GOTCHA that cost a gate cycle (first gate run FALSELY failed):** top-level `make minic/minic` uses GNU make's IMPLICIT bison rule with minic/minic.c as an auto-deleted intermediate, so once the binary exists it SKIPS the rebuild even when minic.y changed — the gate's `make minic/minic` step reused a STALE pre-fix binary.  ALWAYS `rm -f minic/minic && touch minic/minic.y && make minic/minic` after editing minic.y and verify the emitted .ssa before trusting a gate result.  See [[minic-make-staleness]].  REMAINING (§3m limitation (b), still deferred — no current consumer): a volatile struct-to-struct COPY `*d=*s` is NOT volatile (the word-by-word emit_struct_copy path predates this work, doesn't check QVOLATILE).  NEXT candidates: (b) above if a real consumer needs it; the 211-commit upstream-qbe rebase.  Reproducer: `minic/dos/examples/volatile_direct_probe.c`.  See [[minic-volatile]].)

</details>

<details><summary>§3m (prior) — C `volatile` for STRUCT MEMBERS + whole aggregate via pointer</summary>

# Next session (§3m — EXTEND PHASE part 3: C `volatile` for STRUCT MEMBERS + whole VOLATILE AGGREGATE via pointer (`volatile struct S *p; p->m`).  COMMITTED (minic/minic.y, tools/test-dos.sh, volatile_struct_probe.c).  NO QBE-core change — pure minic frontend, building on the §3j/§3k/§3l QVOLATILE machinery.  **KEY DISCOVERY: two of the three sub-cases ALREADY WORKED before this session and just needed a regression guard** — (1) a volatile-qualified MEMBER `struct{volatile int x;}` (the QVOLATILE bit rides in `m->ctyp` from the `VOLATILE T` member-type production, and the existing member-access load/store at expr/lval `case '.'` set `sr.ctyp = m->ctyp`, so the §3k `ISVOLATILE(d.ctyp)` check in load()/store fires); (2) a volatile ARRAY `volatile int a[N]` (the element type carries QVOLATILE → the subscript load is volatile).  The REAL GAP was the canonical MMIO case: a whole `volatile struct S *p` — the `VOLATILE STRUCT/UNION IDENT` productions only set g_decl_volatile and DROPPED the qualifier, so `p->anymember` was NOT volatile.  THE FIX (3 parts, minic.y): (a) `VOLATILE STRUCT IDENT` / `VOLATILE UNION IDENT` now return `((idx<<3)+STRUCT_T)|QVOLATILE` so the qualifier rides on the struct TYPE; for `volatile struct S *p` it shifts up through IDIR (pointee volatile→bit 28) and back down through DREF at the `*p` deref (→bit 25, since DREF masks bit 25 of x before the shift but bit 28 survives), so the dereffed struct lvalue `s0.ctyp` carries QVOLATILE.  (b) member access (expr `case '.'` read + lval `case '.'`) ORs `ISVOLATILE(s0.ctyp)` onto each member's value type so EVERY member access through a volatile aggregate emits the QBE `volatile` keyword (it COMBINES with the member's OWN QVOLATILE in m->ctyp — nested `o->inner.data` works because lval(`o->inner`) returns a QVOLATILE lvalue and the inner `.data` access propagates it); the read path STRIPS QVOLATILE from the result rvalue (C11 6.3.2.1 lvalue conversion) right before `break` so it never reaches the raw `ctyp==` comparison sites.  (c) FIXED A PRE-EXISTING g_decl_volatile LEAK — a `volatile` MEMBER left g_decl_volatile=1 (the member-type production sets it, but the smembers rules never consumed it since the member's volatility is already captured in m->ctyp), leaking into the NEXT file-scope decl (`struct{volatile int x;}; int g;` made `g` spuriously volatile, reading as `loadw volatile`); now cleared in structaddmember / structaddarrmember / structaddbitfield right after capturing ctyp.  Bug-loud MEDIUM-only (far loadfw/storefw are unoptimized by loadopt, so volatile is conservatively honored there regardless — like §3k/§3l).  Probe `volatile_struct_probe.c` + `run_volatile_struct_asm_probe`: 4 pairs (vm_read/vm_fwd = volatile member through a plain ptr; vs_read/vs_fwd = volatile aggregate through a volatile ptr) each keep STRICTLY MORE word-mem ops than their identical-bodied plain twins (CSE + store-forward prevented).  `make check` green (no core change); minic 111 s/r 0 r/r unchanged (pure action/helper code, no grammar rule touched); DOS gate **196→197**; non-volatile codegen byte-identical (the leak fix only REMOVES a spurious volatile keyword from previously-affected structs).  KNOWN LIMITATIONS (documented + contained, matching §3k/§3l discipline): (1) a DIRECT (non-pointer) `volatile struct S s` local/global honors volatile ONLY for its OFFSET-0 member (reached via the named symbol → symb_isvolatile); an offset>0 member becomes a computed `$s+off` address so its access is NOT volatile — varadd strips QVOLATILE from the stored aggregate type and records only a scalar isvolatile flag keyed to the symbol name.  Fixing fully = keep QVOLATILE on a STRUCT/UNION-typed var's STORED type (so member access sees it via ISVOLATILE(s0.ctyp)), but that risks polluting struct-copy / struct-return `ctyp==` sites — deferred.  (2) a volatile struct-to-struct COPY `*d=*s` (d a `volatile struct *`) is NOT volatile — the word-by-word struct-copy path predates this work and doesn't check QVOLATILE.  Both are uncommon (the MMIO register-block case is the POINTER, which fully works).  NEXT candidates: (a) the two limitations above if a real consumer needs them; (b) the 211-commit upstream-qbe rebase.  Reproducers: `minic/dos/examples/volatile_struct_probe.c`, `/tmp/v{mem,gap,p2,write,leak,direct}.c` style.  See [[minic-volatile]].)

</details>

<details><summary>§3l (prior) — C `volatile` for POINTER-TO-VOLATILE / MMIO</summary>

# Next session (§3l — EXTEND PHASE part 2: C `volatile` for POINTER-TO-VOLATILE / MMIO (`volatile T *p; *p=x; y=*p`) — the canonical memory-mapped-I/O case, and the higher-value/higher-risk half of the extend phase the user picked.  COMMITTED (minic/minic.y, tools/test-dos.sh, volatile_ptr_probe.c).  NO QBE-core change — the §3j machinery (the `vol:1` Ins bit + parse.c keyword + the four promote/loadopt/coalesce/gcm gates) already honors the keyword; minic just needed to EMIT it at a DEREFERENCE whose pointee is volatile-qualified.  THE MECHANISM (minic frontend only): the qualifier rides INSIDE the type encoding as a new `QVOLATILE` bit so it survives the IDIR→DREF round-trip.  **CRITICAL NAMING GOTCHA that cost a debug cycle: the macro is `QVOLATILE`, NOT `VOLATILE`, because `VOLATILE` is already a grammar TOKEN — yacc emits its own `#define VOLATILE <toknum>` in the generated C that SHADOWS a same-named macro in the action code, so `$$ = INT | VOLATILE` silently OR'd in the token number (0xa0) giving 0xa2 instead of 0x2000002.**  `#define QVOLATILE (1<<25)` + `#define ISVOLATILE(x) ((x)&QVOLATILE)`; `DREF` is now `((x) & ~FAR & ~QVOLATILE) >> 3` — strips the OUTER qualifier before the shift exactly like the FAR mask (the pointee's OWN volatile is encoded one IDIR up at bit 28 and shifts down to 25 correctly; the mask also cleans DREF-as-struct-index extraction).  FLOW: the `VOLATILE T` base productions now return `T | QVOLATILE` (and still set `g_decl_volatile`); `IDIR(T|QVOLATILE)` shifts the bit up so a pointer ENCODES its pointee-volatility; the plain `type '*'` / `type '*' CONST` / `TFAR` pointer rules now RESET `g_decl_volatile = 0` (the pending qualifier belonged to the POINTEE, now captured in the type bit) so the pointer OBJECT stays non-volatile — **this FIXES a prior wrong behavior where §3j/§3k marked the pointer `p` itself volatile for `volatile T *p`, not `*p`**; `type '*' VOLATILE` keeps `g_decl_volatile = 1` (`T * volatile` — the pointer IS volatile).  varadd/varaddextern: `vol = g_decl_volatile || ISVOLATILE(ctyp)` then `ctyp &= ~QVOLATILE` strips the OUTER bit from the STORED type (so the inner pointee bit is preserved for a later `*p` while the `varh[].ctyp ==` redeclaration checks + all downstream comparisons stay byte-identical).  EMIT: the deref load/store sites OR `ISVOLATILE(value/lvalue-type)` into the six §3k `symb_isvolatile` sites — `load()`/`loadfar()` check `d.ctyp` (the pointee recovered by DREF), the inline `=`/prefix/postfix stores check the lvalue `s1`/`sl.ctyp`.  CRUCIAL to contain the bit: it is STRIPPED from the deref RVALUE (case '@' after the load — C11 6.3.2.1: the result of a volatile lvalue read is an ordinary unqualified rvalue) + the inc/dec loaded value + masked in the assignment exact-type test (`& ~QVOLATILE`), so QVOLATILE NEVER reaches the raw `ctyp == LNG`/`== NIL`/`== IDIR(NIL)` comparison sites (which mask only FAR) — that containment is what makes the audit-risk the user flagged a non-issue.  Nested `volatile int **pp` is CORRECT: `*pp` (the inner pointer) is plain, `**pp` (volatile int) is volatile.  TESTING: bug-loud discrimination is MEDIUM-ONLY (same as §3j/§3k — under far-data the deref uses i8086 loadfw/storefw which QBE loadopt doesn't optimize at all, so volatile is conservatively honored regardless; the keyword is still emitted + IR-verified, future-proof).  `minic/dos/examples/volatile_ptr_probe.c` + `run_volatile_ptr_asm_probe` (medium): `vp_read`/`vp_fwd` (`*p+*p` and `*p=5;return *p` through a `volatile int *` param) keep STRICTLY MORE word-mem ops than their identical-bodied plain-`int *` twins `np_read`/`np_fwd` (volatile prevents the CSE and the store→load forward).  Verified vs the COMMITTED minic: it left `*ep` deref NON-volatile (pointee volatility ignored entirely — the actual bug); now the deref is volatile while plain global pointers (`int *gp`) are unaffected.  `make check` green; minic 111 s/r 0 r/r unchanged (pure action/helper code, no grammar rule touched); DOS gate **195→196**; non-volatile codegen byte-identical (named-local + named-global volatile probes still green).  KNOWN minor over-conservatism (PRE-EXISTING §3k, HARMLESS = the access is merely un-optimized, never wrong; NOT gate-tested): `extern volatile int *ep` and a file-scope `volatile int *gp` mark the pointer OBJECT volatile too, because the extern/global declarator path retains `g_decl_volatile` (only LOCALS/PARAMS flow through the new `type '*'` reset); and `&named_volatile` yields a plain (non-volatile-pointee) pointer because varadd stripped the bit from the named object's stored type.  SCOPE / EXTEND phase remaining (deferred): volatile STRUCT MEMBERS / arrays — `markvol`, `symb_isvolatile`, and the QVOLATILE-on-base machinery are all direct-named-scalar / scalar-pointee only; `volatile struct S *p` does NOT mark the struct pointee (the `VOLATILE STRUCT`/`UNION`/`ENUM IDENT` productions don't add QVOLATILE).  NEXT candidates: volatile struct members/arrays (lower-risk follow-on), or (D) the 211-commit upstream-qbe rebase.  Reproducers: `minic/dos/examples/volatile_ptr_probe.c`, `/tmp/volptr.c`/`vpf.c` style (vp_read/vp_fwd vs np_*).  See [[minic-volatile]].)

</details>

<details><summary>§3k (prior) — C `volatile` for named scalar GLOBALS + externs (extend phase part 1)</summary>

# Next session (§3k — EXTEND PHASE part 1: C `volatile` for named scalar GLOBALS + externs (the lower-risk half of the §3j extend phase the user picked).  COMMITTED (minic/minic.y, tools/test-dos.sh, volatile_global_probe.c).  NO QBE-core change this session — the §3j machinery (the `vol:1` Ins bit + parse.c keyword + the four promote/loadopt/coalesce/gcm gates) already does all the work; globals just needed minic to EMIT the `volatile` keyword at the access, since a global has no `alloc` and so §3j's `markvol(fn)` (which propagates the bit from a volatile alloc to its loads/stores) can never reach it.  THE MECHANISM (minic frontend only): new `symb_isvolatile(Symb s)` — for a `Glo` symbol look the source name up via `gloname[s.u.n]`, for an `Ext` via `s.u.v`, both through the existing `var_isvolatile` (which reads `varh[].isvolatile`, ALREADY set for volatile globals by §3j's `varadd` consume-`g_decl_volatile` path); a `Var` (local) returns 0 deliberately — locals are covered by markvol via their volatile `alloc`, and emitting the keyword here too would change their codegen.  Wired at SIX scalar load/store emit sites, each appending `"volatile "` AFTER the opcode's existing trailing space and BEFORE the operands (so non-volatile output is BYTE-IDENTICAL — `symb_isvolatile` returns 0 → empty string): `load()` (near loadw/loadub/loadsh…), `loadfar()` (far loadfw/loadfl…), `storefar()`, the assignment `=` store (lvalue `s1`), the prefix `++`/`--` store and the postfix `++`/`--` store (lvalue `sl`).  `extern volatile` ALSO fixed: the extern path goes through `varaddextern`, NOT `varadd`, so it never consumed `g_decl_volatile` — added the same consume-and-reset there, `varh[].isvolatile = vol` on a fresh slot and `|= vol` on a redeclaration (so a volatile decl upgrades a prior plain extern).  KEY FINDING that shaped the probe: the bug-loud discrimination is **MEDIUM-MODEL-ONLY**.  Under near-data (medium) a global is accessed with the standard `loadw`/`storew`, which QBE's loadopt CAN forward (`g=7; return g;` → returns the constant) and CSE (`g+g` → one load) — so the volatile keyword visibly changes the asm.  Under FAR-DATA (compact/large) a global goes through the i8086-specific `loadfw`/`storefw` ops, which loadopt does NOT optimize at all (verified: the non-volatile `nfwd`/`nload` already reload under compact), so volatile is conservatively honored there REGARDLESS — the keyword is still emitted (IR-verified) and is future-proof, but produces no asm delta to assert on.  So the probe asserts at medium, exactly like §3j's local probe.  TESTING: `minic/dos/examples/volatile_global_probe.c` + `run_volatile_global_asm_probe` (medium) — `vg_load` (two reads of a `volatile int`), `vg_fwd` (`vg=7; return vg;`), and `evg_load` (through an `extern volatile int`) together KEEP 6 word-memory ops (2 + store+reload + 2); the identical-bodied non-volatile `ng_load`/`ng_fwd`/`eng_load` FOLD to 3 (1 each).  Bug-loud: without the global emit, the volatile trio CSEs/forwards down to 3 too.  The probe is asm-only (never linked), so the undefined `extern`s are fine.  `make check` green (no core change); minic 111 s/r 0 r/r unchanged (pure action/helper code, no grammar rule touched); DOS gate **194→195**; non-volatile codegen byte-identical (gate's stevie/examples/runtime probes all unaffected — none use volatile globals).  NOTE on MicroPython: not rebuilt this session (not in the gate); the extern-volatile change is conservative (only ever ADDS the keyword, never wrong) so a volatile extern in the port would just be correctly un-optimized.  SCOPE / EXTEND phase remaining (deferred, in risk order): (a) pointer-to-volatile / MMIO `volatile T *p; *p=x` — THE canonical MMIO case, needs a ctyp qualifier bit threaded through IDIR/DREF + auditing/masking the ~90 raw `ctyp==`/`!=` comparisons (real regression risk); (b) volatile struct members / arrays (markvol + symb_isvolatile are both direct-named-scalar only).  Current behavior on those unchanged from §3j: `volatile T *p` leaves p unmarked (pointee volatility ignored — correct for the p-as-value), `int * volatile p` marks p.  NEXT candidates: the pointer-to-volatile/MMIO ctyp-bit work (highest value, highest risk), or (D) the 211-commit upstream-qbe rebase.  Reproducers: `minic/dos/examples/volatile_global_probe.c`, `/tmp/volglob.c`/`volfwd.c`/`volext.c` style.  See [[minic-volatile]].)

</details>

<details><summary>§3j (prior) — C `volatile` for named scalar LOCAL/PARAM objects (phase 1)</summary>

# Next session (§3j — IMPLEMENTED C `volatile` for named scalar LOCAL/PARAM objects (phase 1 of "named first, then extend").  COMMITTED (all.h, parse.c, mem.c, load.c, gcm.c, main.c, tools/lexh.c, minic/minic.y, tools/test-dos.sh, volatile_probe.c, + designated-init fixes in amd64/arm64/rv64/pmov).  WHY it was real work: QBE has NO native volatile, and `AEsc` (the §3e escape mechanism) is INSUFFICIENT — `load.c:244-246` only respects escape ACROSS A CALL (`iscall && escapes`), so within straight-line code loadopt still forwards stores→loads and CSEs loads even for escaping slots.  So volatile needed a real per-instruction flag honored by four passes.  THE MECHANISM (QBE core, TARGET-GENERAL — helps amd64/arm64/rv64 too, `make check` green on all): (1) stole `vol:1` from `Ins.op:30`→`op:29` in all.h (zero struct growth; opcodes fit in 29 bits).  (2) parse.c: new `volatile` keyword between a load/store/alloc opcode and its operands (`%v =w loadw volatile %a`, `storew volatile %x,%a`, `%a =l alloc4 volatile 4`); the existing `lexh` perfect hash K=362902335/M=23 already accommodates it (NO regen needed — verified via tools/lexh.c).  (3) `markvol(fn)` (mem.c, wired in main.c after the first filluse, before promote): propagates the bit from a volatile `alloc` to every load/store that addresses it DIRECTLY (named scalar; alloc+offset NOT marked = the deferred pointer/array case).  (4) FOUR gates: promote skips volatile allocs (else it register-promotes them and erases all accesses); loadopt skips resolving volatile loads + treats any volatile op as a backward-scan barrier (`goto Load`); coalesce excludes volatile slots from collection (no dead-store-kill, no slot-coalesce); gcm `pinned()|=vol` + `canelim()&=!vol` (volatile load is pinned-to-block AND never elided-when-unused).  GVN confirmed clear (no load/store handling).  ALL gates are NO-OPS when vol=0 → non-volatile codegen BYTE-IDENTICAL (gate stayed green with only +1 new probe).  THE minic SIDE (named scalar scope, NO ctyp bit — deliberately, to avoid disturbing the 90 raw `ctyp==`/`!=` comparisons): global `g_decl_volatile` set in the VOLATILE *type* productions (NOT the `asm volatile` productions — verified `asm volatile("nop")` does NOT leak to the next local); consumed-and-cleared in `varadd`→`varh[].isvolatile` (so no unbounded leak; multi-declarator `volatile int a,b;` marks only `a` — minor documented limit); `emit_local_alloc()` emits `alloc4 volatile N` at the 7 scalar local/param alloc sites (param spill, dcls `T x;`/`T x=e;`, block-scope `T x;`/`T x=e;`, for-init, emit_local_init).  KEY GOTCHA that cost a build cycle: adding a field to `Ins` broke NINE positional `(Ins){op,cls,to,{arg}}` initializers (parse.c×8, load.c, amd64/sysv+emit, arm64/abi+emit, rv64/abi, tools/pmov) — converted all to DESIGNATED initializers so the new field defaults to 0 and field order is robust.  TESTING — volatile is a CODEGEN property, NOT runtime-observable in a self-contained program (no external agent → identical results either way), so it is pinned by a COMPILE-TIME asm-inspection probe, not a runtime golden: `minic/dos/examples/volatile_probe.c` + `run_volatile_asm_probe` in tools/test-dos.sh compile through minic→qbe→i8086 asm and assert `volf()` KEEPS ≥4 word-memory ops (2 stores incl. the dead `x=5` + 2 loads) while the identical-bodied `nonvolf()` FOLDS to `mov ax,20` (0 mem ops).  Bug-loud: without the gates volf folds too.  `make check` green; DOS gate **193→194**; non-volatile output byte-identical.  SCOPE / the "EXTEND" phase (explicitly deferred, user chose "named first, then extend"): (a) volatile GLOBALS (no alloc → markvol can't reach them; need direct `volatile`-keyword emit at the scattered load()/loadfar()/store sites, incl. far-data loadfw/storefw); (b) pointer-to-volatile / MMIO `volatile T *p; *p=x` (THE canonical MMIO case — needs a ctyp qualifier bit threaded through IDIR/DREF + auditing/masking the 90 raw ctyp comparisons; real regression risk, hence deferred).  Current behavior on those: `volatile T *p` leaves p unmarked (correct — pointee volatility just ignored); `int * volatile p` correctly marks p; volatile struct members / arrays not covered (markvol is direct-address only).  NEXT candidates: the volatile EXTEND phase (globals are lower-risk than the ctyp-bit pointer-to-volatile), or (D) the 211-commit upstream-qbe rebase.  Reproducers: `/tmp/voltest.c` style (volf/nonvolf), `minic/dos/examples/volatile_probe.c`.  See [[minic-volatile]].)

</details>

<details><summary>§3i (prior) — fast-alloc 4-byte alignment for tagged mp_obj_t pointers</summary>

# Next session (§3i — FIXED `min([..])`/`max([..])` and the whole stack-iterator builtin class (`all`/`any`/`sum` too).  COMMITTED (all.h, i8086/isel.c, i8086/emit.c, objalign_probe + golden, tools/test-dos.sh).  ROOT CAUSE was NOT the multi-layer round-trip §3h suspected — it was **fast-alloc stack objects are only 2-byte aligned on 8086, but MicroPython REPR_A tags pointers in the low 2 bits so an `mp_obj_t` MUST be 4-byte aligned**.  `mp_builtin_min_max`'s `mp_obj_iter_buf_t iter_buf;` landed at a frame offset with `&3==2`; `mp_getiter` returns `MP_OBJ_FROM_PTR(iter_buf)` (a far ptr to it), and `mp_obj_get_type`'s `mp_obj_is_obj(o) == ((mp_int_t)o & 3)==0` then read FALSE → treated the iterator as an immediate → `types[off&0xf]` (a wrong builtin type) → flags lacked the iter bit → `mp_iternext` raised `object not an iterator`.  WHY sorted/enumerate/varargs escaped it: `sorted([list])` fast-copies a list arg in `list_make_new` WITHOUT iterating; the VM's comprehensions/generators iterate via the **heap** Python value stack (gc-4-aligned), never a C-stack iter_buf; varargs `min(1,2,3)` never makes an iterator.  CRACKED in DOSBox (seconds), NOT Victor: `build/iterbuf5_probe.c` printed `iterbuf addr &3 = 2 / is_obj NO(BUG)` — the §3h iterbuf2 probe missed it because it compared the read-back type pointer but never checked `(addr&3)==0`.  THE FIX (i8086 backend, LOCAL — no ABI/crt0/linker change): minic already emits `alloc4` (4-byte-align request) but the i8086 slot allocator rounded only the SIZE, and BP itself is only 2-byte aligned at runtime.  isel.c now flags every fast-alloc of original size >=4 (could hold an `mp_obj_t`; 2-byte allocs can't) in a per-slot `fn->salign4[]` bitmap and reserves 2 bytes headroom; emit.c's two Oaddr handlers (Kl->slot far-ptr escape, and Kw->reg near/medium) round the materialised address up to a 4-byte boundary (`add ax,3; and ax,0xFFFC`).  Rounding is deterministic (BP fixed within a call) so a direct field write and an escaped tagged pointer to the SAME slot agree — verified by the probe writing a field directly AND through the returned pointer.  REAL VICTOR (`build/bi-probe.py`, 220 s): `min_var 1 / max_var 3 / min_lst 1 / max_lst 3 / abs 5 / sorted [1,2,3] / enum [(0,'a'),(1,'b')] / END`, clean `D4 C5` — min_lst/max_lst were `ERR object not an iterator` before.  `make check` green; DOS gate **190->193** (`objalign_probe` medium+compact+large, bug-loud: pre-fix prints `is_obj BUG` for any buf at `&3==2`); mpython.exe **791216->802128 B** (+10.9KB from headroom slots + rounding instrs; well under the ~896KB ceiling).  NEXT candidates (all LOWER priority — the language surface is now complete + validated): (C) PROPER `volatile` (minic still DISCARDS it — only non-setjmp/MMIO uses; §3e covers setjmp conservatively); (D) the 211-commit upstream-qbe rebase.  The §3i alignment fix is i8086-only (gated by the slot model); the §3e alias.c change is TARGET-GENERAL and upstream-worthy.  HARNESS: feature/exception probes need ≥200 s Victor budget (slow parse, NOT a hang); run via run_in_background:true + redirect to a file (NOT `| tail`).  Reproducers: `build/iterbuf5_probe.c` (the DOSBox alignment repro), `build/bi-probe.py`/`bi2-probe.py` (per-builtin), `build/nlr_mock_probe.c` (§3e setjmp).)

</details>

<details><summary>§3h (prior) framing — the MicroPython language surface is SOUND</summary>

# (§3h header retained below)

# Next session (§3h — the MicroPython language surface is SOUND + integration-validated on real Victor (§3d/§3e/§3f), and §3g(B) found the "2 minic bugs" were actually ONE qstr-pool sync issue (NOW FIXED — see §3g(B) block).  builtins min/max/abs/sorted/enumerate are now ENABLED; abs/sorted/enumerate + min/max-VARARGS all work on real Victor.  ONE narrow bug remains:  PRIMARY next task = FIX `min([..])`/`max([..])` (the single-ITERABLE-arg path) which raises `TypeError: object not an iterator` on Victor while varargs `min(1,2,3)` and `sorted([..])`/`enumerate` all work.  ROOT: `mp_builtin_min_max`'s `if (n_args==1)` branch declares a STACK `mp_obj_iter_buf_t iter_buf;` and passes `&iter_buf` down THREE call layers (`min_max → mp_getiter → list_getiter → list-iterator-new`); the iterator's type is written into that stack buffer through the far pointer and read back via the returned `mp_obj_t` (= a far ptr to the stack buf).  `mp_iternext` then reads a WRONG type → "object not an iterator".  So it's a FAR-DATA codegen bug in the multi-layer `&stack_aggregate` round-trip (sorted/enumerate avoid it — they don't pass a caller stack iter_buf through getiter the same way).  TWO DOSBox repros already WORK (do NOT reproduce): `build/iterbuf_probe.c` (1-layer: fn writes through `&stack_struct` + returns it, caller derefs) and `build/iterbuf2_probe.c` (3-layer chain `getiter→type_getiter→new_iter` + the buf declared in a non-entry `if` block).  So the trigger needs MORE of the real `mp_builtin_min_max` (the `while ((item=mp_iternext(iterable))!=NULL)` LOOP re-reading the buf across iternext calls + the `key_fn`/`best_key`/`best_obj` interplay + the real `mp_obj_iter_buf_t`).  DON'T hand-type further — either shrink the REAL `mp_builtin_min_max` from `build/mp-link/modbuiltins.pp.c` (rebuild with the flags on) down to a runnable DOSBox fragment, OR on-target instrument (print the iterable's type ptr seg:off right after mp_getiter vs inside mp_iternext).  Also worth checking: does `mp_iternext` read the iterator TYPE via a far ptr to a ROM type object (not the stack) — maybe it's a far-ROM-type read, not the stack buf.  EXACT exception captured: `min_lst ERR object not an iterator` (`build/bi2-probe.py`).  Per-builtin status pinned by `build/bi-probe.py` (min_var/max_var/abs/sorted/enum OK; min_lst/max_lst ERR).  LOWER-priority alternatives:  (C) PROPER `volatile` — minic still DISCARDS the `volatile` qualifier on variables (the type productions `VOLATILE TINT → INT` drop it; `isvolatile` in varh is only for inline-asm).  §3e's setjmp-gate covers the setjmp/longjmp case conservatively, but a TRUE `volatile` (needed for memory-mapped I/O, or any non-setjmp use) is still unimplemented — a real local would be register-cached.  Low priority for MicroPython (no MMIO), but it is a real C-conformance gap; doing it right = thread `volatile` into varh + force those allocs non-promotable + emit a per-access load/store QBE won't elide.  (D) the 211-commit upstream-qbe rebase (still deferred).  NOTE: §3e's alias.c change is TARGET-GENERAL (helps amd64/arm64/rv64 too — `make check` green), a genuine upstream-worthy QBE correctness fix.  HARNESS: feature/exception probes need a ≥200 s Victor budget (slow parse, NOT a hang); host minimal port (slice enabled) for GRAMMAR only.  Reproducers: `build/exc-min.py`, `build/exc{,2,3,4}-probe.py`, `build/nlr_mock_probe.c` (the fast DOSBox setjmp/longjmp repro that cracked §3e).)

</details>

> ---
>
> **§3h-investigation (2026-06-04) — chased the min/max-iterable bug; did
> NOT crack it, but FOUND A TANGENTIAL static-far-data-pointer bug.  NO CODE
> SHIPPED.  min/max-iterable still needs ON-TARGET instrumentation.**
>
>  - Built up `build/iterbuf{,2,3,4}_probe.c` modeling the
>    `min_max → mp_getiter → … → list-iterator` stack-iter_buf round-trip.
>    1-/3-layer + non-entry-block + compare-the-type-ptr ALL WORK.  The first
>    DIFFERENCE that broke: `iterbuf3` DEREFERENCES the read-back type ptr to
>    read a `const char *name` field → came back EMPTY.
>  - **But that's a SEPARATE bug**: `build/staticptr_probe.c` /
>    `staticptr2_probe.c` show a file-scope `static const struct { …; const
>    char *p; int *q; }` with `p="literal"`/`q=&global` reads the POINTER
>    fields as GARBAGE under **compact** (wrong SEGMENT — offset is right) but
>    CORRECT under **medium**.  This is the known §1g "far static-data-ptr
>    relocation gap" (asm_to_omf/omf_link emit the offset but not a seg:off
>    data relocation for a pointer-valued static initializer).
>  - **Why it's almost certainly NOT the min/max cause**: the real
>    `mp_obj_type_t` has `uint16_t name`(qstr) + uint8 slot_index + a
>    FUNCTION-pointer `slots[]` (code far ptrs, which static-init FINE via
>    §1z) — NO `const char*`/`&data` field.  And MicroPython's pervasive
>    `MP_ROM_PTR(&type)` tables clearly WORK (builtins resolve), so its
>    actual far-data config (FARSTORAGE opt-in via `--far-static-data`)
>    must handle static data-ptrs that build-example's far-everything flags
>    do not.  So the staticptr bug reproduces under build-example but may not
>    bite the real mpython build.
>  - **NEXT for min/max-iterable**: stop hand-repro'ing — INSTRUMENT
>    on-target.  Add to `mp_builtin_min_max`'s `n_args==1` branch (real
>    modbuiltins.c) a print of `iterable`'s far ptr seg:off right after
>    `mp_getiter`, and of the type ptr `mp_iternext` reads; one Victor run
>    shows whether the iter_buf pointer or the type/slot read is wrong.
>    (Image has ~105KB headroom for a couple of mp_printf calls.)  Also
>    re-examine whether the §1g static-data-ptr gap is worth a general fix
>    (asm_to_omf/omf_link far data relocation) — it would harden any
>    pointer-valued static table under far-data.
>
> ---
>
> **§3g(B) (2026-06-04) — enabled builtins; the "2 minic bugs" were actually
> ONE qstr-pool SYNC issue (FIXED via config+regen, NOT minic).  4 of 5
> builtins work on Victor; min/max-iterable has a separate far-data bug
> (→ §3h).  No qbe-repo code change (port-tree config + qstr regen only).**
>
>  - Enabling `MICROPY_PY_BUILTINS_MIN_MAX`+`ENUMERATE` first gave 104/106 OK,
>    2 minic FAILs (`modbuiltins`, `objenumerate`).  Both errors —
>    `modbuiltins` "undefined variable" and `objenumerate` "undefined
>    identifier in static initializer" — turned out to be the SAME thing:
>    **a MISSING qstr in the genhdr pool**.  `MP_QSTR_default` (used by
>    `mp_builtin_min_max`) and `MP_QSTR_enumerate` (used by the enumerate
>    type init) appeared ONLY at their use site in the `.pp.c`, NOT in the
>    qstr enum — because the genhdr (`ports/minimal/build/genhdr/
>    qstrdefs.generated.h`) is generated by the HOST MINIMAL PORT, which did
>    NOT have these flags on, so its qstr scan never collected those names.
>    minic CORRECTLY reported them undefined (like §1m's `stream`/SEEK_SET —
>    a build/include artifact, not a frontend gap).  THE FOUR HAND-REPROS
>    that "didn't reproduce" were a red herring: the real construct compiles
>    fine; only the missing qstr broke it.
>  - **FIX**: enable `MICROPY_PY_BUILTINS_MIN_MAX (1)` + `ENUMERATE (1)` in
>    BOTH `ports/minimal/mpconfigport.h` (the genhdr source) AND
>    `ports/dos8086/mpconfigport.h`, then `make CROSS=0` in ports/minimal to
>    regenerate qstrdefs (now has QDEF `MP_QSTR_default`,`MP_QSTR_enumerate`).
>    Rebuild mpython: **106/106 OK**, image **791216 B** (+6.4KB, under the
>    ~896KB ceiling).  All UNTRACKED port-tree changes.
>  - **Verified on Victor** (feature + per-builtin probes): abs, sorted,
>    enumerate, and min/max-VARARGS all work; the rest of the surface is
>    UNREGRESSED (mul..exc still all OK at 791216).  ONLY `min([..])`/
>    `max([..])` (single-iterable path) raises `object not an iterator` — a
>    far-data codegen bug, see §3h header.
>  - LESSON: when enabling a config flag pulls in a TU that fails with
>    "undefined identifier/variable" on an `MP_QSTR_*`, CHECK THE GENHDR
>    QSTR POOL FIRST (grep the .pp.c for the qstr's definition) before
>    suspecting minic — the genhdr is config-gated and must be regenerated
>    with the same flags.  (Saved `build/saved-{modbuiltins-minmax,
>    objenumerate}.pp.c` are now obsolete — the bug wasn't in them.)
>
> ---
>
> **§3f(A) (DONE 2026-06-04) — END-TO-END VALIDATION of the language surface
> with a real program.  NO CODE CHANGE (validation); reproducer
> `build/mp-integ.py` (untracked, build/).**
>
>  - Wrote a non-trivial 1.5 KB integration program: an RPN calculator
>    (tokenize via `str.split`, eval on a list-stack, `apply_op` helper) +
>    `dict` operator table + a `Shape→Square→Box` class hierarchy with
>    `super`-style `Square.area(self)` calls + a list comprehension + a
>    generator + a dict word-frequency counter.  Exceptions exercised
>    REALISTICALLY: `7 0 /` raises a custom `CalcError` and `2 3 bad` raises
>    a VM-internal `ValueError` (`int("bad")`), BOTH caught by `run()`'s
>    function-frame `try/except` (the §3e fix in a real program).
>  - **Ran byte-identical to host on the real Victor** (240 s budget): `7 /
>    14 / 5 / CALCERR / EXC / areas 41 / sq [0,1,4,9,16,25,36] / gen 55 /
>    the 3 / cat 2 / DONE`, clean `D4 C5`.  NO latent codegen bug surfaced —
>    §3d (Kl fold) + §3e (setjmp locals) are robustly confirmed together.
>  - Used the §3e-fixed mpython.exe (784800 B).  Avoided MINIMUM-ROM-absent
>    builtins (min/max/sum/sorted/enumerate) — manual loops instead; see
>    §3g(B) to enable them.
>
> ---
>
> **§3e-fix (DONE 2026-06-04) — FIXED the function-frame VM-raise exception
> bug.  COMMITTED `b618fbf` (alias.c, mem.c, all.h, setjmp_clobber_probe +
> golden, tools/test-dos.sh).**
>
>  - **Root cause** (cracked via a FAST DOSBox repro, not Victor cycles):
>    QBE treated a non-escaping local alloca as invisible to a `setjmp` call,
>    so `promote` (mem2reg) hoisted it into a callee-saved register AND
>    GCM/store-motion reordered its stores across the setjmp.  `setjmp` is a
>    "returns twice" fn: `longjmp` restores callee-saved regs to their
>    setjmp-time values, so a local modified AFTER the setjmp reverted on the
>    longjmp-return.  MicroPython's VM `exc_sp` (volatile, advanced by
>    SETUP_EXCEPT) thus reverted to its empty pre-push value → the
>    exception_handler saw an empty exc_stack → a C-runtime-raised exception
>    (NameError/etc.) escaped any FUNCTION-frame `except`.  (minic DISCARDS
>    the `volatile` qualifier — see §3f(C) — so even the volatile guarantee
>    was absent.)
>  - **THE FAST REPRO that cracked it** (`build/nlr_mock_probe.c`, seconds in
>    DOSBox vs 7-min Victor cycles): a faithful mock — real setjmp-macro
>    `nlr_push` (`nlr_push_tail(n), setjmp(n->jb)`), `nlr_jump` writing
>    ret_val + longjmp, nested frames, and a `volatile`-style local
>    INCREMENTED between setjmp and the raise (mirroring SETUP_EXCEPT's
>    `++exc_sp`).  Host → CAUGHT; DOSBox → `ESCAPED (exc_sp stale)`.  KEY
>    LESSON: the first mock used a FUNCTION-wrapped setjmp (UB — frame dies on
>    return) and gave a false negative; MicroPython's `nlr_push` is a MACRO so
>    setjmp is at the call site.  And the bug only appeared once the local was
>    MODIFIED between setjmp and longjmp (a value merely set before setjmp
>    survives — that path passed and misled the first two mock iterations).
>  - **THE FIX**: `calls_setjmp(fn)` (alias.c) scans for a call to a
>    "setjmp"-named symbol; `fillalias` then forces EVERY stack slot `AEsc`
>    (escaped) in such a function, so loadopt/GCM/store-motion treat the
>    slots as call-clobbered (no reordering across the setjmp); `promote`
>    bails explicitly (it does not consult escape).  Conservative, like a
>    real compiler — gives the C11 7.13.2.1p3 guarantee to ALL locals in a
>    setjmp fn.  TARGET-GENERAL (not gated on wordsz): a real QBE correctness
>    fix for amd64/arm64/rv64 too.
>  - **Verified**: real-Victor feature probe now reaches `DONE` matching host
>    (`t_bi`'s NameError CAUGHT by `run()`'s function-frame `except Exception`
>    → `ER bi`, was escaping to `DE`).  `make check` green; DOS gate
>    **188→190** (`setjmp_clobber_probe` medium+compact, bug-loud against the
>    pre-fix qbe: `pushed FAIL 0`).  mpython image **779296→784800 B** (+5.5KB
>    from un-promoted setjmp fns; the size fear was overblown — only
>    mp_execute_bytecode + a few nlr users lose promotion).
>
> ---
>
> **§3e-diag (DONE 2026-06-04) — BISECTED the function-frame VM-raise
> exception bug.  NO CODE CHANGE (diagnosis only); the §3e header above has
> the full characterization + next steps.  This block records the evidence.**
>
>  - **Started from §3d's wrong framing** ("`except Exception` doesn't catch
>    a builtin raise").  Disproved it: 5 targeted Victor probes
>    (`build/exc{,2,3,4}-probe.py`, each host-verified to print all-pass
>    first) bisected the real trigger.
>  - **Reproducer ladder + results (real Victor, fixed §3d build):**
>    - exc-probe (all `try` at MODULE level): T1 `except Exception`+Py-raise,
>      T2 `except Exception`+VM-NameError, T3 same across `boom()` call,
>      T4 `except NameError`+VM, T5 `except Exception`+Py-raise across call —
>      **ALL PASS**.
>    - exc2/exc3 (function-level `try`): the FIRST function-frame +
>      VM-NameError case (even SAME-FRAME, no call) **ESCAPED** both its own
>      `except Exception` and a module-level guard `try` → top-level `DE`.
>    - exc4 (function-level, splitting axes): f1 `except Exception`+
>      `raise ValueError` **PASS**, f2 `except ValueError`+raise **PASS**,
>      f3 `except Exception`+VM-NameError **FAIL** (`undefined_xyz`).
>  - **Conclusion**: the discriminator is VM-internal-raise (C `mp_raise` →
>    `nlr_jump`/`longjmp`) vs Python-`raise` (vm.c:267 `RAISE` macro =
>    `nlr_pop(); goto exception_handler`, no longjmp), AND the catching frame
>    being a FUNCTION (nested `mp_execute_bytecode`) rather than the module
>    (outermost).  Module+VM-raise works; function+Py-raise works;
>    function+VM-raise fails.
>  - **Verified-correct-by-inspection** (so the bug is subtle / on-target):
>    nlr_top chaining (nlr.c), exception_handler `exc_sp`/`exc_stack`
>    recovery (vm.c:1397/1464), FAR_SETJMP_EXE asm
>    (libstub_to_exe.py:2272 — BP/SP/SI/DI/BX/CS:IP all saved+restored).
>  - **Bonus confirmation**: `except Exception` + subclass-walk WORKS at
>    function level (f1), so §2h's "exc-obj far-ptr loses segment" is NOT the
>    cause here.  Next session = on-target instrument the longjmp-return path
>    in a function frame (see §3e header).
>
> ---
>
> **§3d (DONE 2026-06-04) — MAPPED THE FEATURE SURFACE + landed a real QBE
> codegen fix that unlocked it.  COMMITTED (fold.c, shift_fold_probe + golden,
> tools/test-dos.sh).**
>
>  - **The headline**: a one-line `fold.c` fix unlocked the ENTIRE feature
>    surface on the real Victor.  `build/mp-feature-probe.py` (run via
>    `VICTOR_SRC=… run-victor-sasi.sh … 220`) now reports: `OK` for int
>    (mul/pow/mod/bit/shl), class+inheritance (inst/inh), str
>    (slice/neg/upper/split/join/find/replace), list (sort/comp),
>    generators (gen), exceptions (exc) — i.e. EVERYTHING except `t_bi`
>    (builtins min/max/abs/sorted/enumerate = `NameError`, OFF at MINIMUM
>    ROM, expected).
>  - **THE BUG (QBE core, not minic)**: `fold.c::opfold` called
>    `foldint(…, w = cls==Kl, …)` where `w` means "fold as a 64-bit op".
>    On i8086 **Kl is 32-bit** (`long` / far ptr = 4 bytes), so a Kl
>    constant shift folded with 64-bit semantics: `(int32_t)0x80000000 >> 1`
>    (a `sar`) saw 0x80000000 as the POSITIVE 64-bit 2147483648 and gave
>    0x40000000 instead of the 32-bit-correct (sign-extended) 0xC0000000.
>    That corrupted MicroPython's `MP_SMALL_INT_MAX` (= `~((mp_int_t)((mp_uint_t)1<<31) >> 1)`),
>    which came out NEGATIVE, so the small-int overflow check
>    `lhs_val > (MP_SMALL_INT_MAX >> rhs_val)` (runtime.c lshift) tripped on
>    EVERY `1 << n` → spurious `OverflowError` → `t_int` aborted the whole
>    probe at the FIRST run.  (REPR_A here = 32-bit `mp_obj_t`=`void*`=far
>    ptr, so small ints are 31-bit, NOT the 15-bit the config comment
>    claims — `1000000` etc. all fit; the "overflow" was pure codegen.)
>  - **THE FIX** (fold.c:220): `foldint(&c, op, cls == Kl && T.wordsz != 2, …)`.
>    Gating `w` on `T.wordsz != 2` makes Kl fold with 32-bit semantics on
>    i8086 across ALL of foldint's width-sensitive ops (shifts + div/rem +
>    int conversions — all genuinely 32-bit for a 32-bit Kl), and is a
>    NO-OP on amd64/host (`wordsz==4`), so `make check` is byte-identical.
>    The fold's 32-bit input casts (`(int32_t)`/`(uint32_t)`) also fix the
>    secondary symptom that minic stores an `unsigned long 0x80000000`
>    literal sign-extended (`storel -2147483648`) — harmless now since the
>    fold truncates inputs to 32 bits.
>  - **Probe**: `shift_fold_probe.c` (+ golden), gate medium+compact.
>    Bug-loud verified: against the pre-fix qbe it FAILS 3/4 incl. the exact
>    `noovf FAIL ovf=1` MicroPython trip; `sar` alone passes (that operand
>    was stored sign-extended so the 64-bit sar's low 32 happen to match).
>    Gate **186 → 188**.  `make check` green; mpython.exe rebuilt 106/106
>    (779296 B, unchanged).
>  - **METHOD LESSON**: the §3d "use Victor for int-range" warning was
>    literal — a 90 s run looked like a parse HANG (stuck at `D1`) but was
>    just slow parse of the 1.8 KB source; 200 s reached the run and
>    exposed the overflow.  Host (64-bit obj_t) ran the probe to `DONE`, so
>    the bug was target-only; bisected via two tiny DOSBox C probes
>    (`build/{shift,sar}_probe.c`) that print each subexpression, NOT more
>    multi-minute Victor cycles.  The .ssa was CORRECT (`sar`/`shr` chosen
>    right by signedness) — the swap was in QBE's constant FOLD, found by
>    reading the folded printf-arg immediates in the .asm.
>  - Two narrow follow-ups (the `except Exception`-escapes-builtin-raise gap
>    and the builtins ROM flag) are written up in the §3e header above.
>
> ---
>
> **§3c (DONE 2026-06-03) — LANDED SEQUENCE SLICING.  Two minic frontend
> fixes + one config flag; slicing verified on the real Victor.
> COMMITTED (minic/minic.y, tools/test-dos.sh, oo_designate_probe).**
>
>  - **The win**: `MICROPY_PY_BUILTINS_SLICE (1)` in
>    `ports/dos8086/mpconfigport.h` (port tree, UNTRACKED) now builds.
>    On the real Victor, `s[0:5]`→`Hello`, `s[7:]`/`s[-5:]`→`World`,
>    `lst[1:3]`→`[20,30]`, `lst[2:]`→`[30,40,50]`, `lst[:2]`→`[10,20]`,
>    `1+2`→`3`, clean `D4 C5`.  Full compact build **106/106 TUs, 0 fail**;
>    image body 757040 / 779296 total (under the ceiling).
>  - **BUG 1 (objslice.c — out-of-order struct designators), as predicted**:
>    `mp_type_slice` lists `.slot_index_unary_op=1` BEFORE `.slot_index_print=2`
>    but print is declared first (lower offset) → the single-pass emitter
>    die()d ("out-of-order designated initializer unsupported").  **FIX**:
>    rewrote `agg_emit_struct` (minic.y) to **two passes** — pass 1 binds
>    each initializer item to a member-indexed slot via C99 cursor semantics
>    (positional advances; `.field=` sets the cursor); pass 2 walks members
>    in declaration order (= ascending offset), gap-coalescing uninitialized
>    members and packing bitfield runs.  In-order inits stay byte-identical
>    (verified: gate 184/184, all existing struct-init probes green).  Both
>    old dies removed.
>  - **BUG 2 (vm.c) — the NEXT_SESSION hypothesis was WRONG**.  It is NOT a
>    nested-designator gap (`mp_obj_slice_t slice = { .base={.type=…}, … }`
>    parses fine, in-order).  Bisected the real trigger to the function
>    *declarator*: `MP_NOINLINE static mp_obj_t *build_slice_…(…)` i.e.
>    `__attribute__((noinline)) static …` — the ATTRIBUTE comes BEFORE the
>    storage class.  minic had `STATIC attrspec …` and bare `attrspec …`
>    but not `attrspec STATIC …`.  **FIX**: added
>    `attr_typed_decl: attrspec STATIC type_and_ident_noattr typed_decl_rest`
>    (the lexer's `pending_static` already gives internal linkage regardless
>    of token order).  111 s/r 0 r/r (no new conflicts).
>  - **METHOD LESSON (cost ~half the session, worth recording)**: do NOT
>    trust the failing-TU + a hand-typed repro of the "obvious" construct.
>    My faithful repro of build_slice (real types, full body) COMPILED — the
>    trigger only appeared with the `__attribute__((noinline)) static`
>    prefix, which the NEXT_SESSION framing never mentioned.  Bisecting
>    needs the EXACT preprocessed token stream: regenerate the `.pp.c` with
>    the real flags, isolate by feature (slice-on vs slice-off diff under
>    matched FAR_DATA), then shrink the failing fragment line-by-line.  minic
>    error line numbers LAG / point near the recovery state, not the faulty
>    token — don't read them literally.
>  - **Probe**: `oo_designate_probe.c` (compact+large): file-scope
>    out-of-order designators, out-of-order bitfield run, partial-init
>    gap-zero, and an attr-before-static pointer-returning fn.  Gate
>    **184→186**.  Port-tree change (`mpconfigport.h`) is UNTRACKED — re-add
>    the one `#define` if the port tree is reset.
>
> ---
>
> # (Prior framing) §3b — DECISION POINT: the i8086 codegen size-shrink vein is now MINED OUT for easy wins. WHY: §2w + §2x were the two big levers (-51520 B, -57712 B). Everything since — §2y (-1952), §2z (-272), §3a (-592) — has been sub-2KB and shrinking. §3a (far-handler `push bx` liveness gating) dropped only ~5% of its target population (25/446 in vm.asm) because BX is CALLEE-SAVE, so a value placed there is almost always live across the far access. The remaining unconditional save brackets are all similarly low-yield or unsafe to gate (see §3a honest-note below). Recommendation for §3b: STOP chasing codegen bytes and spend the session on a NEW capability (the image is content-bound under the ~896KB Victor ceiling — a feature that lets a real program run is worth more than another few hundred bytes). If you still want a codegen win, the only sizeable lever left is `push es` in the 8 far handlers (446 in vm.asm vs the 446 push bx) — but it is NOT a §2w-style localized change: ES must equal DGROUP at every libstub call site (stosb writes ES:DI), so dropping `push es` needs a real "is ES restored to DGROUP before the next call/return" dataflow analysis with high blast radius (ES corruption = silent wrong far writes). That is explicitly a NON-GOAL under the §2w discipline.
>
> # (Prior framing) §3b — DECISION POINT: the i8086 codegen size-shrink vein is now MINED OUT for easy wins. WHY: §2w + §2x were the two big levers (-51520 B, -57712 B). Everything since — §2y (-1952), §2z (-272), §3a (-592) — has been sub-2KB and shrinking. §3a (far-handler `push bx` liveness gating) dropped only ~5% of its target population (25/446 in vm.asm) because BX is CALLEE-SAVE, so a value placed there is almost always live across the far access. The remaining unconditional save brackets are all similarly low-yield or unsafe to gate (see §3a honest-note below). Recommendation for §3b: STOP chasing codegen bytes and spend the session on a NEW capability (the image is content-bound under the ~896KB Victor ceiling — a feature that lets a real program run is worth more than another few hundred bytes). If you still want a codegen win, the only sizeable lever left is `push es` in the 8 far handlers (446 in vm.asm vs the 446 push bx) — but it is NOT a §2w-style localized change: ES must equal DGROUP at every libstub call site (stosb writes ES:DI), so dropping `push es` needs a real "is ES restored to DGROUP before the next call/return" dataflow analysis with high blast radius (ES corruption = silent wrong far writes). That is explicitly a NON-GOAL under the §2w discipline.

> **NOTE ON DIMINISHING RETURNS (now CONFIRMED EXHAUSTED): the two big
> levers (§2w AX/DX brackets, §2x Kl-param copy) are spent.  §2y/§2z/§3a
> were each sub-2KB and the trend is down.  The MEASURE-FIRST rule paid
> off in §3a: recompiling vm.c showed only 25/549 push bx dropped BEFORE a
> full build was spent, correctly predicting a small (~-592 B) aggregate.
> Apply the same gate to any future candidate — if the representative-TU
> drop rate is in the single-digit-percent range, it is not worth a full
> Victor cycle.  Strongly prefer a NEW capability for §3b.**

> **§3b PLAN (next) — prefer a NEW capability over more codegen bytes.
> If codegen is still pursued, the ONLY remaining low-yield-but-safe
> candidates are below; none is expected to beat §3a's -592 B:**
>
>  1. **Kw-param copy** (same shape as §2x but for Kw params).  Kw params
>     currently get a register, so aliasing to a slot may be a WASH or
>     worse — MEASURE before committing.  UNTRIED.
>
>  2. **CX liveness for div/rem `save_cx`** — extend the now-AX/DX/BX
>     tracker to also track CX, then gate §2z's `save_cx` on it.  CX is
>     caller-save (like AX/DX), so the tracker addition mirrors AX/DX
>     exactly (kill on call).  Payoff bounded by div/rem frequency — same
>     population §2z's -272 B came from, so likely <300 B.  UNTRIED.
>
>  3. **`push es` far-handler analysis** — the big-but-unsafe lever above.
>     Only attempt with a proper ES-reaches-call dataflow pass; out of
>     scope for a localized win.
>
> NON-GOALS (unchanged): register-allocator rewrite, general peephole
> framework, near-call conversion, inlining, FAR_DATA/segment-model changes,
> and (NEW) any `push es` drop without a real ES-liveness/reachability pass.
>
> MEASUREMENT LOOP (per win): `make qbe`; `make check` green; `bash
> tools/test-dos.sh` green (**184/184**; the Kl-clobber + structarg/param
> probes are the safety net); recompile one TU with
> `tools/recompile-mp-tu.sh <base> <src>` and eyeball the asm (NOTE:
> recompile-mp-tu.sh needs /tmp/mp_objs.txt for the relink step — gone on
> reboot; a full build regenerates the objs but NOT that file, so the
> relink is skipped; the per-TU asm is still produced and that's what you
> eyeball); then full `tools/build-micropython.sh --model=compact
> --keep-going` for the body bytes (**§2y left it at body 742096 with heap
> 49152**; §2x was 744048, §2w 772064, all at the same heap); then run on
> the real Victor:
>   `VICTOR_SRC=build/mp-test.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 90`
> must still print `[2..37] / 12 / 197 / fib→514229 / 5 / 21`, clean
> `D4 C5`.  **Victor harness gotchas (re-confirmed §2y):** (a) the run is
> deterministic; EMPTY serial output = a host invocation problem, not MAME
> flakiness.  (b) Do NOT pipe the run through `tail` — it buffers ALL
> output until the pipe closes, so you see nothing mid-run and a 0-byte
> result if the wrapper is killed early.  Redirect straight to a file
> (`… > /tmp/victor_out.txt 2>&1`) and `run_in_background: true`; MAME
> under -nothrottle for 90 emulated sec takes SEVERAL real minutes, longer
> than a 300s monitor window, so wait for the task completion notification
> (not the monitor) and read the file.  Commit qbe-repo files at the green
> milestone; the MicroPython port tree is untracked (note port-tree
> changes in this file).
>
> ---
>
> **§3a (DONE 2026-06-03) — i8086 far-handler `push bx` liveness gating.
> ONE codegen win, fully verified.  COMMITTED (i8086/emit.c).**
>
>  - **The win**: the 8 far load/store handlers (`Oloadf{b,h,w,l}`,
>    `Ostoref{b,h,w,l}`) bracket their ES:BX access with `push bx … pop bx`
>    because BX is the offset scratch and rega doesn't model that clobber
>    ([[i8086-farptr-bx-clobber]]).  That bracket fired UNCONDITIONALLY.
>    Now it is gated on BX-liveness-after (and skipped when the load's `to`
>    IS BX, since the handler writes the result there after the restore).
>  - **Implementation** (i8086/emit.c only): extended `compute_axdx_liveafter`
>    to also fill a `la_bx` buffer + new `g_live_bx_after` global, driven
>    the same way as AX/DX.  **KEY DIFFERENCE: BX is callee-save**
>    (`i8086_rclob`), so a value in BX SURVIVES a call — the tracker does
>    NOT kill BX on `iscall` (it does kill AX/DX).  New helpers
>    `farptr_save_bx`/`farptr_restore_bx` replace the raw `push bx`/`pop bx`
>    in all 8 handlers.  STRICT over-approximation, same safety class as
>    §2w (can never drop a needed save).
>  - **Results**: compact image body **741824 → 741232 B (-592)**; heap
>    stays 49152 (segment-bound).  `make check` green; DOS gate
>    **184/184**; real-Victor mp-test.py output unchanged
>    (`[2..37]/12/197/fib→514229/5/21`, clean `D4 C5`).
>  - **Honest note (drives the §3b STOP recommendation)**: only **25 of
>    549** `push bx` in vm.asm dropped (~5%), because BX is callee-save and
>    therefore almost always live across a far access (it holds long-lived
>    loop/temp values precisely because rega chose a callee-save reg).  The
>    MEASURE-FIRST rule flagged this before the full build was spent.
>    Green/sound/measured, so banked — but this confirms the easy-codegen
>    vein is mined out.  The bigger neighbouring lever (`push es`, equal
>    population) is unsafe to gate without a real ES-reaches-call pass
>    (ES must be DGROUP at libstub call sites; stosb writes ES:DI).
>
> ---
>
> **§2z (DONE 2026-06-03) — i8086 div/rem AX/DX save-bracket liveness
> gating.  ONE codegen win, fully verified.  COMMITTED `ec4adb0`
> (i8086/emit.c).**
>
>  - **The win**: the Odiv/Oudiv/Orem/Ourem handler (~line 2463) bracketed
>    its libstub soft-divide call (`call _qbe_{div,rem}32{s,u}`) with
>    `push ax/cx/dx … pop dx/cx/ax`, gated ONLY on whether the dst lived
>    in that reg (`!dst_in_ax`, etc.).  Now `save_ax`/`save_dx` are also
>    ANDed with §2w's `g_live_ax_after`/`g_live_dx_after`, so the bracket
>    is dropped where rega has no live temp in AX/DX after the op.  Slot
>    destinations with no other live AX/DX value drop their `push ax`
>    (verified in objint.c's base-conversion loop: rem32u/div32u sites now
>    push only `cx`+`dx`).
>  - **CX left dst-gated**: §2w's tracker models only AX/DX, so `save_cx`
>    stays `!dst_in_cx` (candidate §3a.2 to extend).
>  - **SOUNDNESS**: strict over-approximation, identical to §2w — the
>    liveness flags can only ever say "more live than reality", never
>    less, so a needed save is never dropped.
>  - **Results**: compact image body **742096 → 741824 B (-272)**; heap
>    stays 49152 (segment-bound).  `make check` green; DOS gate
>    **184/184**; real-Victor mp-test.py output unchanged
>    (`[2..37]/12/197/fib→514229/5/21`, clean `D4 C5`) — the rem-heavy
>    `is_prime` (`n % i == 0`) path confirms no regression.
>  - **Honest note**: smallest win of the series.  Most div/rem sites keep
>    AX live (the DX:AX result is often consumed immediately), so the
>    bracket only drops at slot-dest sites with no competing live AX/DX.
>    Green/sound/measured, so banked — but the easy size levers are now
>    largely spent (see the diminishing-returns note above).
>
> ---
>
> **§2y (DONE 2026-06-03) — i8086 redundant-arg-marshal elimination.  ONE
> codegen win, fully verified.  COMMITTED (i8086/abi.c, minic/dos/libstub.asm).**
>
>  - **The win**: selcall writes each call's args into the shared arg-slot
>    region at the bottom of the frame.  When two calls in the same block
>    pass the IDENTICAL value at the IDENTICAL slot+width with no other
>    store touching those words between them, the second marshal is dead —
>    the slot already holds the value.  `_mp_hal_stdout_tx_str` (str passed
>    to both `far_strlen` and `tx_strn_cooked`) dropped its second
>    `mov ax,[bp+6];mov dx,[bp+8];mov [bp-12],ax;mov [bp-10],dx` block.
>  - **Implementation**: new `dedup_arg_stores(fn)` in i8086/abi.c, called
>    at the end of i8086_abi (abi1, post-SSA so an RTmp source is
>    single-assignment / stable).  Per-block forward scan; per arg-slot
>    WORD it tracks the last (store-op, src-ref); a candidate arg store
>    (isstore + RSlot dest with `0 <= rsval < fn->arg_slot_top`) is dropped
>    iff every word it covers already holds the identical (op, src).  Any
>    store overwrites the tracked words (defeating a later spurious match);
>    tracking resets per block.
>  - **SOUNDNESS** rests on ONE closed-world invariant: an intervening
>    call does NOT write the arg slots passed to it.  Verified true here —
>    minic copies each incoming param into a fresh local alloca and never
>    writes the incoming `[bp+N]` slot (the `%str=alloc4 + storel %t0`
>    shape), and every hand-written libstub / libstub_to_exe helper reads
>    its stack args into registers (grep confirmed: no `mov [bp+N],…` in
>    either).  So a call clobbers caller-save REGISTERS but never
>    caller-frame arg-slot MEMORY; we deliberately do NOT invalidate
>    tracking on a call.  A warning documenting this invariant was added to
>    the top of minic/dos/libstub.asm (a future helper that writes an
>    incoming arg slot in place would silently break the next call).
>  - **Results**: compact image body **744048 → 742096 B (-1952)**; heap
>    stays 49152 (segment-bound, not load-bound — freed bytes become image
>    margin under the ~896KB Victor ceiling).  `make check` green; DOS gate
>    **184/184**; real-Victor mp-test.py output unchanged
>    (`[2..37]/12/197/fib→514229/5/21`, clean `D4 C5`).
>  - **Honest note**: the win is an order of magnitude smaller than §2w/§2x
>    — the redundant-marshal idiom is locally visible but rare in aggregate.
>    Still green/sound/measured, so banked.
>
> ---
>
> **§2x (DONE 2026-06-03) — i8086 Kl-parameter materialization-copy
> elimination.  ONE codegen win, fully verified.  COMMITTED (i8086/abi.c,
> i8086/emit.c, spill.c).**
>
>  - **The win**: every function with a Kl (far-pointer / `long`) parameter
>    copied it from its incoming ABI stack slot (`[bp+6]`) into a fresh
>    below-BP forced-Kl slot (`[bp-14]`) at entry — `mov ax,[bp+6];mov
>    dx,[bp+8];mov [bp-14],ax;mov [bp-12],dx` — then read it from there.
>    The copy is dead: a param SSA temp is never reassigned (minic mutates
>    params through a separate alloca), so `[bp+6]` always holds the passed
>    value.  Now the param temp is ALIASED to its incoming ABI slot, so the
>    materialization load becomes a no-op self-copy that emit elides; every
>    use reads `[bp+6]` directly.
>  - **Implementation** (3 files): (1) `spill.c` — a pre-pass inside the
>    existing `force_kl_slot` block scans `fn->start` for the selpar pattern
>    `%t =l load SLOT(s)` with `s < 0` (a negative slot only ever names a
>    read-only incoming param) and pre-sets `tmp[%t].slot = s`, so the
>    following `slot()` loop reuses it instead of carving a fresh below-BP
>    slot.  (2) `i8086/emit.c` — the Oload Kl handler elides an
>    `Oload Kl SLOT(s) <- SLOT(s)` when `s < 0` (reading and writing the
>    same param memory is a no-op; the `s < 0` gate keeps it away from the
>    spilled-Kl-ptr deref case at slot idx >= arg_slot_top).  (3)
>    `i8086/abi.c` — comment only.  **KEY GOTCHA (cost a debug cycle):** the
>    alias MUST be set in spill.c (after isel), NOT in abi.c (before isel).
>    i8086 isel's `fixarg` (i8086/isel.c:57) overloads a non-(-1)
>    `tmp[].slot` to mean "this temp is a fast-local alloca whose value is
>    its slot ADDRESS" and would materialize `addr S-3` (i.e. `&param`,
>    `lea [bp+6];ss`) instead of the param value.  Setting the slot only
>    after isel avoids that collision.
>  - **Results**: `_f`-shaped probe `sub sp,14`→`sub sp,6` and the 4-instr
>    copy gone; compact image body **801760 → 744048 B (-57712)**.  `make
>    check` green; DOS gate **184/184** (structarg/param/Kl-clobber probes
>    green = no ABI/aliasing regression); real-Victor mp-test.py output
>    unchanged (`[2..37]/12/197/fib→514229/5/21`, clean `D4 C5`).
>  - **No heap bonus this time**: heap stays 49152 — it is already at the
>    compact-model ~64 KB single-segment ceiling (NOT the DOS load
>    ceiling), so freed code bytes become image margin under the ~896KB
>    Victor limit, not more heap.  See `ports/dos8086/mpconfigport.h:89`.
>
> ---
>
> **§2w (DONE 2026-06-03) — i8086 dead AX/DX save-bracket elimination via
> conservative liveness.  COMMITTED `9a32707` (i8086/emit.c).**
>
>  - **The win**: the `push ax/push dx … pop dx/pop ax` brackets around every
>    Kl op and 32-bit copy fired UNCONDITIONALLY, even where AX/DX hold
>    nothing live.  New `compute_axdx_liveafter()` does a per-block backward
>    scan of physical-AX/DX liveness; the six save-bracket sites gate
>    push/pop on it.  STRICT over-approximation (can't reintroduce the
>    clobber bugs).  `push ax` brackets 64702→28720 (-56%); body 823584 →
>    772064 B (-51520).  Heap bonus: 19456→49152 (2.5×, segment-bound).
>
> ---

_Older session headers (§2u and earlier) were moved to [`SESSION_LOG.md`](./SESSION_LOG.md). See there for the full history._
