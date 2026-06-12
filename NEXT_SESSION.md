# Next session (§6b — continue Phase 6.  §6a [2026-06-11, this session] ran the newlibc TRIAGE SWEEP (Phase-6 step 1) and fixed SEVEN minic dialect gaps it exposed: the per-TU sweep (`build/newlibc-triage/sweep.sh` + `shiminc/` newlib-shaped shim headers) took phase3_newlib from **21/66 → 46/66 TUs compiling** under BOTH small and medium, and the **entire portable subset (libgloss ×7, vfs ×2, every non-asm test) now compiles** — exactly the step-2 target population.  Every remaining failure is inline-asm-flavored (drivers + dos_tests) or the ISR-definition gap — i.e. step-4 work, none of it portable-subset.  Six new probes, gate 262→274.  Next: **step 2 proper** — link the portable subset against libstub, run VFS/FAT/printf tests DOS-hosted in DOSBox, gate them; then step 3 (omf_link raw-binary output + minic crt0).)

## §6a session notes (2026-06-11)

### The seven minic fixes (each probe-gated, all loud-verified pre-fix)
1. **`extern T *f(args);`** — extern + pointer-return + ANSI params had NO production
   (only K&R `*f()`); errno.h's `extern int *__errno(void);` killed ~29 TUs at line 1.
   New ext_decl kind `'H'` (par1 stashed on `->l`, fnproto_record'd in the extern walks).
   `extern_ptrret_probe`.
2. **File-scope prototype param leak** — par1's param() varadds names at file scope and
   nothing removed them after a PROTOTYPE; a later decl reusing the name with a different
   type died "double definition" (`int first(char *buf,…); extern long second(…,const
   void *buf,…)`).  Fixed: `varclr()` at the end of every file-scope prototype-only
   reduction (ansi_proto_register, EXTERN par1, both ext_decllist walks).  Definitions
   were always safe (init_ansi/init_kr varclr first).  `proto_param_leak_probe`.
3. **Array parameter declarators** — par1 had no `'['…']'` forms at all: `uint8_t out[11]`,
   `char buf[]`, `char *const argv[]` all parse-errored (the *const was a red herring —
   `type '*' CONST` existed).  Four new par1 productions, decay to (far-aware) pointer,
   dimension folded and discarded.  `array_param_probe`.
4. **`void __far __attribute__((interrupt)) f(void);`** — the ia16-gcc far-ISR spelling
   (interrupts.h) had no production for the interposed `__far`; new
   `type TFAR attropt IDENT` in type_and_ident accepts-and-drops the __far.  PROTOTYPE
   only: ISR *definitions* remain a designed gap (the vestigial interrupt emission
   produces `asm "iret"` with no block terminator — QBE rejects — and would skip the
   epilogue anyway; Phase-6 step 4 decides the real ISR strategy).  `isr_far_attr_probe`.
5. **`const volatile T`** — qualifier pair missing everywhere; new `vol_qual` nonterminal
   (VOLATILE | CONST VOLATILE | VOLATILE CONST) replaced the bare-VOLATILE heads in all
   type productions (incl. STRUCT/UNION/TNAME).  Covered by the font_test-shaped probe
   cases inside array_param/others; no dedicated probe (parse-only, exercised by sweep).
6. **Scalar global symbol-address init** — `char **environ = __env;` / `int *p = &x;` /
   `int *mid = &arr[2];` died (the `'=' expr ';'` rule folded with const_eval only; the
   aggregate path §1b/§1g could already emit `$sym+off`).  Now routed through cival_eval
   → new emit_global_sym_init.  `static_sym_init_probe` (small+medium).  **The probe
   under compact CONFIRMED the §1g far static-DATA-ptr reloc gap at runtime** (prints raw
   offsets 4194/4192 — segment missing) — now a reproducible open track, NOT gated far.
7. **Locals shadow file-scope bindings** — minic had NO local-shadows-global support
   (`int g; int f(){int g;}` died), only §1k local-vs-local inner-block renames.  newlibc
   vfs_open declares `const fat_mount_t *fat_mount;` next to the file-scope function
   fat_mount() (found by automated delta-reduction of the 785-line failing prefix).
   Fixed: block_scope_decl's rename trigger extended to any global/extern/function/enum
   binding + block_scope_decl wired into the dcls-chain local rules (fn-body depth) —
   the stmt-context rules already had it.  `local_shadow_probe` (global var + function +
   enum constant all shadowed, post-shadow global intact).
   Plus: postfix prototype attribute `void _init(void) __attribute__((weak));` (new
   `ansi_proto_register ATTRIBUTE…';'` production), and `die("undefined variable")` now
   prints the NAME (4 shim-gap diagnoses fell out instantly).

### Sweep infrastructure (build/newlibc-triage/, intentionally untracked probe-grade)
- `sweep.sh [model]`: per-TU clang -E (-nostdinc **-D__ia16__** — keeps `__far` real and
  selects the GCC MK_FP branch in v9k_hw.h, which matches minic semantics) → minic →
  qbe → asm_to_omf → nasm, keep-going, stage-bucketed report.
- `shiminc/`: newlib-shaped shim headers (errno/unistd/fcntl/reent/dirent/stdio with
  struct FILE._file/sys/stat with S_IF*+S_BLKSIZE/sys/types with dev_t/time/limits/io/
  conio→dos.h/i86→dos.h).  These prefigure the real newlibc-port headers.
- minic line numbers in errors are 0-based-ish (error:0 = line 1); statements are emitted
  at the function-close reduce, so stmt-level errors report the `}` line — bisect inside.

### Remaining failures (all step-4 flavored, NONE portable-subset)
- 6 dos_tests: Watcom `_asm { … }` blocks (phase-1-style TUs; park or rewrite later).
- 5 qbe-stage: extended-asm `"=r"` OUTPUT constraints — minic substitutes the slot into
  the template but QBE sees a slot that is read-never-stored and rejects; ALSO
  interrupts.c ISR definition (designed gap, see fix 4).
- 8 nasm-stage: AT&T/ia16 mnemonics leak through the template (`pushfw/popw %0`,
  `movw %es`) — nasm wants Intel.  Driver asm will need per-target porting in step 4
  anyway (it was written for ia16-gcc).
- 46/66 small AND medium, identical fail sets.

### Open tracks (new + carried)
- far static-DATA-ptr reloc (§1g, now runtime-reproduced by static_sym_init_probe under
  compact) — needed before newlibc-style tables-of-pointers work in far-data models.
- minic extended-asm output-constraint store marking + Intel-syntax template translation
  (the qbe/nasm buckets above) — step 4.
- ISR definition strategy (minic save-all+iret codegen vs asm shims) — step 4.
- Param/static-local shadowing a global still dies (only plain locals fixed).
- Carried: huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic
  static-init FLOAT const-expr folding; small setjmp/longjmp; multi-decl items after the
  first skip block_scope_decl; Kw spill-slot sharing.

---

# (§6a designation, kept verbatim) Next session (§6a — DESIGNATED by the user 2026-06-11: **the newlibc frontier (Phase 6).**  Get `~/projects/newlibc` — a much-progressed Victor 9000 C library + driver suite — working when built by THIS toolchain, and adopt its test suite as a standing robustness harness; end state retires libstub.  Full plan + survey in ROADMAP.md → Phase 6.  Docs were consolidated this session (ROADMAP.md/CLAUDE.md rewritten, Phases 0–5 marked complete, the CLAUDE.md Prior: chain pruned, session history §3u–§5c-PLAN archived to SESSION_LOG.md).)

## §6 entry notes (gathered 2026-06-11)
- **newlibc survey**: `phase1_dos_drivers/` (DOS-hosted driver validation, OpenWatcom-built), `phase2_baremetal/`, `phase3_newlib/` (PRODUCTION — bare-metal newlib port built with **ia16-elf-gcc**, small model at load addr 0x3000, ~55–62 KB bins; medium secondary).  Phase 3 complete per its docs: crt0 + v9000.ld, libgloss syscalls, integer-only printf/scanf wrappers, sbrk malloc, VFS + read-only FAT12/16 over a SASI driver, CRTC display + loaded fonts (Victor has no char ROM), VIA 6522 keyboard, 8253-ch2-on-IR2 timer (NOT IBM IRQ0), PIC, serial.  **~40 test programs run under MAME victor9k** — that suite is the harness we want.  ~22 K lines / 82 files in phase 3.
- **Known toolchain gaps** (validate by probe, don't assume): minic has no `__attribute__((interrupt))` (ISRs need a minic feature or asm shims); omf_link has no raw-binary output mode (newlibc is bare-metal at a load address, not MZ .EXE); newlib proper (the ia16-built libc.a/libm.a it links) is OUT of near-term scope — target newlibc's own code (libgloss/drivers/VFS/tests) first; the small model (§5c, brand-new) gets its first real consumer and still lacks setjmp/longjmp.
- **Gated bring-up sequence** (detail in ROADMAP.md Phase 6): (1) triage sweep — compile newlibc sources with minic and bucket the failures (the MP-spike per-TU methodology); (2) portable subset (string/printf wrappers, VFS, FAT) DOS-hosted in DOSBox + gate entries; (3) raw-binary omf_link output + minic-built crt0 + MAME bare-metal hello; (4) drivers + ISR strategy; (5) `tools/test-newlibc.sh` standing gate; (6) libstub retirement.
- Rules of engagement unchanged: probe-first; `tools/run-emit-audit.sh` after any emit.c change; MP compact rebuild + body byte-compare after any toolchain change (731,088 is the current baseline).

## Carried open tracks (from §5c; pick by appetite)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic static-init float const-expr folding (also unlocks MICROPY_PY_MATH_CONSTANTS); small-model setjmp/longjmp (newlibc may force this); multi-decl items after the first skip block_scope_decl (loud); Kw spill-slot sharing.
- ~~ROADMAP.md refresh / CLAUDE.md prune~~ — DONE 2026-06-11.

---

# Next session (§5c is DONE 2026-06-11 — all three primary items landed in one session.  (1) **Upstream sync**: merged `c081897..e786f06` (3 commits; `fa19d3c` strict-aliasing fix in emit.c float-constant comments is output-identical) — make check green, gate 254/254, MP body byte-identical.  (2) **THE SMALL MODEL WORKS** (the only flat-out broken model, commit `4cfb321`): `libstub_to_exe.py` gained `near_code_model`/`unfar_epilogue` — small keeps libstub's native near ABI untouched and the EXE epilogue blocks (authored far-ABI) are reverse-transformed (retf→ret, `call far X`→`call X`, `[bp+N≥6]`−2, the printf engines' computed vararg bases `add si,N`−2, LIBSTUB_TEXT→_TEXT); FAR_DOSIO + SETJMP dropped under near-code (unreachable; SETJMP's jmp_buf is structurally far — a near setjmp is an OPEN track); `asm_to_omf.py` emits tiny/small code into shared `_TEXT` (no budget split); **`omf_link.py` now coalesces CODE segments BY NAME like DATA/BSS** (behavior-identical for far models — names unique; under small the three `_TEXT`s merge into ONE paragraph frame so near calls and 16-bit fn ptrs resolve against the runtime CS — fnptrprobe passes, proving it); crt0 `-DNEAR_CODE` → near `call _main`.  6 gate entries: cprobe/cstrprobe/fnptrprobe/mathprobe/dosapi_probe/fileio-roundtrip, all DOSBox-verified; cstrprobe has a small-specific golden (`%p` prints the C-correct 16-bit near ptr `5678`).  Gate 254→260.  (3) **the `float **` collision is FIXED** (commit `bf4a2e3`): FAR 24→26, QVOLATILE 25→27 — FLOAT (18) two shifts up no longer lands on FAR, so `float **` deref/store/param all decode right (pre-fix medium emitted loadl+loadfw+swtof garbage — probe-proven).  PLUS the probe's f3 case found the DIRECT-call sibling of §5b: `DREF(FUNC(ret))` strips ret bits on the flag positions (a direct fn returning `float **` puts FLOAT on new-QVOLATILE 27), so **fnproto gained `rett`/`has_rett`** (the fpproto.rett mirror; recorded at all 6 fnproto_record sites, used at the direct-call decode — layout-independent).  Residual collisions all moved one level deeper and surveyed UNCONSUMED (MP+stevie+probes): `unsigned T ***` (17+9=26), `float ***` (18+9=27), and far-data nested-far depth is now exactly ONE level (`T **` ok; T***'s innermost FAR needs bit 32 — the probe's short*** case under compact CONFIRMED the documented trade, reduced to short**).  Probe `float_dblptr_probe.c` (medium+compact, bug-loud).  Gate 260→**262/262** with every pre-existing golden unmoved; conflicts unchanged 115 s/r; MP compact rebuild BYTE-IDENTICAL (731,088) after EACH of the three changes → no Victor runs needed all session.  No designated successor — open tracks below.)

## §5c session notes (2026-06-11)

### Small model — what to know before extending it
- The near-code link model: ALL code (crt0 `_TEXT` + every TU's `_TEXT` + libstub `_TEXT`)
  coalesces by NAME in omf_link into one paragraph frame at para 0; entry CS=0.  Near calls
  are self-relative byte-distance fixups (frame-independent); 16-bit code-symbol fixups
  (fn ptrs, loc==1) resolve against the combined segment's frame == runtime CS.  This is
  why fn pointers REQUIRED the coalescing, not just contiguous placement.
- The i8086 backend needed ZERO changes: `sf_farcall()`/`farcall` in emit.c already gate
  helper calls on memmodel (near `call _qbe_div32s` under small), minic's `far_stdlib`
  mangling is NEAR_CODE-off, crt0 was the only far-call site.
- OPEN small-model gaps (extend on demand): no setjmp/longjmp (SETJMP_EXE dropped — needs
  a dedicated near impl with a 2-byte env return slot); softfloat probes not gated under
  small (the `_sf_*` helpers ARE near-callable and softfloat.c compiles — just never
  gated); no stevie-small (DGROUP pressure untested); `unfar_epilogue` is mechanical —
  any NEW epilogue block with computed arg offsets needs the `add si,N ; first vararg`
  idiom or plain `[bp+N]` so the reverse transform sees it.
- tiny .COM is untouched (flat concat via build-com-test.sh, never goes through
  libstub_to_exe/omf_link); `--model=tiny` through build-example.sh now produces the same
  near-code .EXE shape as small (untested, no consumer).

### float**/encoding — the residual map (post-move)
- Encoding law: any flag bit f collides with anything at bit f+3k after k shifts.  Current
  layout: SHORT 16, UNSIGNED 17, FLOAT 18, FAR 26, QVOLATILE 27.  Residuals: UNSIGNED@3
  →FAR, FLOAT@3→QVOLATILE (the fn-ptr/direct-call RETURN paths are immune via
  fpproto.rett / fnproto.rett), SHORT@4→28 (harmless today), far-data T*** loses its
  innermost FAR (bit 32 overflow).  A future flag must not land on 19–25 without checking
  depth collisions against all five.
- fnproto.rett applies to EVERY recorded direct call (not just float) — it also upgrades
  the stale-prototype shape where a function-local prototype's varh entry died at scope
  exit but fnproto persists (previously decoded FUNC(INT)).  MP byte-identical proves no
  shipping consumer changed.

### Open tracks (carried; pick by appetite)
- huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i) — gate has huge entries to extend.
- `jmp_buf bufs[6]` (§4v, UNREDUCED) — reduce, classify, fix or document.
- minic static-initializer FLOAT const-expr folding (`static float x = 2.0f*3.14f;`) —
  also unlocks MICROPY_PY_MATH_CONSTANTS for free.
- Multi-decl items after the first skip block_scope_decl (loud "double definition").
- Small-model setjmp/longjmp (near env) if a consumer appears; softfloat gate entries
  under small (cheap thickening).
- **ROADMAP.md still stale since 2026-05-23** (predates the whole MP campaign + §4/§5
  toolchain work) — one consolidation pass; also prune the CLAUDE.md Prior: chain.
- Kw spill-slot sharing (no consumer pain); newlibc integration (STRATEGIC, needs user
  go/no-go — would retire the libstub ret-rewrite machinery §5c just made model-aware).


---

Older session headers (§3u–§5c-PLAN and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
