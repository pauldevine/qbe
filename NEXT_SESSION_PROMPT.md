# Resume prompt — Stevie/QBE: file loads, screen render + Kl emit still broken

## Status

Major progress: **readfile completes and recognizes lines**.  Status bar
shows `"HELLO.txt" 2 line, ? character` (the `?` is libstub's `%ld`
placeholder for nchars-high junk — see below).  But:

- The screen render loops endlessly, filling with a `1 1 1 1 …` pattern
  instead of the file's contents.
- With Bug A applied (commit e897434 — long → IL `l`), readfile hangs
  again because i8086 emit handles `Kl` operations with implicit AX:DX
  scratch that rega doesn't model.  Multiple per-op push/pop AX/DX
  workarounds landed today but don't cover every Kl op yet (Osub, Oload,
  the `Kl-in-single-reg` lossy load, …).

Current HEAD state: Bug A is **applied** (commit e897434, still in
history).  With it applied, the loop hangs.  Reverting it in your
working tree (`git revert e897434` + rebuild) gets you back to the
"2 line, ? character + screen-render loop" state.

## What landed this session (5 commits on master)

```
1ee353a i8086 emit: preserve AX/DX across Oadd Kl
1594464 i8086 emit: preserve AX/DX across Ocopy Kl
fa06a81 i8086 emit: preserve AX/DX across Ostorel
7225d19 qbe rega+i8086 emit: stop caller-save reg from leaking across calls
e897434 minic: emit 'l' (32-bit on i8086) for LNG types
```

### `e897434` — Bug A (variadic `%ld` truncation)

`irtyp` / `irtyp_ret` only returned `'l'` when `SIZE == 8`; the i8086
build has `SIZE(LNG) == 4`, so longs slipped through as `'w'`.  Fixed
by adding a `KIND == LNG` branch.

### `7225d19` — rega caller-save propagation

The "any free reg" fallback in `ralloctry` was setting `sethint` +
`visit` even when the fallback reg was in the avoid mask (caller-save
for a live-across-call temp).  That bad choice then propagated into
low-pressure blocks via `visit`.  Fix: skip propagation for fallback
picks.  Same commit ships the `Ocopy Kl Con → Slot` fast path so
`R1 = mov; R6 = 0; S231 = 0` parallel-move blocks don't clobber AX.

### `fa06a81` — `Ostorel` AX/DX preservation

32-bit store uses AX:DX as scratch unconditionally.  Wrapped with
push/pop AX (skip when src is AX) + push/pop DX (always).

### `1594464` — `Ocopy Kl` AX/DX preservation

Same family: 32-bit copy through AX:DX clobbers them.  Push/pop unless
they're the destination of the copy.

### `1ee353a` — `Oadd Kl` AX/DX preservation

Same: `mov ax, …; xor dx, dx; add ax, …; adc dx, …`.  In stevie's @l18
the ceqw(incomplete, 0) result was in DX and got wiped by the Oadd
preceding the jnz.

## What's still broken

### 1. Other `Kl` ops still leak AX/DX

The current fix is one-handler-at-a-time.  Untouched `Kl` handlers in
`i8086/emit.c` that also use the AX:DX scratch pattern:

- `Osub` (line ~839) — same shape as `Oadd`, same fix.
- `Oload` Kl (line ~1182) — DX clobbered by `mov dx, [hi]`.
- `Omul`/`Odiv`/`Orem` Kl (delegate elsewhere?  check).
- `Oand`/`Oor`/`Oxor`/`Oshl`/`Oshr`/`Osar` Kl — all use the same
  multi-word AX:DX path.
- The `Oceql`/`Ocnel`/`Ocsltl`…`Ocugel` comparisons.

**The right fix** is to model AX/DX as implicit clobbers in
`i8086/isel.c` (cf. how amd64 wraps `seldiv` with `TMP(RDX)`).  Then
rega would naturally avoid placing live tmps in AX/DX across Kl ops
and the per-handler push/pop hack becomes unnecessary.

