# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  Premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**

Build target: `stevie-orig/`.  Pipelines:
- `tools/build-stevie.sh` → small model, .COM via `nasm -f bin`.
- `tools/build-stevie.sh --model=medium` → medium model, .EXE via
  per-TU OMF objects + custom Python OMF→MZ linker.

## Strategic shift — full DOS memory-model matrix

Direction set by user: **stop trying to squeeze stevie into a .COM.**
Instead, support the full DOS memory-model matrix end-to-end so that
stevie picks the model that fits its needs.

```
  Memory   Code      Data      Default     Default
  Model    Model     Model     Code Ptr    Data Ptr
  -----    -----     -----     --------    --------
  tiny     small     small     near        near       (.COM)
  small    small     small     near        near       (.EXE, 1 CS / 1 DS)
  medium   big       small     far         near       (.EXE, multi-CS)
  compact  small     big       near        far        (.EXE, multi-DS)
  large    big       big       far         far        (.EXE)
  huge     big       huge      far         huge       (.EXE, items > 64KB)
```

For stevie specifically the immediate destination is **medium**: code
crosses 64KB (already at 80KB), data fits in 64KB.

## Where we are right now

**Step 4 in progress — stevie.exe accepts vi commands and renders
typed text under DOSBox.**  Press `i`, type chars, watch them appear
on screen (after a buffering delay because `flushbuf` only fires when
outbuf fills or is explicitly drained).  Cursor advances per keystroke.

**Latest commit: `2154af9` — "stevie: editor takes typed input,
displays text via teletype".**

### Bugs fixed this session

1. **minic: nested decl-init was hoisted to function entry.**
   `int v = *p++;` inside `if (cond) { ... }` evaluated the
   initializer unconditionally at the prologue.  The grammar rule
   `stmt: type IDENT '=' expr ';'` called `expr(init_node)` directly
   at parse time and returned `Stmt = 0`.  Fix in `minic/minic.y`:
   wrap the assignment as `mkstmt(Expr, init_node, 0, 0)` so it gets
   emitted in lexical order via `stmt()`.  Symptom: `vgetc()`'s
   `*getcnext++` ran when `getcnext` was NULL, so the editor read NUL
   chars from random DGROUP memory and never asked for keyboard input.

2. **qbe spill.c: caller-save avoid mask not applied to void calls.**
   `dopm()` only fired for calls followed by `Ocopy` (return-value
   handoff).  Void callees got no `hint.m`, so live-across temps
   landed in AX/CX/DX and were clobbered.  Fix in `spill.c`: in the
   per-instruction loop tail, OR the caller-save mask into `r` for
   `iscall(i->op)` before calling `sethint(v, r)`.  Repro:

   ```c
   extern void other(); extern int gA, gB;
   void test() {
     int r[4]; r[0]=r[1]=r[2]=r[3]=0;
     other(r); r[2] = gA * gB; other(r);
   }
   ```
   Pre-fix: `mov cx, &r[2]` set up before call, reused as `mov [bx],
   ax` after via `xchg bx, cx` — wild write since CX got clobbered.
   Post-fix: `&r[2]` lands in BX (callee-save), preserved across the
   call.  See
   `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/feedback_qbe_void_call_no_caller_save_hint.md`.

3. **qbe i8086/emit.c: Oloadub fast path clobbered base when dst
   aliased it.**  After `addr_fixup_reg` + `swap_bx` had routed the
   address through BX and rega put dst in BX too, the fast path
   emitted `xor bx, bx; mov bl, byte [bx]` — destroying the address.
   Fix: detect when the memref's base/index register equals dst and
   fall through to the AX-routed safe path that pushes/pops AX.
   Symptom in `windinit`: `bgn_mode`, `Columns`, `bgn_color` all read
   junk from DGROUP:0 instead of the BIOS-returned values.

4. **stevie workaround: flushbuf bypasses INT 10h AH=09.**  The
   write-char-with-attribute call doesn't render anything under
   DOSBox even with hardcoded attribute = 0x07.  Stripped the AH=09
   block and rely on the AH=0E (teletype) loop alone.  Root cause
   TBD.  Same edit hardcodes `bgn_color = 0x07; P(P_CO) = 0x07` to
   skip the second BIOS read while debugging.

### Workarounds carried forward (real fixes deferred)

- `host_type = hIBMPC` forced in `windinit` (far-pointer codegen bug
  for the `strncmp(ti_sig, ti_sig_addr, ti_sig_len)` call site).
- `flushbuf` skips AH=09 (see above).
- `bgn_color`/`P(P_CO)` hardcoded to 0x07.

CLS in `screenclear()` is now **restored** — the rega caller-save fix
made it safe.

## What still needs follow-up

- **Backspace blanks the screen and editor doesn't recover.**  In
  insert mode, deleting the most recent char triggers a screen-blank
  with no repaint after.  Likely another bad-codegen path in the
  delete/redraw code (probably `s_del`, `updateline`, or the `BS`
  handling in `edit.c`).  Repro: launch, press `i`, type chars,
  press backspace.  Add a probe in `s_del` and the delete-char
  branch of insert mode to find where it freezes.

