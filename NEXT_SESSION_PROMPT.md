# Resume prompt — Stevie/QBE: filetonext corruption RESOLVED

## Status

**Filemem corruption is fixed.** Empty buffer test now shows `abc14822
24822` — Filemem stays at 0x4822 across filetonext. Three i8086 emit
bugs found and fixed across this and the prior session.

## What's fixed this session (i8086/emit.c)

**3. Omul clobbered DX silently.** *(the actual root cause of the
filetonext heisenbug)*

8086's single-operand `imul r/m` writes the 32-bit result as DX:AX.
QBE's rega does not model this implicit clobber, so it freely placed
live SSA temps in DX across mul instructions. In the no-probe build of
`filetonext`, rega put `screenp.44` in DX at l29, then `imul bx` for
`c*3` overwrote DX with the high word of the product (zero, for c=0).
The inner blank loop then wrote `0x20` from address 0x000C upward,
blasting Filemem and most of `_DATA`.

This was the "heisenbug" — probes shifted rega allocation so that
`screenp` happened not to land in DX during a mul, masking the issue.

**Fix:** wrap `imul` with `push dx`/`pop dx` whenever the destination
isn't DX. Also added a defensive swap for the case where `a1 == AX`:
the existing `mov ax, a0` would clobber a1 before `imul a1` could read
it. 16-bit mul is commutative for the low-word result, so swapping
operands is safe.

The clean long-term fix is to model the implicit clobber in
`i8086/isel.c` the way amd64's `seldiv` does (with `TMP(RDX)`). The
push/pop workaround in `emit.c` is self-contained and correct but
costs 2 instructions per mul.

## Pre-existing fixes from prior session (still in tree)

**1. Oswap with `to=R` was dropped silently.** rega's pmgen emits
`Oswap to=R, arg0=src, arg1=dst` to resolve cycles in parallel
register moves at block boundaries. The emit code had an early-return
treating these as "QBE-internal no-op markers" — actually they're real
xchg ops. Fix: explicit `xchg arg0, arg1` emission for `Oswap` with
null `to`.

**2. Jjnz on a spilled condition clobbered AX.** Emit pattern for
`jnz <slot>` was `mov ax, [bp+X]; test ax, ax` with a comment claiming
"AX is dead at block-end." Wrong — rega freely places live SSA temps
in any caller-save reg at block end. Fix: `cmp word [bp+X], 0` sets ZF
directly without touching any register.

## Bisection trail (for future heisenbug hunts)

The fix wasn't obvious from the rega map alone. The path that found it:

1. Confirmed corruption is INSIDE filetonext (empty stub → no corruption).
2. Bisected by gating the main while loop (`while(0)`) → corruption pattern
   changed from `2020` to `2121` (the cleanup blank's diagnostic char),
   localizing to the cleanup l58 loop in that build.
3. Read the asm for the cleanup mul: `Nextscreen + srow * Columns` →
   noticed `imul ax` where rega'd IR said `mul Columns, srow` with srow in AX.
4. First fix: swap operands when `a1 == AX`. This fixed the cleanup
   case but not the main loop.
5. Re-enabled main loop; corruption returned. Audited remaining muls:
   l29's `imul bx` (for `c*3`) was correct in isolation but the rega
   map showed `screenp.44 = DX` at l29 entry. The imul clobbers DX.
6. Wrapped imul with push/pop dx → full fix.

## Probes/diagnostics state

- `stevie-orig/screen.c` carries probes in `updatescreen()`:
  `dbghex4((unsigned)Filemem)` before and after `filetonext()`,
  followed by a `for(;;)` halt so the post-call value is visible.
  Useful as a regression check. Remove these once you trust the
  empty-buffer path.
- `stevie-orig/alloc.c`, `edit.c`, `main.c` have probe additions
  (`dbgFM`, `dbgmbef`, `dbgmaft`, `dbgaddr`) carried from the
  filetonext bug hunt. These can be culled when convenient.

## Next steps

1. **Verify other code paths.** The empty-buffer test passes. Try
   opening a non-empty file (cmd-line arg) and exercising more of the
   editor to see if any other `imul`-near-live-DX cases break.
   `lfiletonext` has a similar structure and should also be tested.
2. **Consider the cleaner fix.** Replace the push/pop-around-imul
   workaround with proper clobber modeling in `i8086/isel.c`. Pattern:
   add `emit(Ocopy, k, TMP(RDX), CON_Z, R)` after the mul (or similar)
   to tell rega DX is written. See `amd64/isel.c` `seldiv` for the
   template. Audit other instructions with implicit register effects:
   `cbw` (AL→AX), `cwd` (AX→DX:AX), and any shift count via CL.
3. **Strip the debug probes** from stevie-orig once the editor is
   stable on a non-empty buffer. The `for(;;)` after the second probe
   in `updatescreen()` is the most important to remove.
4. **Commit the i8086/emit.c fix** has been done (see git log). The
   stevie-orig/ probe changes are still uncommitted — decide whether
   to commit them as a "debug instrumentation" patch or revert.

## Build & test

```
make qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
dosbox build/stevie-orig/stevie.exe
```

Expected: `abc14822 24822`. (Hangs at `for(;;)` after the second '2'
probe — that's intentional. Ctrl-F9 in DOSBox to exit.)

## Probe legend (current state)

- `abc` = edit() startup probes
- `1` / `2` / `3` = updatescreen probes around filetonext / nexttoscreen
- `for(;;)` after the '2' probe blocks further execution so the
  Filemem value after filetonext is visible

For `dbghex4(X)`: prints `XXXX ` (4 hex digits + space).
For `dbgaddr(&X)`: prints `<addr><value>;` (avoids stale-AX bug since
the address is a constant immediate).

## Heisenbug lessons (for the playbook)

- **Implicit register clobbers are silent killers.** When porting QBE
  to a new target, audit every instruction the target uses for
  side-effect register writes (DX:AX for mul/div, AL→AX for cbw, CL
  for shifts on pre-286, AX for cmpxchg, etc.) and make sure rega
  knows. The amd64 backend models these via `TMP(R<X>)` ops in isel.
- **Heisenprobes != fixed bugs.** When adding a probe makes the
  symptom go away, the next question is "what about rega allocation
  changed?" — not "the probe helped, ship it." Diff the rega map with
  and without probes (`qbe -dR`) to find the differing allocation.
- **The corruption shape is a fingerprint.** ~10KB of `0x20` spanning
  most of `_DATA` from `0x0C` to `0x2A74` was telling us "a blank loop
  ran from address ~0 instead of from `Nextscreen` (`0x500E`)." That
  pointed at screenp being clobbered to 0, not at the loop math being
  off — which led directly to the DX-clobber root cause.

## Memory entries updated

- `feedback_i8086_imul_dx_clobber.md` (new) — root cause + fix.
- `feedback_i8086_oswap_dropped.md` — Oswap/Jjnz fixes from prior session.
