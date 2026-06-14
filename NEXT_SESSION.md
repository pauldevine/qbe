# Next session (§7b — continue Phase 6 / open compiler tracks.  §7a [2026-06-13, this session] implemented **near (small/tiny-model) `setjmp`/`longjmp`** — the carried "small setjmp/longjmp (newlibc may want it)" open track, chosen by the user.  Until now the small `.EXE` model had NO setjmp at all: `tools/libstub_to_exe.py`'s `build_epilogue()` DROPPED `SETJMP_EXE` for near-code models (tiny/small) because that helper is structurally FAR — its `jmp_buf` saves a 4-byte CS:IP return address and `longjmp` exits via `retf` — and it CANNOT be produced by `unfar_epilogue()` (the `retf→ret` / `[bp+N≥6]−2` reverse transform the other EXE epilogue blocks use), because that transform drops 2 from EVERY `[bp+N≥6]`, which would silently corrupt the `jmp_buf` INTERNAL offsets `[bx+10]`/`[bx+12]` along with the call-frame offsets.  So **any small-model program that referenced `setjmp`/`longjmp` failed to LINK** — confirmed bug-loud: `tools/build-example.sh --model=small minic/dos/examples/setjmp_probe.c` → `omf_link: error: undefined symbols: _setjmp, _longjmp`.  **The fix is a new hand-written `NEAR_SETJMP_EXE` string** in `libstub_to_exe.py`, mirroring the proven medium `SETJMP_EXE` with the CS word removed: a near `call` pushes only a 2-byte return IP, so the frame at setjmp entry is `[bp+0]` saved BP / `[bp+2]` ret IP / `[bp+4]` env (one word lower than the far form's `[bp+6]` after the extra CS word), the caller's resume SP is `lea [bp+4]`, the `jmp_buf` is 6 words (`[0]` BP, `[2]` resume SP, `[4]` SI, `[6]` DI, `[8]` BX, `[10]` ret IP — NO CS word; the C `jmp_buf` is `int[8]`=16 B so `[12]`/`[14]` stay spare), and `longjmp` restores SP, pushes the IP only, and exits via a near `ret` (vs the far form's push-CS+IP / `retf`).  Near-data (DS==SS) reaches a stack-allocated env via DS:BX — no ES involved (so it is simpler than even the medium near-DATA `SETJMP_EXE`, which still used the far call ABI).  It is authored directly in near ABI / `segment _TEXT` and appended **raw** to the `near_code_model` branch of `build_epilogue()` (NOT through `unfar_epilogue`, precisely to avoid the `[bx+N]` corruption described above).  **The two existing setjmp probes were reused as the gate** — both are model-independent (program output only), so no new probe/golden was authored: `setjmp_probe.c` (case 1 direct=0, case 2 `longjmp(env,7)`, case 3 the C `0→1` fixup, cases 4/5 a DEEP 3-frame nested unwind via an NLR clone + callee-saved BX/SI/DI/BP guard restore, case 6 chained-buffer NLR popping to the right level) and `setjmp_clobber_probe.c` (the `calls_setjmp()`-forces-AEsc guard: a local modified AFTER setjmp must survive the longjmp).  Both build and run **byte-exact vs their existing goldens under small** in DOSBox — proving the resume-SP arithmetic, the near `ret` target, and the callee-saved-register save/restore are all correct.  Wired `:small` entries for both into `tools/test-dos.sh` (alongside their existing medium/compact/large entries): **test-dos 298 → 300, all [ok]**.  This is a `libstub_to_exe.py` (toolchain) change, NOT an i8086/emit.c change, so per house rules **no emit-bracket audit was required**; the required check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088) — as expected since the change touches ONLY the `near_code` branch and MP builds compact (far-data), which never hits it → codegen unchanged, no Victor run needed.  The track note "small-model setjmp/longjmp — only if a small-model consumer needs it (newlibc may)" is now CLOSED: the capability exists and is gated; if/when a small-model newlibc consumer appears (e.g. an NLR-using test that fits the 64 KB single-`_TEXT` ceiling), `setjmp`/`longjmp` resolve by real name (near-data models do not `far_stdlib`-mangle, so minic calls `setjmp`→asm `_setjmp` directly).  Next: pick another carried compiler track (huge `_qbe_huge_add` ≥0x8000 §4i; multi-decl items after the first skip `block_scope_decl`; far static-DATA-ptr reloc §1g; param/static-local shadowing a global; Kw spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp §4v — unreduced, reduce first) OR resume Phase-6 newlibc gating (`serial_loopback_test` is the only remaining tractable bm_testhost candidate but needs real new harness plumbing — an rs232a TXD→RXD loopback attach distinct from the §6e `V9K_SERIAL_IN` rs232b null_modem, gate serial capture moved to channel B, plus RX-timing determinism on the 5 MHz 8088; `interrupt_test` stays SKIPPED per §6v).)

