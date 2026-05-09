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

**Step 3 in progress — stevie.exe boots and reaches its main edit
loop under DOSBox.**  The screen clears and a blinking cursor appears.
Vi commands typed at the keyboard are not picked up — the editor
loop runs but `vgetc`/`getch` aren't reaching the dispatcher.

### Trace last observed (medium-model stevie.exe)

```
XM 1 2 3 a b c d e f g h i A B C  [screen clears, blinking cursor, no input]
```

Decoded against the probes still in source:
- `X` — `_start` reached (crt0_exe.asm)
- `M` — DGROUP set; about to `call far _main`
- `1` — entered `main()`
- `2` / `3` — before / after `windinit()`
- `a..g` — each LPTR malloc returned
- `h` — `screenalloc()` succeeded
- `i` — `filealloc()` returned
- `A` — entered `screenclear()`
- `B` — past CLS (CLS is currently *skipped* — see workaround below)
- `C` — buffers blanked; about to return from screenclear
- (`D` was lost to a screen-clear race; happens elsewhere)

### Bugs fixed this session

1. **libstub `_int86` / `_intdos` clobbered BX**.  Same class as last
   session's `_malloc` BX clobber (BX is callee-save in cdecl/8086).
   Both stubs used BX to walk the REGS struct without `push bx /
   pop bx`.  Fixed in `minic/dos/libstub.asm` so caller's BX
   survives any `int86()` / `intdos()` call.

2. **libstub `_strcmp` / `_strncmp` were `mov ax, 0; ret` stubs**.
   Now real byte-by-byte 8086 implementations.  This was the headline
   bug.  Stevie's `windinit()` does:
   ```c
   host_type = strncmp(ti_sig, ti_sig_addr, ti_sig_len) ? hIBMPC : hTIPRO;
   ```
   Stub returned 0 → `host_type = hTIPRO` → `crt_int = 0x49` → every
   `int86(crt_int, …)` hit **INT 49h** (undefined on a PC).  DOSBox
   handled INT 49h as a benign no-op, which is why earlier sessions
   thought "msg() hangs" — actually screen output went nowhere because
   no INT 10h ever fired.

### Workarounds in place (real fixes deferred)

3. **Skip the strncmp(ti_sig, ti_sig_addr, …) call entirely**
   (`stevie-orig/dos.c`).  Force `host_type = hIBMPC` directly.
   minic's far-pointer codegen for that line is broken — emits
   `mov es, dx` with uninitialised DX, plus a `cwd` chain that
   sign-extends a near offset where a segment word should go.  Not
   the hill to die on; sidestep until the rest of the path is green.

4. **Skip `CLS` in `screenclear`** (`stevie-orig/screen.c`).  See
   below — this is the qbe rega caller-save bug.  `updatescreen()`
   will repaint the buffers anyway.

### The big remaining codegen bug — qbe rega + caller-save

Documented in
`~/.claude/projects/-Users-pauldevine-projects-qbe/memory/feedback_qbe_caller_save_bug.md`.

qbe's i8086 register allocator places SSA temps in **CX (or AX/DX
— all caller-save)** and keeps them live across `call far`.  Minimal
repro:

```c
// /tmp/repro.c
extern void other();
extern int gA, gB;
void test() {
    int r[4];
    r[0]=0; r[1]=0; r[2]=0; r[3]=0;
    other(r);
    r[2] = gA * gB;
    other(r);
}
```

Compile with `./minic/minic -m medium < /tmp/repro.c | ./qbe -t i8086
-m medium`.  Output shows `mov cx, &r[2]` set up before the first
call and reused as the destination address after the call:

```asm
    lea ax, [bp-16]
    mov cx, ax
    add cx, 8           ; cx = &r[2]
    xchg bx, cx
    mov word [bx], 0
    xchg bx, cx         ; cx still = &r[2]
    ...
    call far _other     ; CX clobbered here per cdecl
    mov ax, [_gA]
    mov dx, [_gB]
    imul dx
    xchg bx, cx         ; bx = whatever the callee left in cx (garbage)
    mov word [bx], ax   ; WILD WRITE
```

