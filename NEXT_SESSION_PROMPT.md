# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  Premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**

Build target: `stevie-orig/`.  Pipeline: `tools/build-stevie.sh
--keep-going`.

## Where we are right now

**24/24 sources compile and link to a 96KB stevie.com.**

```
=== Build summary ===
  PASS: 24/24
=== Linking ===
  OK: build/stevie-orig/stevie.com (96705 bytes)
```

Last session (12/24 → 19/24) closed all minic grammar gaps in the
core 19.  This session (19/24 → 24/24) cleared the rest:

- Cherry-picked **upstream qbe commit 6a2dca8** ("fix jmp arg
  spilling").  This was the root cause of three sources segfaulting
  inside qbe (`ops`, `screen`, `tagcmd`, plus `search` after its
  grammar fix landed).  `reloads()` was being called before
  `curi = &insb[NIns]` reset in spill.c, corrupting curi.
- Block-scope `static T *NAME[] = { "s1", "s2", ... };` (dos.c's
  disclaimer2 array).
- Top-level `T NAME[] = { ... };` extends to pointer-typed elements.
- `dcls type '(' '*' IDENT ')' '(' fptpar0 ')' ',' ext_decllist ';'`
  for `int (*move)(), inc(), dec();` (search.c).
- `sizeof(IDENT)` for variables (was type-only).
- `word ptr SEG:[...]` segment-override translation in
  build-stevie.sh.
- libstub stubs for getch, int86, intdos, signal, sleep, stat,
  chmod, mktemp, islower, isupper, strrchr.

## What's next

The stevie.com binary **links but does not yet run**.  At 96KB it's
already past the .COM 64KB limit, and the libstub.asm calls are
no-ops, so even shrunk it would just sit there.

### Priority 1 — switch to .EXE (multi-segment)

The .COM format caps the whole image at 64KB (code + data + BSS in
one segment).  With QBE i8086 in small model and stevie's data
footprint, we're already at 96KB.  Options:

- **Stay at .COM but reduce footprint.**  Strip unused code/data,
  shrink string tables, split stevie into a smaller subset.  Likely
  not feasible without losing features.
- **Switch to .EXE format.**  Use NASM `-f bin` is fine; we just
  need an .EXE header and a stack segment.  Requires changes to
  `tools/build-stevie.sh` (header bytes + segment directives) and
  possibly `crt0.asm`.
- **Use a real DOS linker.**  `wlink` or `optlink` from a DOS dev
  toolkit.  Outputs proper .EXE / .OBJ flow.

The .EXE approach is the standard one and what real Stevie used.

### Priority 2 — flesh out libstub stubs

Most of the runtime is currently stubs returning 0:

- `_int86`, `_intdos` — these need to actually issue the requested
  interrupt with the regs in the union REGS argument.  Doable in
  ~30 lines of inline asm.  Critical for stevie's screen output.
- `_getch` — INT 21h AH=08h (read char, no echo).
- `_signal` — can stay as no-op.
- `_sleep` — stub; doslib already has _delay.
- `_stat` / `_chmod` / `_mktemp` — DOS file I/O via INT 21h.
- `_islower` / `_isupper` / `_strrchr` — already have minimal
  implementations; ASCII-only is fine for stevie.

### Priority 3 — actually run it

```sh
dosbox build/stevie-orig/stevie.com   # if a .EXE conversion lands
```

If it crashes or freezes, suspect order:
1. crt0 — check segment setup, stack init.
2. windinit — first DOS interaction; int86 must actually work.
3. malloc — libstub returns 0 for everything; main() bails.
4. Far pointer arithmetic — `0xF400800D` access is in dos.c's
   TI Pro detection; should fail strncmp and fall back to IBM PC.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/`:
- `feedback_minic_yacc_quirks.md` — miniyacc, varclr probe chains,
  uniform-* peeling, CRLF/0x1A, macOS sysroot trap.
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git; check `git log upstream/master -- spill.c` (or
  whatever) before debugging generic qbe bugs.

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

# Cherry-pick from upstream qbe (this saved us once)
git fetch upstream
git log upstream/master --oneline -- spill.c rega.c isel.c
git cherry-pick <sha>

# Inspect the pipeline for one file
cat build/stevie-orig/<base>.pp.c    # cpp output (post tr/sed)
cat build/stevie-orig/<base>.ssa     # minic's IR
cat build/stevie-orig/<base>.asm     # qbe i8086 emit
cat build/stevie-orig/<base>.nasm.asm
```

## When the build runs

Once stevie.com (or stevie.exe) actually executes:

```sh
dosbox build/stevie-orig/stevie.com
```

If it crashes, the libstub functions in `minic/dos/libstub.asm`
are the first suspect.  Suggested order: crt0 → windinit → malloc
→ DOS API.
