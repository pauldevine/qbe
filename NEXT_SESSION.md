# Next session (§3g — the MicroPython language surface is SOUND on real Victor AND validated end-to-end by a non-trivial integration program (§3f(A) — RPN calculator + OOP/inheritance + dicts + comprehensions + generators + custom AND VM-raised exceptions caught in functions, `build/mp-integ.py`, ran byte-identical to host, clean `D4 C5`).  No known correctness bug remains.  Remaining OPTIONAL directions:  (B) ENABLE more builtins — min/max/abs/sorted/enumerate are OFF at MINIMUM ROM (`NameError`).  Flip `MICROPY_PY_BUILTINS_MIN_MAX` (+ check the sorted/enumerate flags) in the UNTRACKED `ports/dos8086/mpconfigport.h`; image has ~111 KB headroom (784800 B total under ~896 KB).  MEASURE the image after.  Cheap, makes the feature probe print all-OK.  (C) PROPER `volatile` — minic still DISCARDS the `volatile` qualifier on variables (the type productions `VOLATILE TINT → INT` drop it; `isvolatile` in varh is only for inline-asm).  §3e's setjmp-gate covers the setjmp/longjmp case conservatively, but a TRUE `volatile` (needed for memory-mapped I/O, or any non-setjmp use) is still unimplemented — a real local would be register-cached.  Low priority for MicroPython (no MMIO), but it is a real C-conformance gap; doing it right = thread `volatile` into varh + force those allocs non-promotable + emit a per-access load/store QBE won't elide.  (D) the 211-commit upstream-qbe rebase (still deferred).  NOTE: §3e's alias.c change is TARGET-GENERAL (helps amd64/arm64/rv64 too — `make check` green), a genuine upstream-worthy QBE correctness fix.  HARNESS: feature/exception probes need a ≥200 s Victor budget (slow parse, NOT a hang); host minimal port (slice enabled) for GRAMMAR only.  Reproducers: `build/exc-min.py`, `build/exc{,2,3,4}-probe.py`, `build/nlr_mock_probe.c` (the fast DOSBox setjmp/longjmp repro that cracked §3e).)

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
