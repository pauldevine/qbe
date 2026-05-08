# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  Premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**

Build target: `stevie-orig/`.  Pipeline: `tools/build-stevie.sh
--keep-going`.

## Where we are right now

**24/24 sources still compile and link to a ~98KB stevie.com.**
**Priority 1 codegen bug is fixed.**  Remaining blocker is .COM size
ceiling (Path A — minic pointer narrowing).

```
=== Build summary ===
  PASS: 24/24
=== Linking ===
  OK: build/stevie-orig/stevie.com (97982 bytes)
```

The binary still exceeds 64KB so DOSBox truncates it on load and
crashes once execution reaches the truncated upper half.  Path A or
Path B from the original prompt is required to actually run.

### What the LAST session changed

(Codegen bug + libstub fixes — see git log dc04ea7..HEAD~1.)

### What THIS session changed (Priority 1 codegen)

- **Fast-local Oalloc4/8/16 codegen** in `i8086/isel.c` and
  `i8086/emit.c`.  Mirrors the amd64 approach:
  - Preprocess pass in `i8086_isel()` scans the entry block for
    constant-size Oalloc4/8/16, assigns slot indices, divides size
    by 2 (slot stride is 2 bytes on i8086 vs. 4 on amd64), nops the
    instruction.
  - `fixarg()` now emits an `Oaddr` instruction for any tmp whose
    `slot` field is set, materializing `lea reg, [bp+offset]` per
    use (single-register because rega doesn't allocate pairs).
  - Excluded `Oaddr` from the Kl-specific switch in `emitins()` so
    it falls through to the format-string path (`lea %=, %M0`).
  - Added explicit `Oextsw`/`Oextuw`, `Oswap`, and `Odiv`/`Oudiv`/
    `Orem`/`Ourem` cases in the Kl switch.  No more "TODO: 32-bit
    op" markers anywhere in the build (was 42 before).

- **Sanity check** — `_getswitch()` now starts:
  ```asm
  _getswitch:
      push bp
      mov bp, sp
      sub sp, 32                   ; two alloc4 16's = 32 bytes
      lea ax, [bp-32]              ; address of inregs
      ...
  ```
  was previously `; XXX 32-bit op stub` followed by a write through
  uninitialized AX.

### What the PREVIOUS session changed (libstub fixes — already committed)

- **Real `_int86` and `_intdos`** in `minic/dos/libstub.asm` —
  replaced return-0 stubs with proper implementations.  Self-modify
  the INT immediate, load AX/BX/CX/DX/SI/DI from REGS, execute,
  store outputs back, preserve cflag/flags.
- **Real `_getch`** — INT 16h AH=00h, with the Microsoft-C-style
  convention where function keys return 0 first then the scancode on
  the next call.
- **Fixed pointer-ABI bugs in libstub**.  minic emits pointers as
  `l` (32-bit), so each pointer arg occupies 4 stack bytes, not 2.
  `_int86`/`_intdos`/`_strcpy`/`_strrchr` were all reading the
  *segment* word of arg N as the offset of arg N+1.  Now correct.

### What we learned trying to run stevie.com

Started DOSBox 0.74-3 on stevie.com.  It loaded (DOSBox truncates
oversize .COMs to 64KB without complaint), executed crt0, got into
windinit(), and crashed at `INT 21h AH=0x65` ("Unhandled country
information call").  That AH=0x65 is bogus — stevie never calls it.

After fixing the libstub pointer-ABI bugs the same crash still
fires.  Tracing the asm of `_getswitch()` in
`build/stevie-orig/dos.nasm.asm` revealed the real cause:

```asm
_getswitch:
    push bp
    mov bp, sp
    ; XXX 32-bit op stub - codegen incomplete   ; <-- THIS
    xchg bx, ax            ; AX is uninitialized!
    mov word [bx], 0       ; writes 0 to wherever-AX-pointed
    xchg bx, ax
```

The marker comes from `i8086/emit.c:1419`:
```c
fprintf(f, "\t; TODO: 32-bit op %d\n", i->op);
```
Op 81 = `Oalloc8`.  More importantly the same path swallows
`Oalloc4` and `Oalloc16` when the result class is `Kl`.

The SSA on the C side looks like:
```
%inregs =l alloc4 16
storew 0, %inregs        ; first init of the just-allocated REGS struct
```

minic emits `l`-typed alloc results because pointers are `l`.  The
i8086 emit path has the omap entries:
```c
{ Oalloc4,  0, "; alloc4 (stack slot allocated in prologue)" },
{ Oalloc8,  0, "; alloc8 (stack slot allocated in prologue)" },
{ Oalloc16, 0, "; alloc16 (stack slot allocated in prologue)" },
```
which produce a comment for the 16-bit (`Kw`) class only.  When the
class is `Kl` (32-bit), control falls into the unhandled-32-bit-op
arm of `i8086_emitins()` and emits the TODO comment instead of the
slot-address load.  No `lea ax, [bp - N]` ever happens, so AX stays
whatever-it-was-on-entry; the bx-swap fixup writes 0 through that
junk pointer.

Net effect: **every local struct or array allocated by C
(`union REGS`, anything with `&local`, all of stevie's per-function
buffers) is silently broken.**  That's why we crashed in the first
DOS-via-struct call.

### .COM size — still a problem, but second in line

Even once the alloc bug is fixed, the binary is 96864 bytes, way
past the .COM 64KB ceiling.  The early functions (`_int86` 0x369,
`_main` 0x9126, `_windinit` 0x307A) all happen to live in the first
37KB so DOSBox can run them — but anything that calls into the
truncated upper half hits zero pages and dies.  We need either a
binary-size fix (Path A below) or a real .EXE (Path B), and the
alloc bug fix will probably *grow* the binary, so this gets more
urgent rather than less.

## What's next — in dependency order

### Priority 1 — fix the `Kl` alloc4/alloc8/alloc16 codegen ✅ DONE

Fixed in this session — see "What THIS session changed" above.
Slot-allocation now happens in `i8086_isel()` preprocess, and
`fixarg()` materializes addresses via `Oaddr` per use.

### Priority 2 — get malloc returning real memory

`_malloc` in libstub returns 0.  Stevie's `main()` does
`(Filemem = malloc(...)) == NULL || ...` and bails to "Can't
allocate data structures" → `windexit(0)` → no UI.

Simplest implementation: bump-pointer in BSS.  ~30 lines of asm.

```asm
section .bss
_heap:    resb 8192       ; or larger — pick a size that fits
_heap_top equ $

section .text
_heap_ptr: dw _heap

global _malloc
_malloc:
    push bp
    mov bp, sp
    mov ax, [bp+4]              ; size (lo word of l arg)
    add ax, 1                   ; round up
    and ax, 0xFFFE              ; word align
    mov bx, [_heap_ptr]
    mov cx, bx
    add cx, ax
    cmp cx, _heap_top
    ja .fail
    mov [_heap_ptr], cx
    mov ax, bx                  ; ptr offset
    xor dx, dx                  ; segment relative to DS
    pop bp
    ret
.fail:
    xor ax, ax
    xor dx, dx
    pop bp
    ret
```

Caveat: returns DX:AX where DX=0.  Caller in small model uses the
offset only; high word is "DS-relative segment" = 0.  If the .EXE
path lands later, DX needs to be DGROUP/DS for far-pointer callers.

### Priority 3 — fix the .COM size ceiling

Two paths.  At least one of these has to land before stevie can
run end-to-end.

#### Path A — minic pointer-size bug (highest leverage)

`minic/minic.y` `irtyp()` and `irtyp_ret()` always return `'l'` for
`KIND(ctyp) == PTR || KIND(ctyp) == FUN`, with a misleading comment
saying "the i8086 backend handles the actual size difference."  It
does NOT — `l` is uniformly 32-bit on i8086, generating DX:AX pair
ops everywhere.

Concrete bloat (from `build/stevie-orig/stevie.full.asm`):
- 36514 instruction lines, ~2.7 bytes each = ~96KB code
- 2017 redundant `mov reg, reg` self-moves (4KB)
- 770 `mov / cwd / mov` sign-extend patterns
- 834 `xor / add / adc` 32-bit-add-via-pairs (mostly pointer arith)
- 482 `L_ceql_done` 32-bit-compare boolean materializers

Switching pointers to `w` (16-bit) for small/medium model should
roughly halve the binary and bring stevie under 64KB.

**Implementation sketch:**
1. Add `-m <model>` flag to minic, mirroring qbe's flag.
2. In `irtyp()`, return `'w'` for PTR/FUN when model is small/medium.
3. Ripple through: `loadl` → `loadw`, `storel` → `storew` for
   pointer values.  Many sites in minic.y use `irtyp()` already so
   they'll just propagate.
4. Update libstub stack offsets — pointer args revert to 2-byte
   slots.  `_intdos` outregs goes back to `[bp+6]`, `_int86` outregs
   to `[bp+8]`, `_strcpy` src to `[bp+6]`, `_strrchr` c to `[bp+6]`.
5. Audit `FP_SEG`/`FP_OFF` / far-pointer code; far pointers stay
   `l`-typed regardless of model.
6. The Priority 1 alloc-bug fix probably becomes a no-op for stevie
   in this path (alloc results become `w`-typed and use the existing
   16-bit comment-only emit), but **fix it anyway** since `long` and
   true 32-bit pointers still need it.
7. Rebuild and measure.

This is the most-impactful change available.  No new linker code,
just a real-world memory-model implementation that minic should have
had from the start.

#### Path B — Multi-segment .EXE (broader runway)

NASM `-f bin` does NOT support segment-base references — confirmed
by experiment ("binary output format does not support segment base
references").  So we can't do multi-segment from one bin file.

QBE backend already supports `-m medium` correctly: confirmed it
emits `proc far / call far / retf` on cross-function calls via
`./qbe -t i8086 -m medium build/stevie-orig/alloc.ssa`.

To produce a real .EXE:
1. Compile each TU with `qbe -t i8086 -m medium`.
2. Assemble each TU with `nasm -f obj` (NASM emits OMF natively).
3. Run a real DOS linker.  Options:
   - **wlink (Open Watcom)** — best, but needs to be built from
     source on macOS.  No homebrew package.
   - **Custom Python linker** — write one against NASM's OMF output
     subset.  ~500-800 lines.  OMF reference at Open Watcom docs.
4. Update `tools/build-stevie.sh` to feed the new toolchain.
5. crt0 needs an MZ-EXE-style entry that sets DS to DGROUP (not
   the PSP segment).

Path A is preferred.  Path B is a fallback if pointer-narrowing
breaks too much of minic.

### Priority 4 — actually run it end-to-end

Once Priority 1 and at least one of Priority 3 lands:

```sh
dosbox build/stevie-orig/stevie.com   # or stevie.exe
```

Likely-failure stack:
1. crt0 — segment setup, stack init.
2. windinit — first DOS interaction.  `_int86`/`_intdos` are now
   real, so this should mostly work.
3. malloc — covered by Priority 2.
4. Far pointer arithmetic — `0xF400800D` access for TI Pro
   detection; should fail strncmp and fall back to IBM PC.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/`:
- `feedback_minic_yacc_quirks.md` — miniyacc, varclr probe chains,
  uniform-* peeling, CRLF/0x1A, macOS sysroot trap.
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git; check `git log upstream/master -- spill.c` (or
  whatever) before debugging generic qbe bugs.
- `project_minic_pointer_bloat.md` — minic emits `l` for all
  pointers regardless of model.  Root cause of 96KB stevie.com.
  Path A above.
- `feedback_libstub_ptr_abi.md` — pointer args occupy 4 stack
  bytes (l-typed).  When auditing hand-written asm called from
  minic-compiled C, count from the SSA, not the C signature.

## Useful one-liners

```sh
# Rebuild
make qbe && cd minic && make && cd ..

# Build stevie
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going

# Pass/fail summary
for src in alloc cmdline dos edit enveval fileio help hexchars linefunc \
           main mark misccmds normal ops param ptrfunc regexp regsub \
           screen search sentence tagcmd undo version; do
    err=$(cat build/stevie-orig/$src.err 2>/dev/null | head -1)
    [ -z "$err" ] && echo "PASS: $src" || echo "FAIL: $src ($err)"
done

# Find every TODO-32-bit-op in the build (each is a real codegen bug)
grep -rn "TODO: 32-bit op\|XXX 32-bit op stub" build/stevie-orig/*.asm | head

# Confirm getswitch is fixed: should NOT contain "32-bit op stub"
grep -A 5 "^_getswitch:" build/stevie-orig/dos.nasm.asm

# Verify -m medium emits far code
./qbe -t i8086 -m medium build/stevie-orig/alloc.ssa | grep -E "proc far|retf|call far" | head

# Run in DOSBox.  Until Priority 1 fix lands, this crashes at the
# first storew through an alloc4 pointer.  After Priority 1 + 2 fix
# lands but before Priority 3, it'll get further then fall over
# truncated code.
cat > /tmp/dosrun.conf <<'EOF'
[autoexec]
mount c /Users/pauldevine/projects/qbe/build/stevie-orig
c:
stevie.com
exit
EOF
dosbox -conf /tmp/dosrun.conf

# Cherry-pick from upstream qbe
git fetch upstream
git log upstream/master --oneline -- spill.c rega.c isel.c
git cherry-pick <sha>

# Inspect the pipeline for one file
cat build/stevie-orig/<base>.pp.c    # cpp output (post tr/sed)
cat build/stevie-orig/<base>.ssa     # minic's IR
cat build/stevie-orig/<base>.asm     # qbe i8086 emit
cat build/stevie-orig/<base>.nasm.asm
```
