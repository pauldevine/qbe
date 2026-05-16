# Resume prompt — Stevie/QBE: render bug ROOT-CAUSED, fix awaits

## Status (2026-05-15)

**Diagnosis complete.**  The "screen fills with 'l l l l l' garbage"
symptom is a QBE GCM + rega bug, NOT a stevie source-code or
filetonext/LINE-list bug.  Details below.

## TL;DR

In `nexttoscreen()`, QBE's GCM sinks the `endsc = npp + (Rows-1)*Columns`
computation past the `_anyinput → _bios_t_ci → _windgoto` call sequence.
Rega placed `t34 = Rows` in SI before the calls (SI is callee-save, so
that should have been safe), but **then chose to reuse SI for the spill
reload of `t37 = Columns`** right between the calls.  At the post-call
use site, the asm does `sub ax, 1` expecting AX to hold Rows — but
nothing ever loaded Rows into AX, and AX holds windgoto's return
value (0).  Result: `endsc = npp − Columns`, the loop `for (; npp <
endsc ; ...)` exits immediately, the screen never gets painted.

The full memory entry is at [[feedback-qbe-gcm-sinks-load-past-call]].

## The probe sequence that got us here

`stevie-orig/screen.c` now contains `filetonext`/`nexttoscreen` probes
gated by `#define FTN_DEBUG_PROBE 1`.  Each phase replaced one piece
of logic to isolate the bug:

| Phase | filetonext | nexttoscreen | Result |
|-------|-----------|--------------|--------|
| 0 | no-op return | normal | screen blank (no painting) |
| 2 | inline writes 'A'/'B'/'C' | normal | screen still 'l l l l' |
| 3 | blast Nextscreen with '@' | normal | screen 'lHHl l l l l' (NO '@') |
| 4 | as 3 | early return | screen blank |
| 5 | as 3 | `PROBE: ` + first 16 bytes of Nextscreen | `PROBE: @@@@@@@@@@@@@@@@` ✓ |
| 6 | as 3 | trivial walk, `outchar(npp[i])` | screen '@@@@@@' ✓ |
| 7 | as 3 | pointer-walk + `outchar(*rpp=*npp)` | screen '@@@@@@' ✓ |
| 8 | as 3 | + `if (*np!=*rp)` + inner windgoto | **ONE '@'** then nothing |
| 9 | as 3 | + outer windgoto + CUROFF/CURON, no inner windgoto | **screen empty** |

Phase 5 confirmed the entire data path works.  Phase 9's blank screen
plus the asm trace pinned the bug to the endsc-after-call codegen.

## What to revert when the fix lands

Before flipping `FTN_DEBUG_PROBE` to 0, also drop the `#if 0` around
the original `filetonext` body (it's intact, just gated off).  Then
remove the probe scaffolding from `screen.c` entirely:

```sh
git log --oneline | grep "debug probe"
git revert <probe-commit>      # or just edit screen.c by hand
```

The `feedback_qbe_gcm_sinks_load_past_call.md` and
`feedback_minic_ret_then_code.md` memory entries stay.

## The fix — three options

### A. rega.c: don't reuse a register that holds a live SSA temp

The natural place.  When `rega()` picks a register for the spill
reload of `t37`, it should consult its live-temp map and exclude
registers currently holding live values from a DIFFERENT temp.
Currently the conflict between `t34`-in-SI and `t37`-reload-into-SI
isn't being detected.

### B. spill.c: spill t34 across the call window

If SI is going to be needed for `t37` reload between the calls,
spill `t34` first.  Less natural — rega should own this — but
straightforward if rega's data model doesn't fit.

### C. i8086/emit.c: re-emit `mov ax, [_Rows]` at the use site

Last resort.  The emit pass would need to recognize that the
register holding `t34` was clobbered, and re-load from the source
global if it's a `loadw $name`-style temp.  This is hacky; the
compiler should know.

Recommend **A** first.  Investigate `rega()` in `rega.c` and
specifically how it handles spill-reload register selection.

## How to reproduce / verify a fix

```sh
make qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
# Confirm bug:
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" \
       -c "stevie.exe HELLO.TXT"
# Expected without fix: "screen fills with l l l l..."
```

To verify the fix BEFORE committing:

1. Flip `FTN_DEBUG_PROBE` to 0 in `stevie-orig/screen.c`.
2. Also re-enable the `#if 0` original filetonext body.
3. Rebuild.  If file content renders correctly, the fix worked.

## What's already in the asm to look at

`build/stevie-orig/screen.asm:185..203` is the smoking gun (phase 9
build, with my probe active).  See the memory entry for the
annotated trace.  The corresponding SSA is at
`build/stevie-orig/screen.ssa:140..147`.

## Outstanding work besides this fix

Carried over from prior sessions, unchanged:

- **Strategic Kl AX/DX fix in `i8086/isel.c`** — tactical push/pop
  landed in 228eb27, isel-level fix still TODO.
- **Kl-in-single-reg lossiness audit** — not blocking HELLO.TXT
  but matters for files >64KB and `%ld` printf.

## Memory entries created this session

- [[feedback-qbe-gcm-sinks-load-past-call]] — NEW.  The actual root
  cause for the render-loop bug.
- [[feedback-minic-ret-then-code]] — NEW.  MiniC emits unlabeled
  SSA after `return;` followed by more code.  Workaround: `#if 0`.

## Heisenbug lessons (carried forward)

- **rega's `visit` is sticky.** Fallback picks that violate the
  avoid mask must NOT propagate via `visit`.
- **Multi-word ops on a single-word ISA need wrapping.** Tactical
  push/pop landed in 228eb27; strategic isel-side fix still TODO.
- **Comparisons can clobber.** Even though `Oc*l` results are Kw,
  the body uses AX:DX — easy to miss.
- **Probe-into-screen beats stderr in DOS.**  `libstub_to_exe.py`
  only stubs read-only `fopen`/`getc`/`fclose`; `fopen` for write,
  `fprintf`, `fputs`, `fwrite` all return 0 immediately.  Writing
  diagnostics into the `Nextscreen` buffer is the path of least
  resistance.
- **Phased reduction beats grepping.**  When the bug-trigger is
  unclear, replace function bodies with progressively smaller
  versions and observe what flips.  This bug was invisible from
  source — only the side-by-side phase-7/phase-8/phase-9 results
  pinned the post-call register clobber.
