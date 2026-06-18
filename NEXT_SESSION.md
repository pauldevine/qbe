# Next session (the §8z handoff GATED 2 of 6 hardware dos_tests on MAME and FIXED a real QBE inline-asm-clobber codegen bug, leaving consumer-driven follow-ups; the user (AskUserQuestion) chose **(1) the FILE-SCOPE function-pointer VARIABLE grammar**.  §9a [2026-06-18, this session] **CLOSED the file-scope function-pointer VARIABLE grammar hole — `void (*v)(void);` (plus the `static` and function-address-initialized forms) at file scope was a hard parse error; minic now parses all four forms; the fix is a frontend `minic.y` change → no emit audit; test-dos 402 → 407; conflicts 115 → 117 (justified — see below); MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  ROOT CAUSE: `typed_decl` (the `prog`-level non-extern declaration) begins with `type_and_ident` (= `type IDENT`), which a `type '(' '*' IDENT ')' ...` declarator can NEVER match; only the EXTERN (`extern int (*cb)(int,int);`), TYPEDEF (`typedef void (*fp_t)(void);`), and function-scope (`dcls` / statement) fn-ptr forms had productions, so a plain file-scope DEFINITION had none.  **THE FIX (frontend `minic.y`, additive):** a new `emit_global_fnptr(name, base, fptpar, init, is_static)` helper (placed next to `emit_global_sym_init`) emits a zero- or symbol-initialized DATA global (`{ w 0 }` / `{ w $foo }` near; `{ l $foo }` far — the far code-pointer static init is split into offset+segment words by `asm_to_omf.py`, the §6k/§7h `split_sym_long` path, confirmed in the `.omf.asm` nasm input as `dw _foo+0 / dw seg _foo`) and records the fn-ptr prototype id via `varsetfpid(name, fpproto_alloc(base, fptpar))` so an indirect call coerces its arguments; `is_static` retro-marks the slot internal via `glo_mark_static_range` (no `.globl`).  A new `gfnptrdcl` nonterminal is wired into `prog` with FOUR productions — `{plain, STATIC} × {';' , '=' expr ';'}`; the initializer runs through `cival_eval` (a bare function name decays to its `$sym` address, case 'V').  **CONFLICTS 115 → 117 (justified, NOT a new conflict KIND):** both new conflicts are the IDENTICAL pre-existing `IDENT → reduce attrreset` shift/reduce conflict (baseline already carries 1; mine carries 3), which yacc default-resolves by SHIFT (the correct `type IDENT` path).  Adding the `type '('` / `STATIC type '('` productions forces the LALR builder to DUPLICATE the `type .` / `STATIC type .` item-sets into the new file-scope fn-ptr context, and each duplicate carries that same already-accepted benign conflict; the `gfnptrdcl` decision itself (shift `(`) is fully unambiguous.  Verified by diffing the `y.output` conflict descriptions (token + reduce-rule NAME, stable across state renumbering): the ONLY delta is the `attrreset` IDENT-conflict count 1 → 3.  **GATED bug-loud** by `minic/dos/examples/file_fnptr_probe.c` (small+medium+compact+large+huge): the UNFIXED compiler hits a `parse error` at the first file-scope fn-ptr declaration so the program does not even build (confirmed by `git stash`-ing the `minic.y` fix); it computes sums / dispatch results (not pointer addresses), so the golden is model-independent (near-code small/compact vs far-code medium/large/huge); it covers plain + static × uninitialized + function-address-initialized, proving `static` emits as plain `data` (internal linkage) and still dispatches.  All five models produce byte-identical output; the golden ends `done\n` (no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **407/407 ok** (the 5 new probe entries, 402 → 407, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (MP has no file-scope fn-ptr variables — they were parse errors — so the new branches never fire → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = `emit_global_fnptr` helper + `gfnptrdcl` nonterminal/4 productions + the `prog` wiring; new `minic/dos/examples/file_fnptr_probe.c` + `minic/dos/tests/file_fnptr_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **⇒ Next session — the file-scope fn-ptr-variable hole is CLOSED; all remaining follow-ups are consumer-driven (pick with the user):** (1) merge newlibc **PR #24** (`minic-dostest-hw-gate` — the §8z `_dos_getvect`/`_dos_setvect`/`_chain_intr` intrinsics + `clock()` fill) into `victor9K_newlibc` main; (2) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (3) IF Victor-native timer/keyboard coverage is wanted, author INT 1Ah/16h-free DOS-hosted timing/keyboard probes — but the §8l/§8n bare-metal pattern already covers that ground, so DOS-hosted versions add little.  NO QBE/minic codegen bug open; NO carried compiler track remains.  Note on the file-scope fn-ptr feature's bounds: it covers the single-declarator plain/`static` forms with an optional function-address initializer; a `__far`/attribute-qualified pointee (`void __far (*v)(void)` at file scope) or a multi-declarator comma form (`int (*a)(int), (*b)(int);`) is not handled — no consumer, and the §8r `fpquals` pattern is the template if one appears.)