### 2. `Kl-in-single-reg` is lossy

`R5 =l load S179` with R5 = single 16-bit reg only loads the low word;
DX gets the high word and is then discarded.  `nchars` arithmetic
stays correct as long as the value fits in 16 bits (it always does for
HELLO.txt, but the final `%ld` printf gets sign-ext junk because the
high word is reconstructed from the low word's sign bit at the call
site).

Properly: a `Kl` temp should occupy a register pair on i8086, or rega
should always spill `Kl` results to slots.  Today it does neither.

### 3. Screen render loops endlessly (with Bug A reverted)

After `readfile` returns "2 line", `updatescreen` apparently draws the
buffer over and over.  Symptoms (DOSBox screenshot):

- First chars on row 0: `1HH1 1 1 1 1 …`
- Rest of the screen: rows of `1 1 1 1 …`
- Status bar: `"HELLO.txt" 2 line, ? character`
- Screen fills, blanks, fills again every ~1s; keyboard input ignored.

Suspects:
- `LINE.s` strings corrupted (the link-up in @l41 stores wrong values
  somewhere — many of the `*ptr = val` operations use registers that
  may be clobbered by later Kl ops).
- The doubly-linked list of LINEs may have a cycle (link operations
  again, or `Filemem`/`Filetop`/`Curschar` initialised wrong).
- `vgetc` / `inchar` returning non-blocking, looping `edit()`.
- `need_redraw` getting reset to TRUE somewhere on every iteration.

## How to reproduce

```sh
make qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" -c "stevie.exe HELLO.txt"
```

With current HEAD (Bug A applied): screen shows just `"HELLO.txt"` —
loop is stuck.  To exercise the loop-completes-but-render-loops state:
`git revert HEAD~4 && make qbe && rm -rf build/stevie-orig && tools/build-stevie.sh …`
(reverts `e897434`).

## Next steps

1. **Properly model AX/DX clobbers for Kl ops in i8086 isel.**  This
   is the structural fix.  Look at `amd64/isel.c::seldiv` for the
   pattern — wrap each `Kl` IR op with explicit `Ocopy` to/from
   `TMP(RAX)` / `TMP(RDX)` so rega sees the clobber.  Then `Oadd Kl`
   etc. simply emit DX:AX directly without push/pop wrappers.
2. **Audit `Kl`-in-single-reg cases.**  Either make every `Kl` temp
   force-spill (`tmp[t].slot = …`) or pair-allocate.
3. **Triage the screen render loop.**  With Bug A reverted, drop a
   probe in `edit()` to print whether `updatescreen` is called once
   (good) or repeatedly (bad).  If repeatedly, check `need_redraw`
   and `anyinput()`.  If once, the LINE link list is the suspect.
4. **Write-back (`:w`).**  Still stubbed in libstub.

## Memory entries updated

- `feedback_minic_long_vararg_truncated.md` — Bug A fixed.
- `feedback_minic_readfile_register_clobber.md` — Bug B partially
  fixed (rega + Ocopy Kl in the back-edge).  Add a new entry for the
  Kl-implicit-clobber family if you want it durable.
- `MEMORY.md` index already updated.

## Heisenbug lessons (carried forward)

- **rega's `visit` is sticky.**  Fallback picks that violate the
  avoid mask must NOT propagate via `visit`.  See `7225d19`.
- **Multi-word ops on a single-word ISA need to be modelled or
  wrapped.**  Without it, rega thinks AX/DX are free for live tmps
  but the emit silently uses them as scratch.  Push/pop is a tactical
  fix; modelling in isel is the strategic one.
- **State failures in registers, not in C-source terms.**  "Loop
  exits at line emit" was the C symptom; "rega put ceqw result in DX
  but Oadd Kl wiped DX before the jnz" was the actual mechanism.