## §7a session notes (2026-06-13)

### The gap: small/tiny model had no setjmp at all (link failure)
- `libstub_to_exe.py build_epilogue()` DROPPED `SETJMP_EXE` for near-code
  models (tiny/small) — it is structurally FAR (jmp_buf saves 4-byte CS:IP,
  longjmp exits via `retf`) and `unfar_epilogue()` CANNOT convert it: that
  transform drops 2 from EVERY `[bp+N>=6]`, which would corrupt the jmp_buf
  INTERNAL offsets `[bx+10]`/`[bx+12]` along with the frame offsets.
- So any small-model program referencing setjmp/longjmp failed to LINK.
  Bug-loud confirmed: `build-example.sh --model=small setjmp_probe.c` →
  `omf_link: error: undefined symbols: _setjmp, _longjmp`.

### The fix: hand-written NEAR_SETJMP_EXE (libstub_to_exe.py)
- Mirrors the medium `SETJMP_EXE` with the CS word removed (near `call`
  pushes only a 2-byte IP):
    - frame at setjmp entry: `[bp+0]` BP, `[bp+2]` ret IP, `[bp+4]` env
      (one word lower than the far `[bp+6]`); resume SP = `lea [bp+4]`.
    - jmp_buf (6 words): `[0]` BP, `[2]` resume SP, `[4]` SI, `[6]` DI,
      `[8]` BX, `[10]` ret IP — NO CS word (C jmp_buf is int[8]=16B, so
      `[12]`/`[14]` spare).
    - longjmp: restore SP, push IP only, near `ret` (vs far push-CS+IP /
      `retf`).  Near-data DS==SS reaches env via DS:BX — no ES.
- Authored directly in near ABI / `segment _TEXT`, appended RAW to the
  `near_code_model` branch of `build_epilogue()` (NOT via `unfar_epilogue`,
  to avoid the `[bx+N]` corruption above).

### Gate (bug-loud) + toolchain checks
- Reused the two existing model-independent setjmp probes (no new
  probe/golden): `setjmp_probe.c` (direct=0, val=7, 0->1 fixup, deep 3-frame
  nested unwind + callee-saved guard restore, chained-buffer NLR pop) and
  `setjmp_clobber_probe.c` (calls_setjmp AEsc guard).  Both byte-exact vs
  their goldens under small in DOSBox.
- Wired `:small` entries for both into `tools/test-dos.sh`.
- **test-dos 298 -> 300** (both new small entries [ok], all prior unchanged).
- `libstub_to_exe.py` change (NOT emit.c) -> NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  documented golden (only the `near_code` branch changed; MP is
  compact/far-data) -> codegen unchanged, NO Victor run.

### Closed track
- "small-model setjmp/longjmp (newlibc may want it)" is CLOSED: capability
  exists + gated.  Near-data models don't `far_stdlib`-mangle, so a future
  small-model newlibc consumer calls `setjmp`->asm `_setjmp` by real name.

### Open tracks (carried)
- Compiler: huge `_qbe_huge_add` >=0x8000 (§4i); multi-decl items after the
  first skip `block_scope_decl`; far static-DATA-ptr reloc (§1g);
  param/static-local shadowing a global; Kw spill-slot sharing; `jmp_buf
  bufs[6]` cross-frame longjmp (§4v, unreduced — reduce first).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console +
  rs232a TXD→RXD MAME loopback device colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.

