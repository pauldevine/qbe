# Next session (§3a — CONTINUE CODEGEN QUALITY: bank the NEXT easy, localized, measurable i8086 size win. WHY: §2w + §2x + §2y + §2z proved the thesis — the young minic+qbe-i8086 backend emits a lot of unnecessary code, and removing it is the lever for a bigger image margin / bigger programs under the ~896KB Victor ceiling (the image is content-bound; see [[project-mp-heap-ceiling-analysis]]). §2w landed dead AX/DX save-bracket elimination (-51520 B); §2x landed Kl-param-copy elimination (-57712 B); §2y landed redundant-arg-marshal elimination (-1952 B); §2z landed div/rem AX/DX bracket liveness gating (-272 B). Same discipline: pick ONE win, land it green + measured + Victor-verified, then stop. Do NOT attempt a register-allocator rewrite or a general peephole framework.

> **NOTE ON DIMINISHING RETURNS: the two big levers (§2w AX/DX brackets,
> §2x Kl-param copy) are spent.  §2y/§2z were each sub-2KB.  Before
> investing a session, MEASURE the candidate's aggregate payoff early
> (recompile a couple of representative TUs, eyeball how often the idiom
> actually fires) — if it's only a few hundred bytes it may not be worth
> a full Victor cycle.  Consider whether the session is better spent on a
> NEW capability (the image is content-bound, so a genuinely smaller
> codegen lever beats nothing, but a feature that lets a real program run
> may matter more).**

> **§3a PLAN (next) — pick ONE remaining easy win, same loop as
> §2w/§2x/§2y/§2z (make check green AND DOS gate 184/184 AND a real
> image-size drop AND unchanged Victor output). Candidates:**
>
>  1. **Kw-param copy** (same shape as §2x but for Kw params).  Kw params
>     currently get a register, so aliasing to a slot may be a WASH or
>     worse — MEASURE before committing.
>
>  2. **CX liveness for div/rem `save_cx`** — extend compute_axdx_liveafter
>     to also track CX, then gate §2z's `save_cx` on it.  Bigger change
>     (the tracker currently models only AX/DX); the div/rem site is the
>     only `save_cx` consumer, so payoff is bounded by div/rem frequency
>     (same population §2z's -272 B came from — likely small).
>
>  3. **Survey for other unconditional save brackets** that could be
>     liveness-gated the way §2w/§2z did — grep emit.c for `push ax`/
>     `push dx` sites not already going through kl_save_axdx.
>
> NON-GOALS (unchanged): register-allocator rewrite, general peephole
> framework, near-call conversion, inlining, FAR_DATA/segment-model changes.
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
