# Next session (§2x — CONTINUE CODEGEN QUALITY: bank the NEXT easy, localized, measurable i8086 size win. WHY: §2w proved the thesis — the young minic+qbe-i8086 backend emits a lot of unnecessary code, and removing it is the only lever for a bigger heap / bigger programs (the image is content-bound; see [[project-mp-heap-ceiling-analysis]]). §2w landed ONE win (dead AX/DX save brackets → -51520 body bytes, heap bumped 19456→49152 = 2.5×, all verified on real Victor). Same discipline applies: pick ONE win, land it green + measured + Victor-verified, then stop. Do NOT attempt a register-allocator rewrite or a general peephole framework.

> **§2x PLAN (next) — pick ONE of the remaining easy wins, same loop as §2w
> (make check green AND DOS gate 184/184 AND a real image-size drop AND
> unchanged Victor output). The two strongest remaining candidates, both
> already scoped in §2w's analysis:**
>
>  1. **Eliminate the param-materialization copy** (highest leverage, but
>     touches spill.c/abi.c so verify carefully).  Every Kl (far-pointer)
>     PARAMETER is currently copied from its incoming ABI stack slot (e.g.
>     `[bp+6]`) into a fresh below-BP forced-Kl slot (`[bp-14]`) at function
>     entry, then read from there.  Root cause: `force_kl_slot` (spill.c)
>     allocates a NEW slot for the incoming-param Kl temp instead of reusing
>     the param's existing 4-byte stack location.  The incoming-param SSA temp
>     is never reassigned (minic stores mutable copies into a separate
>     alloca), so aliasing its slot to the negative incoming-param slot is
>     safe and removes one `mov ax,[bp+6];mov dx,[bp+8];mov [bp-14],ax;mov
>     [bp-12],dx` block from EVERY function with a far-pointer param (ubiqu
>     itous).  EASY-WIN FRAMING: in i8086 abi.c/spill.c, when an incoming Kl
>     `Opar` temp's only def is the ABI load and it is never stored-to, set
>     its `tmp[].slot` to the negative incoming slot so the abi.c
>     `Oload Kl to=param-slot from=param-slot` becomes a self-copy that emit
>     elides.  MEASURE on mphalport `_mp_hal_stdout_tx_str` (the `[bp+6]→
>     [bp-14]` copy should vanish) + full image.  RISK: spill/ABI slot
>     numbering; the DOS gate's structarg/param probes are the net.
>
>  2. **Redundant arg re-marshaling across adjacent calls** (lower risk,
>     minic.y).  In `_mp_hal_stdout_tx_str` the same `str` slot is copied into
>     the SAME arg slot `[bp-20]` separately before each of the two calls.
>     When two adjacent calls pass the identical operand in the identical arg
>     position with no intervening redefinition, the second marshal is dead.
>     EASY-WIN FRAMING: in the call/arg lowering, skip re-emitting an arg copy
>     whose source slot + dest arg slot + width are unchanged since the
>     previous call and nothing wrote either in between.  Stay surgical; guard
>     so it cannot change which bytes are pushed.
>
> NON-GOALS (unchanged from §2w): register-allocator rewrite, general
> peephole framework, near-call conversion, inlining, FAR_DATA/segment-model
> changes.
>
> MEASUREMENT LOOP (per win): `make check` green; `bash tools/test-dos.sh`
> green (**184/184**; the Kl-clobber probes are the safety net); rebuild one
> TU with `tools/recompile-mp-tu.sh mphalport …` and eyeball the asm; full
> `tools/build-micropython.sh --model=compact --keep-going` for the body
> bytes (**§2w left it at body 801760 with heap 49152**, or 772064 with the
> pre-bump heap 19456 — note which baseline you compare against); then
> `VICTOR_SRC=build/mp-test.py tools/run-victor-sasi.sh
> build/mp-link/mpython.exe 90` must still print `[2..37] / 12 / 197 /
> fib→514229 / 5 / 21`, clean `D4 C5`.  Commit qbe-repo files at the green
> milestone; the MicroPython port tree is untracked (note port-tree changes
> in this file).
>
> ---
>
> **§2w (DONE 2026-06-03) — i8086 dead AX/DX save-bracket elimination via
> conservative liveness.  ONE codegen win, fully verified.  COMMITTED
> `9a32707` (i8086/emit.c).**
>
>  - **The win**: the `push ax/push dx … pop dx/pop ax` brackets around every
>    Kl op and 32-bit copy (added to fix the i8086-kl-* clobber bugs) fired
>    UNCONDITIONALLY, even at function/arg boundaries where AX/DX hold nothing
>    live — ~25% of the image was these pushes (64702 `push ax`).  New
>    `compute_axdx_liveafter()` in i8086/emit.c does a per-block backward scan
>    of physical-AX/DX liveness; `i8086_emitfn` sets `g_live_ax_after`/
>    `g_live_dx_after` per instruction; the SIX save-bracket sites
>    (`kl_save_axdx` + the Oadd/Ocopy/Ostorel inlines) gate their push/pop on
>    it.  **STRICT over-approximation so it cannot reintroduce the clobber
>    bugs**: blocks entered with AX/DX assumed live at exit (covers Jretw/
>    Jretl/Jnz + cross-block); USE recorded for every AX/DX operand AND every
>    Kl op with a register operand (high word lives implicitly in DX); KILL
>    only for definite overwrites (result to AX/DX, or a call's caller-save
>    clobber).  Globals default 1 = original always-save behaviour.  (The
>    div/rem bracket at the `dst_in_cx` site was left ungated — rare, more
>    delicate push ordering; revisit if needed.)
>  - **Results**: `_mp_hal_stdout_tx_str` 35→23 instructions (all 3 dead
>    brackets gone); image `push ax` brackets 64702→**28720 (-56%)**; compact
>    image body **823584 → 772064 B (-51520)**.  `make check` green; DOS gate
>    **184/184** (sigencode_probe + the kl probes green = no clobber
>    regression); real-Victor mp-test.py output unchanged.
>  - **Heap bonus** (port tree, untracked): the 51 KB freed let
>    `MICROPY_HEAP_SIZE` go **19456 → 49152 (48 KB, 2.5×)** — bounded by the
>    compact-model 64 KB single-segment limit (main's heap[] is one far
>    segment; >64 KB breaks GC's in-segment pointer math), NOT just the DOS
>    load ceiling.  Rebuilt body 801760 (< the known-loading 823584); loads +
>    runs correctly on the real Victor.  See
>    `ports/dos8086/mpconfigport.h:80`.
>  - **The thesis is proven**: every code byte saved is a heap byte gained.
>    Keep going (§2x).
>
> ---
>

---

_Older session headers (§2u and earlier) were moved to [`SESSION_LOG.md`](./SESSION_LOG.md) to keep this file focused on the current plan + the immediately-preceding session. See there for the full history._