---

# Next session (§7a — continue Phase 6 / open compiler tracks.  §6z [2026-06-13, this session] fixed a minic **front-end parse bug: `const`/`volatile`-qualified FLOATING-point declarations did not parse** — `const float`, `const double`, `volatile float`, `volatile double`, and `const volatile double` were hard `parse error`s at any scope, while bare `float`/`double` and `const int` always worked.  This was the carried open track listed as "minic static-init FLOAT const-expr folding (`static float x = 2.0f*3.14f;`) — also unlocks MICROPY_PY_MATH_CONSTANTS", but the diagnosis in that note was WRONG: **const-expr folding was never broken** — `2.0f*3.14f`, `3.14159/2.0`, `6.0f/2.0f`, etc. already fold to a single-precision `data` constant.  The real defect was purely in the `type` grammar (minic/minic.y ~line 8484): it enumerates `CONST TINT`/`CONST TCHAR`/`CONST TLNG`/… and the parallel `vol_qual T…` integer cases, but **omitted the floating forms** — there was no `CONST TFLOAT`/`CONST TDOUBLE` nor `vol_qual TFLOAT`/`vol_qual TDOUBLE` production, so the parser had no action for `const`/`volatile` followed by `float`/`double` and died.  **The fix is four new grammar productions**, each mapping (exactly like the bare `TFLOAT`/`TDOUBLE` rules at lines 8460–8461) to `INT | FLOAT` — double aliases to single-precision (Ks) on i8086 — with the `vol_qual` pair additionally OR-ing `QVOLATILE` and setting `g_decl_volatile = 1`, mirroring every other `vol_qual T…` rule (`const` adds nothing in minic; `volatile` drives the QVOLATILE machinery exactly as the integer cases do).  **No semantic/codegen surface changed** — these productions only fire on token sequences that previously had NO valid parse, so every input that already parsed produces an identical AST.  **Grammar conflict count UNCHANGED**: 115 shift/reduce, 0 reduce/reduce, and "10 rules never reduced" is the pre-existing baseline (verified by stashing the change and rebuilding).  **Gated bug-loud** with a new `minic/dos/examples/const_float_init_probe.c` (+ `minic/dos/tests/const_float_init_probe.golden.txt`), wired into `tools/test-dos.sh` at MEDIUM + COMPACT with `--softfloat` (added to both the runtime-case list and the `sfflag` basename `case`, alongside the sibling float probes): it declares the previously-unparseable forms at file scope — `static const float pi`, `static const double e`, const-expr folds behind a const qualifier (`static const float twopi = 2.0f * 3.14159…f`, `static const float half = 3.14159… / 2.0`), a `static const float tbl[3]` array, a non-static `const float gquarter` (external linkage → `export data`), a `static volatile float vf`, and a `static const volatile double cvd` — printing each value's exact IEEE-754 single bit pattern through a `union { float; unsigned long; }` (the `float_literal_probe` idiom, so the golden is exact with no float printf).  Verified bug-loud: the UNFIXED minic (git stash) errors `error:41: parse error` on the very first `static const float` line.  **test-dos 296/296 → 298/298** (the two new MEDIUM+COMPACT entries `[ok]`, every prior entry unchanged).  Since this is a minic.y grammar change (NOT i8086/emit.c), the emit-bracket audit is NOT required; the required toolchain check is the MP byte-compare, and **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming codegen is unchanged → no Victor run needed.  **Bonus**: this removes the front-end blocker for `MICROPY_PY_MATH_CONSTANTS` (its `const float` definitions of M_PI/M_E now parse) — but MP is PARKED as a byte-compare corpus (the math-constants memory note still says keep it 0), so that feature was NOT turned on; the relevant carried open track is now CLOSED/CORRECTED.  Next: pick another carried compiler track (small setjmp/longjmp — newlibc may want it; huge `_qbe_huge_add` ≥0x8000 §4i; multi-decl block_scope_decl; far static-DATA-ptr reloc §1g; param/static-local shadowing a global; Kw spill-slot sharing) OR resume Phase-6 newlibc gating — `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX in bm_console + an rs232a TXD→RXD MAME loopback device, which collides with the rs232a `null_modem` capture so the gate's serial capture must move to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED (§6v `[90,110]` FAIL-window + raw iteration count).)

