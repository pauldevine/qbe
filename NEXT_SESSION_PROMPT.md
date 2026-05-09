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
stevie (and any other workload) picks the model that fits its needs.

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
crosses 64KB (already at 80KB), data fits in 64KB, libstub doesn't
need to grow (`union REGS *` stays a 2-byte near pointer).

## Where we are right now

**Steps 1 + 2 are done; Step 3 in progress** — medium-model `stevie.exe`
builds clean *and now executes through `windinit`, the malloc loop,
`filealloc`, and `screenclear` under DOSBox*.  Hangs in `msg("Empty
Buffer")` (gotocmd / outstr / flushbuf).

### Codegen + runtime bugs fixed this session (the big wins)

The previous build assembled fine but produced wrong code at runtime.
Stevie hung in `_main` immediately because four interlocking bugs in
the i8086 backend + libstub corrupted register state across calls:

1. **i8086/abi.c — argument lowering** (most-impactful).  `selcall`
   used `sub sp, stk; mov [bp-N], val` to push args, but `[bp-N]`
   addresses inside the function's local frame, not the freshly-
   allocated arg region (BP is offset from SP by `2*fn->slot`).  Args
   were silently written into the locals area; `[SP]` was left
   uninitialized; callees read stack garbage.  Fix: pre-pass all blocks
   to compute max arg-region size, reserve that many slots at the
   bottom of the locals frame at function entry (so slot 0 sits exactly
   at `[SP]` after prologue), use `SLOT(j)` refs for arg stores, drop
   per-call `sub sp` / `add sp` brackets entirely.  Side-effect: code
   shrank ~5.5KB from removing the now-unnecessary SP fixups.

2. **i8086/emit.c — two-address constraint fixup**.  The omap entries
   `add %=, %1`, `sub`, `and`, `or`, `xor` encode the x86 read-modify-
   write constraint (dest must already hold arg[0]).  rega's coalescer
   *hints* but doesn't guarantee this.  When dest and arg[0] landed in
   different registers, the pre-mov got dropped and the asm became
   `add dx, 16` with DX uninitialized.  Fix: before `emitf`, scan fmt
   for `%=`+`%1`; if matched and dest != arg[0] reg, emit a `mov dest,
   arg[0]` first (handling RTmp / RCon / RSlot operand forms).  The
   shift handling already had this fixup; we generalized.

3. **i8086/emit.c — branchy materialize ordering for cnew/ceqw etc**.
   The 8086-compatible `cmp/setcc` substitute emitted `mov dst, 1; cmp
   arg0, arg1; jcc; mov dst, 0`.  When dst aliased arg0 (both → AX),
   the `mov dst, 1` clobbered arg0 *before* the cmp ever read it, so
   every comparison evaluated against `1`, not the loaded value.  We
   saw `if (Filename != NULL)` always take the true branch with argc=0.
   Fix: do `cmp` first, *then* materialize dst — `mov` doesn't touch
   flags on 8086, so the jcc still sees the cmp result.

4. **libstub `_malloc` — clobbers callee-save BX**.  cdecl on 8086
   makes BX/SI/DI callee-save.  qbe-emitted code relies on this (does
   not save BX itself, so it must trust callees).  Our libstub_exe
   `_malloc` used `mov bx, [_heap_ptr]` and didn't restore.  Pattern:
   `newline()` calls `alloc()` which calls `_malloc`; on the 3rd call
   into newline, `nchars+1` (held in BX) was destroyed and the LINE's
   `size` field got `_heap_ptr` written into it.  Fix: bracket _malloc
   body with `push bx` / `pop bx` in `tools/libstub_to_exe.py`.

   *Audit not yet done* on the rest of libstub for similar clobbers.
   Other functions almost certainly have the same problem (e.g.
   `_strlen`, `_strcpy`, `_strcmp` use registers freely without
   saves).  Worth a sweep before dialling in stevie's hot paths.

5. **libstub `_putchar` — actually emits via INT 10h**.  Was a no-op
   stub returning 0 in AX.  Now writes the arg byte via BIOS teletype
   (AH=0Eh) so we can use `putchar()` for printf-debugging from any C
   source.  This is what produced the `S1234abcdefgh56qrsm` trace.

### Current observed run

```
S 1 2 3 4 a b c d e f g h 5 6 q r s m  [HANG]
```

Decoded against probes (the probes have since been removed; they were
in main.c/alloc.c/crt0_exe.asm):

- `S` — crt0 reached the far-call to _main
- `1` — entered main; argc handled (argc=0 path, Filename=NULL)
- `2`/`3` — windinit() before/after (INT 10h video-mode probe + signal
  setup ran clean)
- `4` — 7 LPTR mallocs + screenalloc (Realscreen + Nextscreen, 2*Rows*
  Columns = 4000 bytes) succeeded
- `a..h` — filealloc completed: 3 newlines (each = 2 mallocs), index
  zeroing, list linking, clrall, u_clear
