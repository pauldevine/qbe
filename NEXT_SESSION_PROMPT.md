# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  Premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**

Build target: `stevie-orig/`.  Pipeline: `tools/build-stevie.sh
--keep-going`.

## Where we are right now

**24/24 sources still compile and link.**  Path A (near-pointer
narrowing) landed in commit 5125e70 and dropped stevie.com from
97982 → 80922 bytes (~17%).  **Binary still exceeds the .COM 64KB
ceiling**, so DOSBox truncates and we crash before reaching anything
useful.  Either further bloat reduction or a real .EXE (Path B) is
required to actually run end-to-end.

```
=== Build summary ===
  PASS: 24/24
=== Linking ===
  OK: build/stevie-orig/stevie.com (80922 bytes)
```

### What THIS session changed (Path A — pointer narrowing)

- **`minic.y` `irtyp`/`irtyp_ret`** now return `'w'` for non-far PTR
  and FUN.  Far pointers stay `'l'` (4 bytes: segment:offset).
- **All hard-coded `=l alloc/copy/add/mul/div` sites** in minic.y that
  produce or consume near-pointer addresses are now `=w`.
  Function-pointer locals went from `alloc8 8` to `alloc4 2`.
- **ptrdiff_t** for near-pointer subtraction is `INT` (16-bit), only
  `LNG` for far-pointer subtraction.
- **`ops.h`**: `alloc4`/`alloc8`/`alloc16` accept Kw results as well
  as Kl (`T(w,l,e,e, x,x,e,e)`).  On i8086 the alloc'd address is
  16-bit.
- **`parse.c` `usecheck`**: Kw tmp can be used where Km (=Kl) is
  expected, gated on `T.name == "i8086"` so amd64/arm64 typechecking
  is unchanged.
- **`i8086/emit.c`**: Kw constant operands are sign-truncated to 16
  bits before emission.  Without this, a folded Kw `sub 0, 1` becomes
  `mov ax, 4294967295` (NASM word-bound warning) instead of `mov ax,
  -1`.
- **`stddef.h`**: `size_t` and `ptrdiff_t` are now `unsigned int` /
  `int` (16-bit), not `long`.  Was forcing 32-bit-pair codegen for
  every `strlen()+1` expression.
- **`libstub.asm`**: pointer-arg stack offsets reverted to 2-byte
  slots (`_intdos` outregs `[bp+6]`, `_int86` outregs `[bp+8]`,
  `_strrchr` c `[bp+6]`, `_strcpy` src `[bp+6]`).  `_stdin`/`_stdout`/
  `_stderr` globals added (small non-zero sentinels) so `fprintf()`
  references resolve.

### Why the savings were 17% and not "halve"

The original estimate was that pointer narrowing would roughly halve
the binary.  Actual savings were limited by:

- **Stevie genuinely uses `long`** for file positions (`fseek`/`ftell`
  argument types) and BIOS data (`Realsecs`-style timer ticks).  We
  can't narrow these.  Search/fileio/undo all retain real 32-bit
  arithmetic.
- **2488 `xchg bx, ax`-style fixups** in the binary, ~1 byte each.
  These come from `i8086/emit.c`'s BX-address-mode rewrite — the i8086
  only allows BX/SI/DI as base registers in `[reg]` addressing, but
  rega doesn't hint addresses into BX, so a swap is emitted before
  every store/load through a register-resident pointer.  ~2.5KB of
  pure overhead.
- **9072 `mov` instructions** (every reg-mem and mem-reg op).
  Includes 69 `mov ax, ax` self-moves and 226 `mov bp, sp` /
  187 `mov sp, bp` prologue/epilogue pairs.

## What's next — in dependency order

### Priority 1 — get .COM under 64KB (blocking)

Without this, DOSBox truncates the binary at 0xFFFE and execution
crashes the moment it jumps into the upper half.

#### 1a — Cheaper paths first (incremental codegen wins)

- **Eliminate `mov ax, ax` self-moves** (69 occurrences, 138 bytes).
  Track them down to the emit site and gate on src!=dst.
- **Hint pointer values into BX/SI/DI** during register allocation so
  loads/stores don't need the xchg dance.  Each removed xchg pair is
  2 bytes; ~2KB potential.
- **Coalesce prologue/epilogue** when no locals are allocated (some
  functions have `push bp / mov bp, sp / pop bp / ret` with no body).

Estimated combined savings: 5-10KB.  **Probably won't get us under
64KB on its own** but might get within striking distance.

#### 1b — Path B (multi-segment .EXE) — bigger lift, no ceiling

Per the original analysis, NASM `-f bin` doesn't support segment-base
references.  Need:

1. Compile each TU with `qbe -t i8086 -m medium`.
2. Assemble each TU with `nasm -f obj` (NASM emits OMF).
3. Link with a real DOS linker.  Options:
   - **wlink (Open Watcom)** — best, build from source on macOS.
   - **Custom Python linker** against NASM's OMF output — ~500-800
     lines.  OMF reference at Open Watcom docs.
4. `tools/build-stevie.sh` learns a `--exe` mode.
5. crt0 needs an MZ-EXE-style entry that sets DS to DGROUP.

Path B unlocks much more than .COM (>64KB code, separate code/data
segments, multiple data segments).  But it's a 1-2 session lift on its
own and there are no existing tests for medium-model emission against
stevie.

### Priority 2 — actually run it end-to-end

Once Priority 1 lands:

```sh
dosbox build/stevie-orig/stevie.com   # or stevie.exe
```

Likely-failure stack:
1. crt0 — segment setup, stack init.  Mostly battle-tested.
2. `windinit` — first DOS interaction.  `_int86`/`_intdos` are real
   now, so this should mostly work.
3. malloc — real bump allocator now.  May exhaust if we ask for too
   much (heap top is `SP - 1024`).
4. Far pointer arithmetic — `0xF400800D` access for TI Pro detection;
   should fail strncmp and fall back to IBM PC.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/`:
- `feedback_minic_yacc_quirks.md` — miniyacc, varclr probe chains,
  uniform-* peeling, CRLF/0x1A, macOS sysroot trap.
- `reference_qbe_upstream.md` — `upstream` remote points at
  c9x.me/qbe.git.
- `project_minic_pointer_bloat.md` — Path A landed; details what was
  changed and why the savings stopped at 17%.
- `feedback_libstub_ptr_abi.md` — near-pointer args = 2 stack bytes
  after Path A; far-pointer args = 4 bytes.

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

# Bloat profile
echo "size: $(wc -c < build/stevie-orig/stevie.com) bytes"
echo "self-moves: $(grep -cE '^\s+mov ([a-z][a-z]), \1$' build/stevie-orig/stevie.full.asm)"
echo "xchg: $(grep -c '^\s*xchg' build/stevie-orig/stevie.full.asm)"
echo "32-bit adc: $(grep -c '^\s*adc ' build/stevie-orig/stevie.full.asm)"

# Verify -m medium emits far code
./qbe -t i8086 -m medium build/stevie-orig/alloc.ssa | grep -E "proc far|retf|call far" | head

# Run in DOSBox.  Will still truncate at 0xFFFE.
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

# Inspect the pipeline for one file
cat build/stevie-orig/<base>.pp.c    # cpp output (post tr/sed)
cat build/stevie-orig/<base>.ssa     # minic's IR
cat build/stevie-orig/<base>.asm     # qbe i8086 emit
cat build/stevie-orig/<base>.nasm.asm
```