Why GVN/qbe ends up here: minic's REGS-zinit pattern emits multiple
`add %ptr, K` temps with the same operands (e.g. `&r[2]`).  qbe's
GVN merges them into one temp.  The merged temp is now live across
the call.  spill.c's `iscall` block calls `limit2(v, NGPS, NFPS, 0)`
to limit to callee-save count, but then `sethint(v, r)` hints toward
`r = T.rsave` (caller-save mask) — biasing rega the wrong way.
Suspicion: `sethint` should hint toward callee-save for live-across-
call temps, or the call needs an explicit clobber list.  Untested.

In stevie this hits every BIOS wrapper in `dos.c` that uses `union
REGS` + multiple `int86` calls: `bios_t_ed`, `bios_t_el`, `windgoto`,
`bios_t_il`, `bios_t_dl`.  Symptom is "wild write into DGROUP
somewhere", which sometimes lands harmlessly and sometimes (e.g. the
CLS path observed this session) garbles the BIOS arguments and turns
INT 10h AH=09 into a runaway screen-fill loop.

Also relevant: `_putchar` in libstub is a hand-rolled INT 10h call
that *does* preserve BX, so the diagnostic probes work.  It's the
qbe-emitted code that triggers the bug.

### What still needs follow-up

- **Why typed input doesn't reach the editor**.  After our changes
  stevie reaches the main `edit()` loop, but keystrokes don't take
  effect.  Likely candidates:
  - `_getch` / `_inchar` codegen (same rega-CX issue?)
  - `_int86` BX-save side-effect we missed
  - `vgetc()`'s buffer interactions
  Add a `putchar('K')` probe at the top of `edit()` and inside
  `vgetc()` to see if anything fires on keypress.

- **Fix the rega CX-across-call bug for real**.  The blunt fix is in
  `spill.c` near the `iscall` block.  Try changing `sethint(v, r)`
  so live-across-call temps get hinted toward callee-save (BX/SI/DI),
  not caller-save.  Verify with `/tmp/repro.c` — the post-call store
  must NOT use the same register that was set up pre-call.  Also
  check upstream qbe for related fixes.

- **The far-pointer codegen bug in minic**.  The `strncmp(ti_sig,
  ti_sig_addr, …)` call site emits broken code (uninitialised ES, etc).
  Currently sidestepped.  Worth its own debug session — check how
  minic lowers `char far *` arg passing.

- **Diagnostic probes are still in source**.  Remove before any
  release-quality run:
  - `minic/dos/crt0_exe.asm` — XMR probes
  - `stevie-orig/main.c` — `1`, `2`, `3`, `a`-`j`
  - `stevie-orig/screen.c` — `A`, `B`, `C`, `D`

- **Audit remaining libstub for BX clobbers**.  We've now patched
  `_malloc` (in libstub_to_exe.py), `_int86`, `_intdos`.  `_putchar`,
  `_strrchr` already save BX correctly.  Most other functions don't
  touch BX.  But `_atoi`, `_strchr`, `_strcat` are still stubs that
  return 0/-1 — they'll surface the moment stevie's parser uses them.

- **fprintf/file I/O stubs**.  Currently no-ops returning 0/-1.
  Once stevie tries to load a file the readfile path needs real
  fopen/fread.

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
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git.
- `project_minic_pointer_bloat.md` — Path A landed; details what
  was changed and why the savings stopped at 17%.
- `feedback_libstub_ptr_abi.md` — near-pointer args = 2 stack bytes
  after Path A; far-pointer args = 4 bytes.
- `feedback_omf_pipeline_gotchas.md` — nasm -f obj strictness,
  segment naming, MZ header layout.
- `feedback_i8086_codegen_bugs.md` — five cascading codegen bugs;
  this session added #5 (libstub functional stubs that "succeed"
  silently → wrong branches).
- `feedback_qbe_caller_save_bug.md` — the rega CX-across-call bug
  (this session's headliner).

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

# Verify rega bug repro still triggers
echo 'extern void other();
extern int gA, gB;
void test(){int r[4];r[0]=0;r[1]=0;r[2]=0;r[3]=0;other(r);r[2]=gA*gB;other(r);}' \
    | ./minic/minic -m medium | ./qbe -t i8086 -m medium | grep -A2 'call far' | head

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