- **AH=09 BIOS path doesn't render.**  When flushbuf calls INT 10h
  AH=09 with cx=1, no character appears at the cursor.  AH=0E
  (teletype) on the same struct works fine.  Possibilities: (a) DOSBox-
  specific quirk with cx=1; (b) one of the byte fields in REGS is
  still wrong; (c) the int86 libstub mishandles something specific
  to AH=09.  Try a hand-written asm test that calls INT 10h AH=09
  directly to isolate.

- **Buffering delay.**  Typed chars only appear after outbuf fills.
  Real stevie would flush more aggressively — probably tied to
  cursor/line updates.  Once backspace is fixed, audit the
  updateline/cursupdate paths.

- **Audit remaining libstub for BX clobbers.**  We've patched
  `_malloc` (in libstub_to_exe.py), `_int86`, `_intdos`.  `_putchar`,
  `_strrchr` already save BX correctly.  But `_atoi`, `_strchr`,
  `_strcat` are still `mov ax, 0; ret` stubs — they'll surface the
  moment stevie's parser uses them.

- **fprintf/file I/O stubs.**  Currently no-ops returning 0/-1.
  Once stevie tries to load a file the readfile path needs real
  fopen/fread.

- **The far-pointer codegen bug in minic.**  The `strncmp(ti_sig,
  ti_sig_addr, …)` call site emits broken code (uninitialised ES,
  etc.).  Currently sidestepped via `host_type = hIBMPC` hardcode.
  Worth its own debug session — check how minic lowers `char far *`
  arg passing.

## Build / run

```sh
# Build (medium model .EXE)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
# => build/stevie-orig/stevie.exe (~121 KB)

# Run under DOSBox
dosbox -conf /tmp/stevie_test.conf
```

DOSBox config (`/tmp/stevie_test.conf`):

```ini
[sdl]
output=opengl
fullscreen=false
[cpu]
cputype=386
core=normal
cycles=fixed 3000
[dos]
xms=true
ems=false
umb=false
[autoexec]
mount c /Users/pauldevine/projects/qbe/build/stevie-orig
c:
stevie.exe
```

Note: DOSBox 0.74-3 has no debugger.  All diagnostics go through
`putchar()` → `_putchar` → INT 10h teletype, visible on the BIOS
screen.  Take a screen recording — the program may exit before
you can read the trace.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/`:
- `feedback_minic_yacc_quirks.md` — miniyacc, varclr probe chains,
  uniform-* peeling, CRLF/0x1A, macOS sysroot trap.
- `feedback_minic_decl_init_hoisting.md` — **this session.**  Block-
  scoped `int v = expr;` was hoisted to function entry.  Fixed.
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git.
- `project_minic_pointer_bloat.md` — Path A landed; details what
  was changed and why the savings stopped at 17%.
- `feedback_libstub_ptr_abi.md` — near-pointer args = 2 stack bytes
  after Path A; far-pointer args = 4 bytes.
- `feedback_omf_pipeline_gotchas.md` — nasm -f obj strictness,
  segment naming, MZ header layout.
- `feedback_i8086_codegen_bugs.md` — five cascading codegen bugs.
- `feedback_i8086_loadub_dst_aliases_addr.md` — **this session.**
  Oloadub fast path clobbered address when dst register aliased
  the memref base.  Fixed.
- `feedback_qbe_caller_save_bug.md` — the rega CX-across-call bug
  (the original headline).
- `feedback_qbe_void_call_no_caller_save_hint.md` — **this session.**
  spill.c missed the avoid-mask hint for void calls; fix lifted the
  rega bug for stevie's BIOS wrappers.

## Useful one-liners

```sh
# Rebuild
make qbe && cd minic && make && cd ..

# Build stevie medium model (.EXE)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium

# Inspect the .EXE map
less build/stevie-orig/stevie.map

# Disassemble crt0
python3 -c '
import struct
d=open("build/stevie-orig/stevie.exe","rb").read()
hdr = struct.unpack_from("<H", d, 8)[0]
open("/tmp/crt0.bin","wb").write(d[hdr*16:hdr*16+44])
' && ndisasm -b 16 -o 0 /tmp/crt0.bin

# Verify rega caller-save fix still holds (should NOT show xchg bx, cx
# reusing CX after a `call far`)
echo 'extern void other();
extern int gA, gB;
void test(){int r[4];r[0]=0;r[1]=0;r[2]=0;r[3]=0;other(r);r[2]=gA*gB;other(r);}' \
    | ./minic/minic -m medium | ./qbe -t i8086 -m medium | grep -A8 'call far'

# Verify the minic decl-init fix (the deref+post-inc must be inside
# @l1, not in @l0 entry block)
echo 'extern int *p;
test(){if(p){int v=*p++;return v;}return 0;}' \
    | ./minic/minic -m medium

# Drive minic alone with a model selector
./minic/minic -m medium < build/stevie-orig/search.pp.c | head

# Pass/fail summary
for src in alloc cmdline dos edit enveval fileio help hexchars linefunc \
           main mark misccmds normal ops param ptrfunc regexp regsub \
           screen search sentence tagcmd undo version; do
    err=$(cat build/stevie-orig/$src.err 2>/dev/null | head -1)
    [ -z "$err" ] && echo "PASS: $src" || echo "FAIL: $src ($err)"
done

# Cherry-pick from upstream qbe
git fetch upstream
git log upstream/master --oneline -- spill.c rega.c isel.c
```
