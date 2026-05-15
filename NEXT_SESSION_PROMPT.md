# Resume prompt — Stevie/QBE: filetonext PROBE staged, awaiting DOSBox readout

## What this session did

Added a **debug probe** at the top of `filetonext()` in
`stevie-orig/screen.c`.  The probe short-circuits the normal
render logic and dumps the actual state of `Topchar` and the
first three `LINE` nodes into screen rows 0–5.  Status line
(row 23) continues to render normally via `msg(buff)`.

The probe is gated by `#define FTN_DEBUG_PROBE 1` (top of the
filetonext block) and the original body is gated by `#if 0`
just below it — both flips revert in seconds when the probe is
no longer needed.

### Files touched

```
stevie-orig/screen.c    +73 lines  (probe + #if 0 around original body)
```

No changes to qbe/i8086/emit.c this session.

### MiniC bug observed while landing the probe

MiniC's codegen emits a `ret` followed by more instructions in
the same basic block when a `return;` is followed by additional
statements inside the function — QBE then rejects the SSA
with `label or } expected` (see screen.ssa:280 from the failed
build).  Workaround: use `#if 0` to delete the unreachable code
entirely.  Worth a memory entry if it recurs.

## How to run the probe

```sh
make qbe  # already up to date
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" \
       -c "stevie.exe HELLO.TXT"
```

(The above is already done as of HEAD; `build/stevie-orig/stevie.exe`
exists with the probe linked in.)

## What you should see in DOSBox

Six debug rows at the top of the screen:

```
row 0:  PROBE TC.linep=<n> TC.idx=<n> nu=<n> Rows=<n> Cols=<n>
row 1:  lp0=<n> lp0->prev=<n> lp0->next=<n> lp0->s=<n>
row 2:  <Topchar->linep->s content, or "lp0->s is NULL">
row 3:  lp1=<n> lp1->s=<n>  lp2=<n>
row 4:  <Topchar->linep->next->s content, or "lp1->s is NULL">
row 5:  <Topchar->linep->next->next->s content, or "lp2->s is NULL">
row 23: "HELLO.TXT" 2 line, ?? character        (status)
```

### Interpretation cheatsheet

- `lp0->s` text should match HELLO.TXT line 1 (`Hello from a non-empty file!`)
  IF it's the sentinel head; otherwise it could match line 2 or be NULL.
- `lp1->s` and `lp2->s` should walk forward through the LINE list.
- If `lp0->next == lp0` (or `lp1 == lp0`, etc.) we have a **cycle**.
- If `lp0` itself is `0` (= NULL near pointer), `Topchar->linep` was
  never assigned — probably an `edit()` / `*Filemem` init bug.
- If `lp0` is sane but `lp0->s` is `0`, the `lp->s = malloc(...)`
  inside `newline()` either failed or rega lost the malloc-return value.
- If the printable strings show garbage that includes "1"/"l", the LINE
  list itself is corrupt — narrows the search to `readfile`'s
  `fileio.pp.c:548–554`.
- `nu=` reports `params[P_NU].value`.  Should be 0; if non-zero, we
  have a separate "params got corrupted" bug.

## Suspects (carried from previous prompt)

1. **LINE list cycle / corruption**: `readfile` builds the doubly
   linked list at `stevie-orig/fileio.c:117–123`.  Tactical audit
   of `build/stevie-orig/fileio.asm` lines 367–410 against
   `fileio.ssa` lines 184–219 is still pending — the asm has a
   suspicious lack of `mov [bp-42], cx` (the `curr = lp` store)
   that I couldn't locate when I scanned this session.  May be a
   missing storew or may be the slot is actually `[bp-60]`
   (the prologue is dense; readfile has 506 bytes of stack).
2. **`params[14]` corruption**: handled by the probe (row 0
   shows `nu=`).
3. **`inc(&memp)` returning -1 prematurely**: less likely
   (would produce `~`/`@`, not "1"/"l").
4. **`Topchar` / `Filemem` mis-init**: also covered by row 1
   (`lp0=`, `lp0->prev=`, etc.).

## Reverting the probe

```c
/* in stevie-orig/screen.c near line 42 */
#define FTN_DEBUG_PROBE 0   /* was 1 */
```

…and flip the `#if 0` below it back to `#if 1` (or remove it).

## Next steps

1. **Run the probe in DOSBox**, read the six rows, paste the
   contents back into the next session.
2. Based on the readout, jump to the appropriate suspect:
   - Garbage `lp0->s` content → audit `readfile` line-build
     asm at `fileio.asm:367–410` for a missing/swapped store.
   - Cycle (`lp0->next == lp0` etc.) → also in `readfile`.
   - `nu=1` → audit `param.c` initialization + BSS-zero crt0.
   - `lp0 == 0` → audit `edit()` at `edit.pp.c:438–440` and
     `Filemem` init.
3. After the bug is understood, flip the probe off (see above)
   and land the real fix.

## Outstanding non-probe TODOs

These are unchanged from the prior prompt:

- **Strategic Kl AX/DX fix in `i8086/isel.c`** — replace the
  tactical per-handler push/pop landed in commit 228eb27 with
  proper RAX/RDX clobber-modelling so rega avoids the conflict
  natively.
- **Kl-in-single-reg lossiness audit** — not blocking
  HELLO.TXT but matters for files >64KB and `%ld` printf.

## Memory entries that may need updates

- `feedback_minic_readfile_register_clobber.md` — scope is now
  broader than Bug B; the AX/DX implicit-clobber family applies
  to every emit.c handler that uses AX:DX as scratch.
- `feedback_minic_long_vararg_truncated.md` — Bug A is
  permanent; no change.
- **NEW candidate**: `feedback_minic_ret_then_code.md` — minic
  emits `ret` followed by SSA without a label when a `return;`
  is followed by more code in the same function.  Workaround:
  `#if 0` the unreachable code.

## Heisenbug lessons (carried forward)

- **rega's `visit` is sticky.** Fallback picks that violate the
  avoid mask must NOT propagate via `visit`.
- **Multi-word ops on a single-word ISA need wrapping.** Tactical
  push/pop landed in 228eb27; strategic isel-side fix still TODO.
- **Comparisons can clobber.** Even though `Oc*l` results are
  Kw, the body uses AX:DX — easy to miss.
- **Side-by-side binary tests catch state-of-mind drift.** Stage
  both pre and post binaries in `build/stevie-orig/` with
  distinct names so DOSBox's 8.3 mangling shows them as
  `STEVIE~1.EXE` and `STEVIE~2.EXE`; lets the user compare
  without rebuilding.
- **Probe-into-screen beats stderr in DOS.** `libstub_to_exe.py`
  only stubs read-only `fopen`/`getc`/`fclose` for the medium-
  model `.EXE` build; `fopen` for write, `fprintf`, `fputs`,
  `fwrite` all return 0 immediately.  Writing diagnostics into
  the `Nextscreen` buffer is the path of least resistance.