## §6z session notes (2026-06-13)

### The bug: missing CONST/vol_qual TFLOAT|TDOUBLE grammar productions
- minic's `type` nonterminal (minic/minic.y ~8484) enumerates `CONST T…`
  and `vol_qual T…` for every INTEGER base type but had NO floating forms.
- So `const float`/`const double`/`volatile float`/`volatile double`/
  `const volatile double` were hard `parse error`s — at file scope, local
  scope, anywhere.  Bare `float`/`double` and `const int` always parsed,
  which masked it.
- The carried-track note "minic static-init FLOAT const-expr folding" was a
  MISDIAGNOSIS: folding works (`2.0f*3.14f` → a single-precision `data`
  constant already).  The defect was purely the missing qualifier+float
  grammar rules.

### The fix: four additive productions, semantics-neutral
- Added `CONST TFLOAT`/`CONST TDOUBLE` → `INT | FLOAT` (matching bare
  TFLOAT/TDOUBLE at lines 8460–8461; double aliases to single Ks on i8086).
- Added `vol_qual TFLOAT`/`vol_qual TDOUBLE` → `INT | FLOAT | QVOLATILE`
  with `g_decl_volatile = 1`, mirroring the integer `vol_qual T…` rules.
- Purely additive: fires only on token sequences that previously had no
  valid parse, so all previously-parsing input yields an identical AST.
- Grammar conflicts UNCHANGED (115 s/r, 0 r/r, 10-rules-never-reduced is
  the pre-existing baseline — confirmed by stash + rebuild).

### Gate (bug-loud) + toolchain checks
- `minic/dos/examples/const_float_init_probe.c` + golden, wired into
  `tools/test-dos.sh` MEDIUM + COMPACT with `--softfloat` (runtime-case
  list AND the `sfflag` basename `case`).  Prints exact IEEE single bit
  patterns via a float/ulong union (float_literal_probe idiom).
- Bug-loud verified: unfixed minic (stash) → `error:41: parse error` on the
  first `static const float` line.
- **test-dos 296 → 298** (both new entries [ok], all prior unchanged).
- minic.y change (NOT emit.c) → NO emit audit required.
- MP compact rebuilt: body EXACTLY **731,088 bytes**, byte-identical to the
  documented golden → codegen unchanged, NO Victor run.

### Bonus / closed track
- Removes the front-end blocker for MICROPY_PY_MATH_CONSTANTS (const-float
  M_PI/M_E now parse), but MP is PARKED (byte-compare corpus; math-constants
  stays 0) so the feature was NOT enabled.  The "static-init FLOAT
  const-expr folding" open track is now CLOSED/CORRECTED.

### Open tracks (carried)
- Compiler: small setjmp/longjmp (newlibc may want it); huge `_qbe_huge_add`
  ≥0x8000 (§4i); multi-decl items after the first skip block_scope_decl; far
  static-DATA-ptr reloc (§1g); param/static-local shadowing a global; Kw
  spill-slot sharing; `jmp_buf bufs[6]` cross-frame longjmp (§4v, unreduced).
- Phase-6 newlibc: `serial_loopback_test` (only tractable bm_testhost
  candidate left, but real harness work — channel-A polled RX in bm_console +
  rs232a TXD→RXD MAME loopback device colliding with the rs232a null_modem
  capture → move gate capture to channel B + RX-timing determinism);
  `interrupt_test` stays SKIPPED; display-only/`hlt`-loop tests already
  covered by hand-mirrored `bm_*` ports; newlibc-under-far-DATA-models
  (compact/large) stdio when a far-DATA consumer appears.


---

Older session headers (§6y and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
