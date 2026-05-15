# Resume prompt — Stevie/QBE: Kl emit AX/DX preservation extended; DOSBox test pending

## Status

All known Kl-op emit handlers in `i8086/emit.c` now preserve AX/DX
around their bodies (rega still doesn't model the implicit clobber, but
the per-handler push/pop wrapper makes it correct in practice).
Stevie builds cleanly (`make qbe && tools/build-stevie.sh
--keep-going --model=medium`).

Outstanding: a DOSBox smoke test against `HELLO.txt` to see whether
readfile completes AND the screen renders correctly.  The screen-loop
bug from last session may already be fixed — `Ocultl`/`Oculel` (used
by `screen.c` for the topchar/botchar position comparisons) were
silently clobbering AX/DX, which would explain the corrupted render
loop.  Need to confirm in DOSBox.

## What landed this session

```
<this-commit>  i8086 emit: preserve AX/DX across remaining Kl handlers
              (Osub, Omul, Oload, Odiv/Orem, Oextsw/Oextuw, Oc*l x10)
```

Specifically: added `AxDxSave`/`kl_save_axdx`/`kl_restore_axdx` helper
in `i8086/emit.c` (just below `store_ax_to`), and wrapped each handler
that uses AX:DX as scratch.  Logic: push the scratch reg that ISN'T
the destination; pop it after the op stores its result into the dest
(or directly into AX/DX if the dest landed there).

Handlers wrapped this session:
- `Osub` Kl (line ~862)
- `Omul` Kl (line ~915)
- `Oload` Kl (line ~1245)
- `Odiv`/`Oudiv`/`Orem`/`Ourem` Kl (line ~1650) — also fixes the
  `dst == AX with remainder op` case (would have pop'd over the
  answer).
- `Oextsw`/`Oextuw` (line ~1712) — produces a Kl result from a Kw
  arg via `cwd` / `xor dx, dx`.
- All ten `Oc*l` comparisons (Oceql, Ocnel, Ocsltl, Ocslel, Ocsgtl,
  Ocsgel, Ocultl, Oculel, Ocugtl, Ocugel; lines ~1373–1556).

Previously wrapped (commits `1ee353a`, `1594464`, `fa06a81`): `Oadd`
Kl, `Ocopy` Kl, `Ostorel`.  Not refactored to use the new helper —
their inline expansions handle some extra edge cases (e.g., `Ocopy`'s
Con→Slot fast path).

## How to reproduce / test

```sh
rm -rf build/stevie-orig
tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" \
       -c "stevie.exe HELLO.txt"
```

Expected (good): screen shows the two lines of HELLO.TXT in the editor;
status bar says `"HELLO.txt" 2 line, ?? character` where `??` is the
total chars; keyboard navigation works.

Possible (still broken): screen render loops or shows `1 1 1 …`.
That would mean some Kl-using path is STILL clobbering register state
(or the bug is in some other category entirely — see "What's still
suspect" below).

## What's still suspect

### 1. Kl-in-single-reg lossy

`R5 =l load S179` with rega-assigned R5 = 16-bit reg only retains the
low word (DX is discarded).  Symptoms surface as sign-ext junk in
varargs (`%ld` printf) and could surface in `nchars` arithmetic on
files >64KB.

Fix options:
- Force every Kl tmp to be slot-allocated (the principled fix).
- Pair-allocate Kl tmps in rega (much bigger change).
- For now: only matters when the high word actually carries data
  beyond what fits in 16 bits.  HELLO.txt's `nchars` is tiny, so this
  doesn't block the smoke test.

### 2. r0/r1 in AX or DX (pre-existing)

The handlers emit `mov ax, src0; ... add ax, src1` — if src1 is RTmp
allocated to AX, src1's value was already clobbered by `mov ax, src0`.
The push/pop preserves the *caller-side* AX but not the *operand-side*
src1 within the op.

This is a pre-existing bug (the previous handlers had it too); we just
haven't hit it because rega tends not to put src1 in AX given hint
propagation.  A real fix would model AX/DX in `i8086/isel.c` (the
"strategic" route from the previous prompt).

### 3. Screen render loop

If still broken after this session, the suspect list is unchanged:
- `LINE.s` link-list corruption (the `*ptr = val` chain in `@l41`).
- `Filemem`/`Filetop`/`Curschar` init.
- `vgetc`/`inchar` returning non-blocking.
- `need_redraw` being reset on every `edit()` iteration.

Drop a probe in `edit()` to print whether `updatescreen()` is called
once or repeatedly.

## Next steps

1. **DOSBox smoke test** (you, manually): run the recipe above.  If
   the screen renders correctly, move to step 2.  If it loops, drop
   the `edit()` probe.
2. **Triage write-back (`:w`)** — still stubbed in libstub.
3. **Strategic fix: model AX/DX clobbers in `i8086/isel.c`.**  This
   replaces the tactical push/pop with explicit `Ocopy ↔ TMP(RAX)`
   wrappers in isel, mirroring `amd64/isel.c::seldiv`.  Lower cost,
   simpler emit handlers, and fixes the "src1 in AX" case.
4. **Audit Kl-in-single-reg** — likely needs Kl-tmp-always-spill.

## Memory entries updated

- `feedback_minic_long_vararg_truncated.md` — still current.
- `feedback_minic_readfile_register_clobber.md` — extend the Kl-implicit-clobber
  family scope to all the handlers landed this session.

## Heisenbug lessons (carried forward)

- **rega's `visit` is sticky.** Fallback picks that violate the avoid
  mask must NOT propagate via `visit`.  See `7225d19`.
- **Multi-word ops on a single-word ISA need to be modelled or
  wrapped.** Tactical push/pop works case-by-case; the strategic fix
  is in isel.
- **Comparisons can clobber.** Even though Oc*l has Kw result class,
  the *body* uses AX:DX — easy to miss because the result class hints
  at single-reg cleanliness.