## §9a session notes (2026-06-18)

### The pick
- §8z handoff: 2/6 hardware dos_tests MAME-gated + the inline-asm-clobber QBE
  fix; no open compiler bug, no carried track.  User (AskUserQuestion) chose
  **(1) the FILE-SCOPE function-pointer VARIABLE grammar** from the follow-ups.

### Root cause
- `void (*v)(void);` at file scope → `parse error`.  `prog` reduces
  `typed_decl: type_and_ident typed_decl_rest`, and `type_and_ident` is
  `type IDENT` — a `type '(' '*' IDENT ')' ...` declarator never matches it.
  Only EXTERN (7669), TYPEDEF (7783), and function-scope (`dcls` 9291 /
  statement 9788) fn-ptr forms had productions.  No file-scope variable form.

### The fix (frontend minic.y, additive)
- New `emit_global_fnptr(name, base, fptpar, init, is_static)` near
  `emit_global_sym_init`: emits a zero-init (`emit_zero_init`) or symbol-init
  (`cival_eval` → `{ c $sym }`) DATA global of type `IDIR(FUNC(base))`,
  `varsetfpid(... fpproto_alloc(base, fptpar))`, `glo_mark_static_range` when
  static.  Near = `{ w 0 }`/`{ w $foo }`; far = `{ l $foo }`, split into
  offset+seg words by asm_to_omf.py (§6k/§7h), confirmed in the .omf.asm.
- New `gfnptrdcl` nonterminal in `prog`, 4 productions:
  `type '(' '*' IDENT ')' '(' fptpar0 ')' ';'` and the `'=' expr ';'` form,
  plus the two `STATIC ...` siblings.

### Conflicts 115 → 117 (justified)
- Both new conflicts are the SAME pre-existing `IDENT [reduce attrreset]`
  shift/reduce conflict (default-resolved by shift), duplicated because
  `type '('` / `STATIC type '('` split the `type .` / `STATIC type .`
  item-sets into the new context.  Verified via a `y.output` conflict-desc
  diff (token + reduce-rule NAME, renumber-stable): the only delta is the
  attrreset count 1 → 3.  The gfnptrdcl decision (shift `(`) is unambiguous.

### Gate + validation
- `file_fnptr_probe.c` (5 models): plain/static × uninit/func-init; sums &
  dispatch (model-independent golden, ends `done\n`).  Bug-loud: stashing the
  minic.y fix → `error:40: parse error` at the first fn-ptr decl, build fails.
- `make check` green; test-dos 402 → 407; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (emit_global_fnptr + gfnptrdcl + prog wiring),
  minic/dos/examples/file_fnptr_probe.c, minic/dos/tests/file_fnptr_probe.golden.txt,
  tools/test-dos.sh (+5 entries).  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- (1) merge newlibc PR #24 (minic-dostest-hw-gate);
- (2) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (3) Victor-native timer/kbd DOS probes if wanted (§8l/§8n already cover it).
- NO QBE/minic codegen bug open; NO carried compiler track remains.
- Bounded (no consumer): __far/attr-qualified or multi-declarator file-scope
  fn-ptr vars (the §8r fpquals pattern is the template).
---

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

Older session headers (§8y and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
