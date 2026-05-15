# Resume prompt — Stevie/QBE: readfile works, render loop is the bug

## Status (smoke-tested 2026-05-15)

Current `master` (HEAD `6f0c182`) **renders HELLO.txt's "2 line" status
bar correctly** but the screen body fills with a repeating `1`/`l`
pattern instead of the file contents.  Keyboard input is ignored
during the loop.

This is the same render-loop symptom from the previous session — my
session-2 commit `228eb27` (extending AX/DX preservation to every
remaining `Kl` handler) preserved the prior readfile-completes state
without introducing new regressions.  We're now firmly in the
"readfile works, screen rendering is broken" regime.

(Side note observed during debug: I rebuilt at commit `00c3667` and
its binary showed *only* the "HELLO.txt" prompt without "2 line".
The previous prompt had recorded "2 line, ? character + render loop"
for that state.  Difference is probably from non-emit.c QBE changes
since 00c3667 was committed — not relevant to the current bug, just
noting that the pre-state ≠ the originally recorded pre-state.)

## What landed this session

```
228eb27 i8086 emit: preserve AX/DX across remaining Kl handlers
6f0c182 NEXT_SESSION_PROMPT.md: Kl AX/DX preservation extended; DOSBox test pending
```

The session-2 commit added `kl_save_axdx` / `kl_restore_axdx` helpers
in `i8086/emit.c` and wrapped every `Kl` op that uses AX:DX as scratch
(Osub, Omul, Oload, Odiv/Orem/Oudiv/Ourem, Oextsw/Oextuw, all ten
`Oc*l` 32-bit comparisons).  See the commit message for details.

The 32-bit comparison wrap is particularly important — `Ocultl` /
`Oculel` are used by `screen.c`'s topchar/botchar checks.  Before
this commit they silently clobbered any rega-allocated live tmp in
AX or DX.  This was on the candidate-cause list for the render loop
but did NOT turn out to be the (sole) root cause.

## How to reproduce

```sh
make qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" \
       -c "stevie.exe HELLO.txt"
```

Expected today: `"HELLO.txt" 2 line, ? character` status, screen
body fills with `1`/`l` characters in rows.  Keyboard input ignored.

## What's still broken — the render loop

`updatescreen()` → `filetonext()` → `nexttoscreen()` is producing the
garbage screen.  `filetonext` walks the LINE list starting from
`*Topchar`, calling `gchar(&memp)` (`screen.pp.c:490`) for each char
and `inc(&memp)` (`screen.pp.c:491`) to advance.

### Suspect 1 — the LINE list itself

`lp->linep` (a near pointer) and `lp->linep->s` (a `char *` near
pointer to the line's text buffer).  `readfile` builds the doubly
linked list at `fileio.pp.c:548–554`:

```c
strcpy(lp->s, buff);
curr->next->prev = lp;
lp->next = curr->next;
curr->next = lp;
lp->prev = curr;
curr = lp;
```

If any of these stores go to the wrong place (e.g., `lp->s` ends up
pointing to a static string `"1"`, or `lp->next` cycles back to lp),
filetonext would loop or read garbage.

**Concrete probe**: insert `fprintf(stderr, "line %d: %s\n", linecnt,
lp->s);` after line 548 and run with `stevie.exe HELLO.txt > log
2>&1`.  Compare log against the file content.

### Suspect 2 — line numbers spuriously enabled

`params[14]` is the "nu" / number option, default `(0)` per
`param.pp.c:441`.  If it's reading non-zero (e.g., BSS not zeroed,
or `params[]` storage corrupted), `filetonext` would prepend `mkline(lno)`
to every line.  `mkline` writes a sparse string with the digits of
`n` reversed into a buffer; if `n` is corrupted to something like
65535 you'd get "5", "5", "3", "5", "6" reversed, which read at
`extra[--nextra]` produces "65535" — not "1"s, but a similar visual
flood.

**Concrete probe**: add `fprintf(stderr, "params[14]=%d lno=%d\n",
params[14].value, lno);` at the top of `filetonext`.

### Suspect 3 — `inc(&memp)` returning -1 prematurely

If `inc` returns -1 (end-of-file marker) on the first char,
`done = 1` is set immediately, the while loop exits, and the
fallback code fills rows with `~` (vim empty marker) or `@`
(line-too-long).  `~` is 0x7E, `@` is 0x40 — neither matches the
visible "1"/"l" pattern, so this is less likely.

### Suspect 4 — `Topchar` / `Curschar` / `Filemem` mis-init

`edit()` (`edit.pp.c:438–440`) does:

```c
*Topchar = *Filemem;
*Curschar = *Filemem;
Cursrow = Curscol = 0;
```

If `Filemem`'s `linep` points to the sentinel start (not the first
real line) — that's expected and `filetonext` should skip via `inc`
to the first real line.  But if any of the LPTR struct stores went
wrong, `Topchar` could point somewhere bogus.

## Next steps

1. **Add a printf probe to `filetonext` (Suspect 2 + Suspect 1).**
   Insert at the top:
   ```c
   fprintf(stderr, "filetonext: params[14]=%d Topchar.linep=%p .s=%p [%.20s]\n",
       params[14].value, Topchar->linep,
       Topchar->linep ? Topchar->linep->s : 0,
       (Topchar->linep && Topchar->linep->s) ? Topchar->linep->s : "");
   ```
   Need `stderr` redirected — DOSBox can do `c:\stevie.exe HELLO.txt
   2> err.log`.  Or write to a debug file via `fopen("c:\\dbg.txt","w")`.

2. **Audit `readfile`'s LINE-list construction** at
   `fileio.pp.c:548–554`.  The four stores happen in tight
   succession and at least one of them goes through a stack-slot
   reload — a prime candidate for rega clobber.  Diff
   `build/stevie-orig/fileio.asm` around `_readfile`'s line-build
   block against the post-Bug-A SSA at `fileio.ssa` for those lines.

3. **Strategic Kl AX/DX fix in isel.c** (still outstanding from the
   previous prompt).  Push/pop is tactical; modelling RAX/RDX as
   clobbers in `i8086/isel.c` would eliminate the per-handler
   wrapper and fix the "src in AX/DX" edge case.

4. **Audit Kl-in-single-reg lossiness.**  Not blocking HELLO.txt
   but matters for files >64KB and `%ld` printf.

## Memory entries that may need updates

- `feedback_minic_readfile_register_clobber.md` — scope is now
  broader than Bug B; the AX/DX implicit-clobber family applies to
  every emit.c handler that uses AX:DX as scratch.
- `feedback_minic_long_vararg_truncated.md` — Bug A is permanent;
  no change.

## Heisenbug lessons (carried forward)

- **rega's `visit` is sticky.** Fallback picks that violate the
  avoid mask must NOT propagate via `visit`.
- **Multi-word ops on a single-word ISA need wrapping.** Tactical
  push/pop landed in 228eb27; strategic isel-side fix still TODO.
- **Comparisons can clobber.** Even though `Oc*l` results are Kw,
  the body uses AX:DX — easy to miss.
- **Side-by-side binary tests catch state-of-mind drift.** Stage
  both pre and post binaries in `build/stevie-orig/` with distinct
  names so DOSBox's 8.3 mangling shows them as `STEVIE~1.EXE` and
  `STEVIE~2.EXE`; lets the user compare without rebuilding.