- `5`/`6` — main resumed; screenclear()
- `q`/`r` — getenv("EXINIT") → returned NULL (stub)
- `s` — Filename check; took the else-branch since Filename==NULL
- `m` — entered `msg("Empty Buffer")`; **hangs inside msg**

`msg(s)` in `cmdline.c` is `gotocmd(TRUE, 0); outstr(s); flushbuf();`.
Hang is in one of those three; bisect with `putchar()` probes, or
trace into outchar/INT 10h (cursor positioning + character output).

- `tools/build-stevie.sh` accepts `--model=medium` (auto-implies
  `--exe`).  All 24 TUs compile + assemble + link.
- Output: `build/stevie-orig/stevie.exe` (126 KB MZ EXE, 1247
  relocations, 26 modules linked).
- Map file at `build/stevie-orig/stevie.map`.
- Entry shellcode at CS:IP = 0:0 disassembles cleanly:
  `mov ax,DGROUP; mov ds,ax; push 0,0; call far MAIN_TEXT:_main`.

```
# small (default) — bit-identical to pre-session baseline
tools/build-stevie.sh --keep-going
=> build/stevie-orig/stevie.com (80802 bytes)

# medium — full pipeline working
tools/build-stevie.sh --keep-going --model=medium
=> build/stevie-orig/stevie.exe (126336 bytes)
```

### What this session added

- `tools/asm_to_omf.py` — wraps qbe i8086 `.asm` output as proper OMF
  NASM source.  Tracks `.text/.data/.bss` sections, generates
  `extern`/`global` lists, applies the syntax transforms previously
  done by sed/perl in build-stevie.sh.  Auto-exports every `_xxx:`
  label (matches C's default external linkage — minic doesn't emit
  qbe `export` markers for file-scope data).  Uses per-module code
  segment names (`<MODULE>_TEXT class=CODE`) so the linker keeps
  each TU's code in its own physical 64KB segment (medium-model
  multi-CS).

- `tools/omf_link.py` — pure-Python OMF→MZ linker (~720 LOC, stdlib
  only).  Parses Microsoft OMF records (THEADR, LNAMES, SEGDEF,
  GRPDEF, EXTDEF, PUBDEF, LEDATA, LIDATA, FIXUPP, MODEND, plus
  parse-and-discard for COMDEF/COMDAT/LINNUM/etc).  Layouts CODE
  segments distinct (medium model multi-CS), coalesces DATA + BSS
  into DGROUP, emits standard 28-byte MZ header with relocation
  table.  Handles fixup locations 0/1/2/3/5/9/13 + frame methods
  segment/group/external/preceding-frame/target-frame.  Test rig at
  `tools/test_omf_link.sh`.

- `minic/dos/crt0_exe.asm` — .EXE entry stub.  Sets DS=DGROUP, pushes
  argc=argv=0, far-calls `_main`, exits via INT 21h AH=4Ch.

- `tools/libstub_to_exe.py` — converts `minic/dos/libstub.asm`
  (small-model, near-call ABI) into `libstub_exe.asm` (medium-model,
  far-call ABI).  Mechanical transforms: `ret`→`retf`, `[bp+N]`→
  `[bp+(N+2)]` for positive N (far-call return address occupies an
  extra 2 bytes between saved bp and first arg).  Replaces the
  .COM-specific `_malloc`/`_free` (which references the
  `_heap_end_of_image` post-image label) with .EXE versions that
  bump from a fixed 32 KB `_heap_buf` in `_BSS`.

- `i8086/emit.c` — removed the redundant `<name> proc far` MASM
  directive (NASM doesn't need it; was creating a duplicate label
  with no `_` prefix).  RETF emission unchanged.

### What still needs follow-up

- **minic emits 8-byte data pointers for medium model.**  e.g.
  `data $Version = { l $glo1 }` — `l` (8 bytes) is wrong for
  medium-model near data (should be `w`, 2 bytes).  qbe emits `dq`
  which NASM zero-extends; functionally correct (low 2 bytes are
  the offset) but wastes 6 bytes per file-scope pointer initializer.
  Track in feedback memory; non-blocking.

- **qbe's Ocopy with RSlot dest emits `mov [bp+N], <symbol>`** with
  no size qualifier.  `nasm -f bin` accepts (defaults to word);
  `nasm -f obj` rejects (relocation size ambiguous).  Worked around
  in `asm_to_omf.py` by promoting bare slot stores to
  `mov word [bp+N], ...`.  Real fix is in `i8086/emit.c` Ocopy
  emission (the format-specifier `%=` should add a size prefix when
  expanding to a slot operand).

- **No DOSBox runtime test.**  `stevie.exe` is byte-correct on
  inspection but hasn't been launched.  Step 3.

## What's next — in dependency order

### Step 3 — exercise medium model under DOSBox  ⬅ IN PROGRESS

**Goal:** stevie actually loads a file and renders the screen.

DOSBox config used for testing (`/tmp/stevie_test.conf`):

