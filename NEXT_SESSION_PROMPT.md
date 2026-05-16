# Resume prompt — Stevie/QBE: rendering FIXED, cursor movement NEXT

## Status (2026-05-16)

**Three rendering bugs fixed today; one outstanding: cursor keys
(j/k/h/l) do nothing.**  Stevie now displays line 1, line 2, and the
status line correctly when opening a file.  Pressing movement keys
has no visible effect, suggesting either input isn't reaching
`vgetc()` or the per-keystroke redraw path isn't running.

## What landed today

### 1. spill.c main-loop iscall — void-call callee-save limit

```c
bscopy(u, v);
if (iscall(i->op)
&& (i+1 == &b->ins[b->nins] || !regcpy(i+1)))
    /* Void call (not handled by dopm) — live-across-call temps must
     * fit in actual callee-saves (ngpr - nrsave - nrglob). */
    limit2(v, T.nrsave[0] + T.nrglob, T.nrsave[1], w);
else
    limit2(v, 0, 0, w);
```

The pre-fix `sethint(v, caller-saves)` was only an avoid hint;
rega's fallback path ignored it when callee-saves were exhausted.
With 4 live temps and only 3 callee-saves (i8086 BX/SI/DI),
`t34=Rows` ended up in a caller-save reg and got clobbered by the
`_windgoto` call.  See [[feedback-qbe-gcm-sinks-load-past-call]].

The `regcpy(i+1)` guard skips this path when dopm has already
handled the call.  Without that guard, dopm's later `v |= argregs`
combined with the tightened limit2 trips `slot()`'s "cannot spill
register" assert on amd64 abi tests.

### 2. spill.c dopm — value-returning call callee-save limit

```c
if (i != b->ins && iscall((i-1)->op)) {
    v->t[0] &= ~T.retregs((i-1)->arg[1], 0);
    limit2(v, T.nrsave[0] + T.nrglob, T.nrsave[1], 0);
    ...
}
```

Same `nrsave + nrglob` formula as the main-loop fix.  Resolves the
"`2 line, ? character`" status line: `linecnt` was landing in CX
across `_fclose`/`_free` (caller-save calls handled via dopm).
With both fixes in place, all 62 QBE tests pass except the 3
pre-existing arm64 `far_pointer` ones.

### 3. libstub.asm `_spr_emit_w16` — preserve BX

```asm
_spr_emit_w16:
    push bx        ; <-- ADDED
    ...
    mov bx, 10
    div bx
    ...
    pop bx         ; <-- ADDED
    retn
```

`sprintf` uses BX as the variadic-arg pointer.  `_spr_emit_w16`
loaded `bx = 10` for `div` without saving, so every format spec
after the first `%d` read from address 10 onward (BIOS data area).
Status line became `"FILE" 2 line, ? character` instead of
`"FILE" 2 lines, 22 characters`.

### 4. stevie-orig/screen.c — `flushbuf()` at end of `nexttoscreen`

```c
CURON;        /* enable cursor again */
flushbuf();   /* commit the diff to the actual screen */
```

`nexttoscreen` was buffering its diff output via `outchar` (the
BIOS-mode `outone` macro buffers into `outbuf`).  The original
design relies on `inchar()` calling `flushbuf` at the top of its
loop.  Something in the current build is preventing `inchar` from
running on the first iteration, so without an explicit flush at
end of `nexttoscreen`, the buffered chars (including line 2)
stayed invisible.  Adding `flushbuf()` to `nexttoscreen`'s tail
forces the diff to materialize on screen.

**This is a workaround, not a root-cause fix.**  The real bug is
likely the same one keeping j/k/h/l silent — see below.

## Outstanding: cursor keys j/k/h/l do nothing

Symptoms (with all four fixes above applied):
- Initial draw correct: line 1 at row 0, line 2 at row 1, tildes
  at rows 2-22, status line at row 23.
- Pressing j, k, h, l → no visible change.  Cursor stays at
  row 1 col 0 (wherever the initial draw left it).

The `flushbuf` workaround tells us `nexttoscreen` is being called
once and completing.  After that, control should return to
`edit()`'s `for (;;)` loop:

```c
for ( ;; ) {
    cursupdate();
    if (need_redraw && !anyinput())
        updatescreen();
    if (!anyinput())
        windgoto(Cursrow,Curscol);
    c = vgetc();           /* vgetc → inchar → flushbuf + getch */
    if (State == NORMAL) {
        ...
        normal(c);
    }
    ...
}
```

The fact that the flush-at-end-of-nexttoscreen workaround was
needed strongly suggests **we never reach `vgetc()` on the first
iteration** — because `vgetc → inchar → flushbuf()` is what
normally flushes the diff.  If we *had* reached it, the original
(probe-less) build would have shown line 2 on the first redraw.

Three hypotheses to chase tomorrow, in order:

### Hypothesis A: hang in `cursupdate()` or `windgoto()`

`cursupdate()` (screen.c) walks the file to compute Cursrow/Curscol
from Curschar.  If it has a register-clobber or infinite-loop bug,
the main loop never reaches `vgetc()`.

