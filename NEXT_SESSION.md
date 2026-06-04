# Next session (§3c — LAND SLICING: enable `MICROPY_PY_BUILTINS_SLICE` (1) in ports/dos8086/mpconfigport.h, which requires fixing TWO minic frontend bugs (below). WHY: §3b ("stress a real program", chosen by the user) found that sequence slicing — `s[a:b]`, `lst[a:b:c]` — is a SyntaxError on the Victor build: the slice grammar (py/grammar.h `subscript_3`) is gated behind `MICROPY_PY_BUILTINS_SLICE`, which defaults to CORE level while dos8086 is at MINIMUM. Enabling the one flag IS sufficient for the grammar (verified on a host ports/minimal build), and objslice.c is already in the curated `py/*.c` build glob — but rebuilding `mpython.exe` with it ON fails minic on two TUs:

>  - **BUG 1 — objslice.c (`error:1178: out-of-order designated initializer unsupported`)**: the file-scope `const mp_obj_type_t mp_type_slice = { .base=…, .flags=…, .name=…, .slot_index_unary_op=1, .slot_index_print=2, .slots=… }` lists struct members in NON-ascending offset order (`slot_index_print` is declared BEFORE `slot_index_unary_op` in `mp_obj_type_t`, but the init sets unary_op first). minic's struct designated-init emitter (minic.y ~5409: `if (m->offset < cursor) die(...)`) streams members in order and rejects a backwards offset. **FIX shape**: same as the §1k out-of-order ARRAY designator fix (`agg_emit_array` buffered into index-addressed slots) but for struct members — collect (offset,value) pairs, emit in offset order with gap-fill. Bitfield-run packing (minic.y ~5339) complicates it; mixed positional+designated items need C99 cursor semantics. There are TWO dies (minic.y:5355 bitfield path, :5411 normal path).
>  - **BUG 2 — vm.c (`error:1357: parse error`)**: a LOCAL struct init with a NESTED designator — `mp_obj_slice_t slice = { .base = { .type = &mp_type_slice }, .start=start, .stop=stop, .step=step };` — parse-errors. Different machinery from Bug 1 (local aggregate init / `emit_clit_aggr` / `mk_local_array_init`), a grammar gap on `.member = { .nested = … }` in a local initializer.
>
> Both are real frontend features, ~a session each. FAST LOOP DISCOVERED THIS SESSION: a host repro of the minimal grammar/parse path lives at `~/projects/micropython/ports/minimal` — `make CROSS=0` builds a host binary, and main.c was patched (behind `#if 1`, untracked port tree) to read ALL of stdin and `do_str(src, MP_PARSE_FILE_INPUT)` (faithful to the Victor `do_str` path, NOT line-by-line REPL). Pipe a `.py` to `build/firmware.elf` and grammar gates reproduce in milliseconds instead of a ~5-min MAME run. NOTE: host config ≠ dos8086 config for RUNTIME (host has longint/64-bit obj_t), so use host for GRAMMAR/parse gates only; use the real Victor for int-range / runtime behavior. Also note `MP_SRC_MAX=2048` in ports/dos8086/main.c caps a PROG.PY at 2KB — a >2KB program is truncated mid-token → SyntaxError (burned one Victor run on this; the feature probe is now 1818B). Probe lives at `build/mp-feature-probe.py` (slice lines will only pass once slicing lands). After landing: run it on Victor to map classes/generators/exceptions/str-methods/builtins/int-range in one shot (all currently UNVERIFIED past the slice SyntaxError).
>
> ---
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
