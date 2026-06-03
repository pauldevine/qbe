# Next session (§2y — CONTINUE CODEGEN QUALITY: bank the NEXT easy, localized, measurable i8086 size win. WHY: §2w + §2x proved the thesis — the young minic+qbe-i8086 backend emits a lot of unnecessary code, and removing it is the lever for a bigger image margin / bigger programs under the ~896KB Victor ceiling (the image is content-bound; see [[project-mp-heap-ceiling-analysis]]). §2w landed dead AX/DX save-bracket elimination (-51520 B); §2x landed Kl-param-copy elimination (-57712 B). Same discipline: pick ONE win, land it green + measured + Victor-verified, then stop. Do NOT attempt a register-allocator rewrite or a general peephole framework.

> **§2y PLAN (next) — pick ONE remaining easy win, same loop as §2w/§2x
> (make check green AND DOS gate 184/184 AND a real image-size drop AND
> unchanged Victor output). The strongest remaining candidate, scoped in
> §2w's analysis and still un-done after §2x:**
>
>  1. **Redundant arg re-marshaling across adjacent calls** (the §2w
>     candidate #2; abi.c selcall / minic.y).  After §2x, `_mp_hal_stdout_
>     tx_str` reads the param directly from `[bp+6]` but STILL copies it
>     into the SAME outgoing-arg slot `[bp-20]` separately before each of
>     the two calls (`mov ax,[bp+6];mov dx,[bp+8];mov [bp-20],ax;mov
>     [bp-18],dx` appears twice, identical).  When two adjacent calls pass
>     the identical operand in the identical arg position with no
>     intervening redefinition of either the source or the dest arg slot,
>     the second marshal is dead.  EASY-WIN FRAMING: in the call/arg
>     lowering (i8086/abi.c selcall, where the Oarg→`store …, SLOT(argidx)`
>     happens), skip re-emitting an arg store whose {source ref, dest arg
>     slot, width} are unchanged since the previous call in the same block
>     AND nothing wrote either in between (a call clobbers caller-save regs
>     but NOT caller-frame slots, and the arg source here is a param slot
>     that is never rewritten).  Stay surgical; guard so it cannot change
>     which bytes are pushed/stored.  RISK: proving "nothing wrote in
>     between" across the intervening call — keep it conservative (bail if
>     the source ref is anything a callee could alias, e.g. an address-taken
>     local).  MEASURE on `_mp_hal_stdout_tx_str` (the second 4-instruction
>     marshal block should vanish) + full image.
>
>  OTHER candidates worth a look if #1 proves thorny: the Kw-param copy
>  (same shape as §2x but Kw params currently get a register, so aliasing
>  to a slot may be a WASH or worse — measure before committing); the
>  div/rem AX/DX save-bracket at the `dst_in_cx` site that §2w left ungated.
>
> NON-GOALS (unchanged): register-allocator rewrite, general peephole
> framework, near-call conversion, inlining, FAR_DATA/segment-model changes.
>
> MEASUREMENT LOOP (per win): `make qbe`; `make check` green; `bash
> tools/test-dos.sh` green (**184/184**; the Kl-clobber + structarg/param
> probes are the safety net); recompile one TU with
> `tools/recompile-mp-tu.sh mphalport ~/projects/micropython/ports/dos8086/mphalport.c`
> and eyeball the asm (NOTE: recompile-mp-tu.sh needs /tmp/mp_objs.txt for
> the relink step — a full build regenerates the objs but NOT that file, so
> the relink is skipped; the per-TU asm is still produced and that's what
> you eyeball); then full `tools/build-micropython.sh --model=compact
> --keep-going` for the body bytes (**§2x left it at body 744048 with heap
> 49152**; §2w baseline was 801760 at the same heap); then run on the real
> Victor:
>   `VICTOR_SRC=build/mp-test.py tools/run-victor-sasi.sh build/mp-link/mpython.exe 90`
> must still print `[2..37] / 12 / 197 / fib→514229 / 5 / 21`, clean
> `D4 C5`.  **Victor harness gotcha (burned again in §2x):** the run is
> deterministic but EMPTY serial output = a host invocation problem, not
> MAME flakiness — if the Bash tool *auto-backgrounds* the run (long
> timeout), the kill -9 EXIT trap fires early and kills MAME before it
> boots.  Invoke with explicit `run_in_background: true` (managed task) and
> wait for the completion notification; that keeps MAME alive.  Commit
> qbe-repo files at the green milestone; the MicroPython port tree is
> untracked (note port-tree changes in this file).
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