**Investigation steps:**
1. Add `outstr("[A]"); flushbuf();` immediately *before* `cursupdate()`
   in edit.c.  If `[A]` doesn't appear, we hung before the main loop
   (i.e. in `updatescreen()` itself — unlikely since `flushbuf` at
   end of `nexttoscreen` is reached).
2. Add `outstr("[B]"); flushbuf();` after `cursupdate()`.  If `[A]`
   appears but `[B]` doesn't, the hang is in `cursupdate`.
3. Add `outstr("[C]"); flushbuf();` after `windgoto(Cursrow,Curscol)`.
   If `[B]` appears but `[C]` doesn't, the hang is in `windgoto`
   (unlikely — windgoto worked for the probe).
4. Add `outstr("[D]"); flushbuf();` after `vgetc()`.  If `[C]` appears
   but `[D]` doesn't, `vgetc`/`inchar`/`getch` is the culprit.  If
   `[D]` appears, the loop IS running but `normal(c)` isn't
   producing visible output.

### Hypothesis B: `getch` (libstub) is broken

`_getch` in `minic/dos/libstub.asm` uses `INT 16h AH=00h`.  Should
work in DOSBox.  But after the function-key detection code path
(lines 617+) it returns a sentinel.  If the keystroke is being
mis-classified or the function-key state machine is stuck (e.g.
`fn_pending` non-zero from a previous launch), `getch` might keep
returning 0 indefinitely.

**Investigation steps:**
1. Check `_getch`'s function-key path:
   ```
   global _getch
   _getch:
       push bp
       mov bp, sp
       xor ah, ah
       int 16h
       cmp al, 0
       jne .ascii
       mov [cs:.fn_pending], ah
       mov byte [cs:.fn_flag], 1
       xor ax, ax
       pop bp
       retf
   ```
   If `fn_flag` is sticky / `ah` stays 0 the second time, j/k might
   never deliver an ASCII byte.  Verify the `.fn_flag` reset path.
2. Try pressing arrow keys instead of j/k.  Arrow keys use the
   function-key path; if they work, the issue is specific to ASCII
   delivery.

### Hypothesis C: `normal(c)` runs but redraw silently fails

If `vgetc` returns 'j' correctly, `normal('j')` updates Curschar.
Then loop iterates: `cursupdate` should compute new Cursrow,
`updatescreen` redraws (now WITH our `flushbuf` at end of
`nexttoscreen`).

**Investigation steps:**
1. After landing the `[A]/[B]/[C]/[D]` probes, add `[E]` inside the
   `if (State == NORMAL)` block before `normal(c)` and `[F]` after.
   See if we get past `normal('j')`.
2. If we do, the question is whether `updatescreen()` is being
   called on subsequent iterations.  `need_redraw` is probably
   FALSE after the initial draw, so `updatescreen()` is only
   called if `j` set `need_redraw = TRUE`.  Vim/stevie usually
   handles cursor moves via `windgoto` only (no full redraw),
   relying on `Cursrow/Curscol` being updated.  Verify `windgoto`
   is called with the new row.

## Diagnostic infrastructure already in place

- `flushbuf()` at end of `nexttoscreen` makes diffs immediately
  visible.  Keep this in place during cursor-key debugging.
- The `outchar/flushbuf` probe pattern from this session is the
  go-to diagnostic.  Probe-into-Nextscreen also works (see
  [[feedback-qbe-gcm-sinks-load-past-call]] for examples).

## Memory entries

- [[feedback-qbe-gcm-sinks-load-past-call]] — updated and marked
  **Fixed** (both spill.c paths).
- [[feedback-minic-ret-then-code]] — workaround still in place but
  no longer needed (the original filetonext body was restored
  during cleanup).

## Heisenbug lessons (carried forward)

- **Hints aren't enforcement.**  `sethint(v, caller-saves)` only
  tells rega what to *prefer avoiding*.  When callee-saves are
  oversubscribed, rega's fallback ignores the hint and clobbers
  the temp.  Use `limit2` to actually enforce.
- **rglob counts.**  On i8086, `ngpr=8` but allocatable GPRs = 6
  (RBP+RSP are rglob).  `limit2(v, k1, ...)` computes available
  as `ngpr - k1`, which over-counts by `nrglob` unless rglob is
  included in `k1`.
- **Caller-save preservation in libstub helpers.**  Anything called
  by sprintf/printf that uses BX/DI/SI for scratch needs to save
  them; sprintf passes its variadic-arg pointer in BX across
  helper calls.
- **Don't trust default flush behavior.**  `outchar` buffers; only
  certain code paths flush.  If a screen update needs to be
  visible *immediately* (not just by the next inchar), call
  `flushbuf()` explicitly.

## Reproduction

```sh
cd /Users/pauldevine/projects/qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.txt << 'EOF'
hello from vim
LINE 2
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" -c "stevie.exe HELLO.txt"
# Expected: row 0 shows "hello from vim", row 1 shows "LINE 2",
# rows 2-22 show "~", row 23 shows the status line.
# Try pressing j, k, h, l — cursor should move but currently doesn't.
```
