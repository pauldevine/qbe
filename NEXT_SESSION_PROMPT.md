# Resume prompt — Stevie cursor motion fully working; screenclear+updatescreen redraw is the next deep bug

## Status (2026-05-17, late)

**Cursor motion is fully operational.** All of these work in DOSBox with HELLO.TXT:
- `j`/`k`/`h`/`l` move the cursor.
- `l` past end-of-line beeps without breaking the editor.
- `j` past last line beeps without breaking the editor.
- `G` jumps to the last line.
- Unknown commands (`g` alone) beep without breaking the editor.
- `<ESC>:q!<CR>` quits cleanly.

The remaining oddity (`gg` doesn't go to line 1) is not a bug — `gg`
is a vim extension that stevie never implemented. The first `g` falls
through to default-beep. To go to line 1, use `1G`.

## Fixed this session

### 1–3. (carried from prior session, see commit 2c1ea66)
- `i8086/emit.c` `Ostorel` destination-address clobber
- `i8086/emit.c` `Ostorel`/`Oload` BX-scratch clobber
- minic `static` for function-local variables not persistent — worked
  around by promoting `_nl_next`/`_pl_prev`/`_ca_lp`/`_gtl_l`/`_gl_pos`
  to file-scope statics in `linefunc.c`, `misccmds.c`, `cmdline.c`.

### 4. Visual bell default off (this session, uncommitted)

`stevie-orig/param.c:18` — flipped `vbell` default to FALSE.

Root cause path:
- `vbeep()` in `stevie-orig/dos.c:376` calls `setcolor(revcolor)` then
  `setcolor(oldcolor)` to flash colors.
- `setcolor()` in `stevie-orig/dos.c:812` ends with
  `if (!quitting_now) { screenclear(); updatescreen(); }`.
- `screenclear()` blanks the BIOS screen via INT 10h AH=09h.
- `updatescreen()` is supposed to redraw via `filetonext()` +
  `nexttoscreen()` but FAILS to repaint, leaving the screen blank.

The visual-bell flash thus blanked the screen on every boundary beep.
With audible BEL (`\007`), no screenclear runs and the screen stays
intact.

## What remains broken

**The setcolor → screenclear() + updatescreen() pipeline does not
restore the screen.** This is the next bug to chase. Triggers:
- `:set co=<n>` (explicit color change via `:set`)
- Currently no other path; setrows() uses different machinery.

This is the same "displaying nothing" pattern that previously forced
`flushbuf` (dos.c:226) to abandon the AH=09 write-with-attribute path
in favor of AH=0Eh teletype-per-char. So the redraw path through
`nexttoscreen()` is suspect.

### Hypotheses for the redraw failure

1. **`filetonext()` writes Nextscreen but `nexttoscreen()` sees no
   diff.** After `screenclear()`, both Realscreen and Nextscreen are
   all-spaces. `filetonext()` should rebuild Nextscreen from
   `Topchar`/`Filemem`. If `Topchar` is stale or its `*Topchar` deref
   reads truncated data (LPTR is a long pointer; some Kl path could
   still be truncated to 2 bytes), `filetonext()` might fill all
   spaces and `nexttoscreen()` would see no diff.

2. **`nexttoscreen()` correctly emits chars but `flushbuf`'s AH=0Eh
   teletype loop has a latent codegen bug** that silently fails
   on long batches (e.g. ~Cols*Rows = 2000 chars).

3. **`anyinput()` is spuriously TRUE inside the redraw**, triggering
   the early return at `screen.c:202` that sets `need_redraw=TRUE`
   without writing. But `need_redraw` is checked in the main loop, so
   the blank would only last one frame — yet it persists until the
   next character keypress. So this is unlikely unless `need_redraw`
   itself is corrupted.

### Suggested debug approach

- Add a static counter `n_filetonext_calls` incremented at the top
  of `filetonext()`, and a tracer that writes the count to a fixed
  screen position (bypassing Nextscreen) so we can see how many times
  it runs across one `:set co=15` invocation.
- Inspect `*Topchar` immediately on entry to `filetonext` — log
  `Topchar->linep` (or `LINEOF(Topchar)`) to a fixed status line.
- Compare with original setcolor behavior by writing a minimal C test
  that calls `setcolor` directly.

## Verification

- QBE test suite: 59/62 (3 pre-existing arm64 failures unrelated).
- Stevie compiles (24/24 sources), links to 141,888-byte medium-model
  `.EXE`.
- All cursor motion + boundary beeps + quit confirmed in DOSBox
  2026-05-17.

## Reproduction

```sh
cd /Users/pauldevine/projects/qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Line 1: Hello
Line 2: whatever
Line 3: third time
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" -c "stevie.exe HELLO.TXT"
```

In stevie, try `:set vb` to re-enable visual bell, then `l` past EOL
— the screen will go blank. `:set novb` to restore.

## Memory entries from this session

- `feedback_stevie_vbeep_screenclear_blanks.md` (new this session) —
  setcolor's screenclear+updatescreen leaves screen blank;
  worked around by defaulting vbell=FALSE.
- `feedback_i8086_storel_clobbers_dest.md` (prior) — Ostorel dest
  address clobber.
- `feedback_i8086_storel_loadl_bx_clobber.md` (prior) — Ostorel/Oload
  BX scratch.
- `feedback_minic_static_local_not_persistent.md` (prior) — minic
  treats `static` locals as auto.