```ini
[sdl]
output=texture
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

Run with `dosbox -conf /tmp/stevie_test.conf`.  At time of writing:
loads → crt0 → main → past windinit/malloc/filealloc/screenclear →
hangs in `msg("Empty Buffer")`.

#### Immediate next step: bisect `msg`

`msg(s)` calls `gotocmd(TRUE, 0)` (sets cursor to last row), then
`outstr(s)` (per-char output), then `flushbuf()`.  Add `putchar('A')`
probes around each in `stevie-orig/cmdline.c:msg` to identify which.
Likely candidates:

- `gotocmd` does an `int86()` call (INT 10h cursor position).  Our
  libstub `_int86` is a real implementation; verify it preserves BX
  and that the REGS struct round-trip works.
- `outstr` calls `outchar` per character; `outchar` ultimately wraps
  INT 10h teletype.  Should work — same path as our `_putchar`
  diagnostic.
- `flushbuf` may try to write to a stdio FILE* that isn't real.

#### Probable next-up bugs (order them when you hit them)

- **libstub callee-save audit**.  We patched `_malloc` to save BX.
  Other libstub functions that touch BX/SI/DI without saving will
  manifest as "value mysteriously changes after call".  Look at
  `_strlen`, `_strcpy`, `_strcmp`, `_strncmp`, `_strchr`, `_strcat`,
  `_strncpy`, `_atoi`, `_int86` (probably the worst offender, since
  it sets up REGS).
- **Far-pointer detection at 0xF400:800D** (TI Pro signature).  If
  stevie ever does `strncmp(ti_sig, ti_sig_addr, ...)` on a real far
  pointer, our small-model `_strncmp` won't read across segments.
  Stevie's source treats `ti_sig_addr` as `char far *`, so minic must
  compile that as a 4-byte pointer with seg:off semantics.
- **fprintf/file I/O stubs**.  Currently no-ops returning 0/-1.  msg
  doesn't go through fprintf, but once stevie tries to load a file
  the readfile path will need real fopen/fread.
- **screen.c repaint pipeline**.  After msg works, the next thing
  stevie does is enter `edit()` which calls `screenupdate()` to draw
  the buffer.  That uses the Realscreen/Nextscreen diff buffers we
  malloc'd; render via outchar.  Should be reachable once msg unhangs.

Tooling notes:
- Stock DOSBox 0.74-3 has no built-in debugger — use BIOS teletype
  probes or set up DOSBox-x for source-level breakpoints.
- The S/E probes in crt0 + the bisect probes in main.c/alloc.c that
  produced the `S1234abcdefgh56qrsm` trace have been reverted.  Add
  them back as needed.

### Step 4 — fill in compact / large / huge

Defer until medium runs.  Each one needs:
- minic data-pointer rules (already covered by step 1's table).
- libstub variant with 4-byte data-pointer args.
- Possibly per-model `crt0` adjustments.
- huge specifically needs segment-arithmetic helpers for
  `array[i]` where `i * sizeof > 64KB`.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/`:
- `feedback_minic_yacc_quirks.md` — miniyacc, varclr probe chains,
  uniform-* peeling, CRLF/0x1A, macOS sysroot trap.
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git.
- `project_minic_pointer_bloat.md` — Path A landed; details what
  was changed and why the savings stopped at 17%.
- `feedback_libstub_ptr_abi.md` — near-pointer args = 2 stack bytes
  after Path A; far-pointer args = 4 bytes.  This goes back in
  flux for compact/large/huge.

## Useful one-liners

```sh
# Rebuild
make qbe && cd minic && make && cd ..

# Build stevie (default = small, .COM)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going

# Build stevie medium model (.EXE)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium

# Inspect the .EXE map
less build/stevie-orig/stevie.map

# Decode the MZ header
python3 -c '
import struct
d=open("build/stevie-orig/stevie.exe","rb").read()
print(struct.unpack_from("<2sHHHHHHHHHHHHH", d, 0))
'

# Run the OMF linker test rig
tools/test_omf_link.sh

# Drive minic alone with a model selector
./minic/minic -m medium < build/stevie-orig/search.pp.c | head

# Pass/fail summary
for src in alloc cmdline dos edit enveval fileio help hexchars linefunc \
           main mark misccmds normal ops param ptrfunc regexp regsub \
           screen search sentence tagcmd undo version; do
    err=$(cat build/stevie-orig/$src.err 2>/dev/null | head -1)
    [ -z "$err" ] && echo "PASS: $src" || echo "FAIL: $src ($err)"
done

# Verify qbe -m medium emits far code
./qbe -t i8086 -m medium build/stevie-orig/alloc.ssa | grep -E "retf|call far" | head

# Inspect a single-TU OMF wrap
tools/asm_to_omf.py alloc build/stevie-orig/alloc.asm /tmp/alloc.omf.asm
nasm -f obj /tmp/alloc.omf.asm -o /tmp/alloc.obj
file /tmp/alloc.obj

# Cherry-pick from upstream qbe
git fetch upstream
git log upstream/master --oneline -- spill.c rega.c isel.c
```
