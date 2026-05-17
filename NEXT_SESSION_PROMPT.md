# Resume prompt — Stevie cursor keys mostly work; boundaries still blank screen

## Status (2026-05-17)

Cursor motion fundamentally works for the first time:
- `j`/`k`/`h`/`l` move the cursor across lines within the file.
- `G` jumps to the last line.
- `<ESC>:q!<CR>` quits.

Remaining issues, all on the same code path (one-past-the-end of file/line):
- `l` at end-of-line → screen text blanks (cursor remains usable).
- `j` past the last row → same.
- `gg` (not actually a stevie command — falls through to default beep) → blanks the screen.

## Root causes fixed this session

### 1. `i8086/emit.c` `Ostorel` clobbered the destination address (commit not yet made)

When the destination address was rega-allocated to AX or DX, the value
load (`mov ax, <const>` / `cwd`) clobbered the address BEFORE the store
emitted `mov bx, ax`.  Net effect: every `storel <const>, <ptr>` was a wild
write to the constant value rather than the pointer.  Most visibly,
`alloc.c:103`'s `Fileend->linep->num = 0xffff;` silently never happened,
so every `LINEOF(Fileend)` returned 0 and every walking loop in stevie
was confused.

Fix: capture the destination address into BX *before* the value load
clobbers AX/DX.

### 2. `i8086/emit.c` `Ostorel` and `Oload` didn't preserve BX

`Ostorel`/`Oload` use BX as the address-staging scratch register.  rega
doesn't model that clobber, so any live SSA temp it placed in BX got
overwritten silently.  Smoking gun: the iter counter in our
diagnostic loop ended up at `&num` instead of `iter+1`.

Fix: `push bx` / `pop bx` around the storel/loadl sequence when BX is
actually used as scratch (skipped when r1 or r0 already lives in BX).

### 3. MiniC `static` for function-local variables doesn't actually persist

Workaround applied in stevie source.  Code review of `minic.y`
`emit_local_init` (4391+, 4403+) shows STATIC locals are emitted as
`alloc4 N` (stack allocation, same as auto).  Returning the address of
such a variable dangles after the function returns — subsequent calls
overwrite the stack slot.  The comment at minic.y:4410 acknowledges this
is "invisible for single-function helpers" — but stevie's
`nextline()` / `prevline()` / `coladvance()` etc all return `&static_local`
and ABSOLUTELY rely on persistence.

Symptom: cursupdate's `for (p=Topchar; p->linep != Curschar->linep;
p = nextline(p))` walked 11+ times in a 3-line file before our
diagnostic break, because each iteration's returned &next pointed to a
stack slot that `plines(p)` then overwrote.  The comparison cycled
forever instead of terminating at Curschar.

Fix (stevie-source workaround): promote `static LPTR next;`,
`static LPTR prev;`, `static LPTR lp;`, `static LPTR pos;`,
`static LPTR l;` to file-scope statics in linefunc.c, misccmds.c,
cmdline.c.  These are all functions that return the address of the
static so its persistence matters.

**Better fix (not done):** teach minic's emit_local_init to emit STATIC
locals as global storage with a mangled name (e.g.
`_<func>_<var>`) so source code stays portable.  The grammar already has
the STATIC token; only the emit side needs work.

### 4. Other stuff in working tree from earlier sessions (kept)

- `spill.c`: i8086-gated Kl forcing to slots, caller-save handling fixes.
- `rega.c`: accept RSlot destination for slot-resident Kl temps.
- `i8086/isel.c`: use original temp class for fast-local address materialisation.
- `minic.y`: word-by-word struct copy expansion at line 1915+ (otherwise
  `*Curschar = *Filemem;` truncated to 2 bytes).

## What remains broken

When pressing a key that beeps (l-past-EOL, j-past-EOF, unknown command
like the first `g`), the screen text disappears.  Cursor positioning
remains functional after the blank — you can navigate, just nothing's
visible.

Initial reading of the path (oneright -> inc returns 1 -> dec rewinds
-> return FALSE -> beep() -> vbeep()) shows no obvious write to
Topchar/Curschar/Nextscreen.  vbeep just toggles `P(P_CO)` and calls
flushbuf/windgoto.  So the blank is most likely happening on the NEXT
cursupdate/updatescreen cycle, triggered by some lingering corrupted
state.

Hypotheses to investigate:
1. `need_redraw` getting flipped TRUE during the beep flow, then on
   the next iteration `updatescreen()` runs with Topchar/Botchar in a
   bad state (one of the LPTR struct copies still silently truncated
   somewhere?).
2. Another minic `static LPTR` survivor in screen.c or misccmds.c
   (mkline's `static char lbuf[9]` at screen.c:402 returns its address).
3. `lfiletonext()` / `filetonext()` rendering with `Botchar` left from a
   prior call where filetonext's "didn't fit on screen" branch's
   `*Botchar = save;` got truncated.  The struct-copy fix in minic.y
   covers this in theory, but worth double-checking.

## Verification

- QBE test suite: 59/62 (3 pre-existing arm64 `far_pointer`/`float_simple`
  failures, unrelated).
- Stevie compiles (24/24 sources), links to ~142 KB medium-model .EXE.
- Cursor motion j/k/h/l/G/q! works in DOSBox with HELLO.TXT.

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

## Memory entries worth checking next session

- `[[feedback-i8086-storel-clobbers-dest]]` (to be added) — the storel
  destination-address-clobber bug.
- `[[feedback-i8086-storel-loadl-bx-clobber]]` (to be added) — BX not
  preserved across Kl ops, same shape as the AX/DX clobber bugs.
- `[[feedback-minic-static-local-not-persistent]]` (to be added) — minic
  treats `static <type> <var>;` inside a function as stack alloc.
