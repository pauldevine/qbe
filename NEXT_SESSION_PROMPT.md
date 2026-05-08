# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  Premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**

Build target: `stevie-orig/`.  Pipeline today: `tools/build-stevie.sh
--keep-going` (tiny model, .COM output via `nasm -f bin`).

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

**24/24 sources still compile and link** to a tiny-model `.COM` of
80802 bytes (commit e5c013d).  Path A (near-pointer narrowing) +
self-move elision landed this session.  Binary still over the 64KB
.COM ceiling, but that ceiling stops mattering once we hit the .EXE
pipeline below.

```
=== Build summary ===
  PASS: 24/24
=== Linking ===
  OK: build/stevie-orig/stevie.com (80802 bytes)
```

### What qbe already supports (foundation already in place)

- `enum MemModel` covers all 6 models — `all.h:70`.
- `qbe -m {tiny,small,medium,compact,large,huge}` flag — `main.c:49`.
- ABI picks RET vs RETF, near-call vs far-call return-slot size based
  on `uses_far_code()` — `i8086/abi.c:33`.
- `selpar()` already lays parameters out at `[bp+4]` for near calls
  vs `[bp+6]` for far calls.
- Emit prologue knows about all six models and emits the appropriate
  segment directives — `i8086/emit.c:177`.
- Verified working: `./qbe -t i8086 -m medium <ssa>` emits `proc far
  / retf / call far` correctly.

### What's still missing for the full matrix

1. **No `uses_far_data()`** — qbe's data-pointer width is whatever
   the IL says.  compact/large/huge need data pointers to be `l`
   (far) by default, but minic emits `w` after Path A.

2. **minic has no `-m` flag** — Path A hard-codes near pointers as
   `w` regardless of model.  Needs to be model-driven:

   | model    | code ptr (FUN) | data ptr (PTR) |
   |----------|----------------|----------------|
   | tiny     | w              | w              |
   | small    | w              | w              |
   | medium   | **l**          | w              |
   | compact  | w              | **l**          |
   | large    | **l**          | **l**          |
   | huge     | **l**          | **l** (huge)   |

3. **No .EXE pipeline** — `tools/build-stevie.sh` only knows
   `nasm -f bin` → flat .COM.  Medium+ requires `nasm -f obj`
   (OMF) → DOS linker → MZ .EXE.

4. **No DOS linker on macOS** — wlink (Open Watcom) builds from
   source; alternative is a custom OMF linker (~500-800 LOC).

5. **crt0 has no MZ variant** — current `crt0.asm` is .COM-only.
   .EXE entry needs to set DS to DGROUP and handle a separate stack
   segment.

6. **libstub stack offsets depend on model** — for compact/large/
   huge, pointer args are 4-byte (segment:offset) again, so
   `_intdos`/`_strcpy`/`_strrchr`/`_int86` need model-aware variants
   or per-model rebuilds.

## What's next — in dependency order

### Step 1 — minic learns memory-model awareness

**Goal:** `minic -m <model>` produces IR with the right pointer
widths per the table above.  small/tiny still works exactly like
today (regression check).

Concrete work:
- Add `-m <model>` arg parsing to `minic/minic.y` `main()`.
  Default = small (matches Path A behaviour).
- Replace the hard-coded `KIND(ctyp) == PTR && !ISFAR(ctyp)` →
  `'w'` rule in `irtyp()` / `irtyp_ret()` with a model lookup:
  ```
  near_data = (model in {tiny, small, medium});
  near_code = (model in {tiny, small, compact});
  if (KIND == FUN)        return near_code ? 'w' : 'l';
  if (KIND == PTR && ISFAR(t))   return 'l';
  if (KIND == PTR)        return near_data ? 'w' : 'l';
  ```
- Audit the 28+ hard-coded `=w alloc/copy/add/mul/div` sites we
  changed in commit 5125e70 — those are *near pointer* operations.
  In compact/large/huge, the alloc result is still a near pointer
  (you can't far-alloc a stack slot), but the data-pointer arithmetic
  on top changes.  Sites to revisit:
  - `=w copy $name` (function/global address) — code addresses are
    `'w'` if near_code, else `'l'`.
  - `=w add %_clit` (struct/array member offset) — uses the IL type
    of the address being added to (the clit is always near, so
    stays `'w'`).
  - `=w add %ptr, off` (member access on a runtime pointer) —
    matches `irtyp(ptr_ctyp)`, which is now model-aware automatically.
- Update `minic_cpp_v2` (the cpp wrapper) to pass `-m` through.
- Update `tools/build-stevie.sh` to plumb `--model=<m>` through to
  both minic and qbe.

Validation: rebuild stevie with `--model=small`, expect identical
behaviour to today.  Then `--model=medium`, expect bigger but
working asm with `proc far` / `call far` everywhere.

### Step 2 — stand up the .EXE pipeline (medium model)

**Goal:** `tools/build-stevie.sh --model=medium --exe` produces a
runnable `stevie.exe`.

Concrete work:
- Switch per-TU assembly from `nasm -f bin` to `nasm -f obj` (OMF).
  Verify NASM emits sane segment directives for medium model.
- Pick a DOS linker:
  - **Easiest:** install Open Watcom (homebrew has `open-watcom-v2`
    on macOS via `brew tap open-watcom/open-watcom-v2-binaries` —
    confirm), use `wlink`.
  - **Fallback:** write a minimal OMF→MZ linker in Python.  Spec
    in Open Watcom docs (`/openwatcom/docs/pdf/lr.pdf`) — only
    need to handle the records NASM emits (THEADR, SEGDEF,
    GRPDEF, EXTDEF, PUBDEF, LEDATA, FIXUPP, MODEND).
- Write an `crt0_exe.asm` variant that:
  - Sets DS to DGROUP at entry.
  - Initializes SS:SP to the stack segment (separate from DGROUP).
  - Calls `_main` via far call.
  - Exits with INT 21h AH=4Ch using AL from `_main`.
- Update `build-stevie.sh` to drive the new pipeline behind
  `--exe`/`--model=medium`.
- Update `_malloc` heap layout — without a fixed image-end, the
  heap starts after BSS (linker-defined `_end`) and grows up to
  the bottom of the stack segment.

Validation: `dosbox build/stevie-orig/stevie.exe` reaches `_main`
without truncation, runs `windinit`, calls `_int86` cleanly.

### Step 3 — exercise medium model under DOSBox

**Goal:** stevie actually loads a file and renders the screen.

Likely-failure stack (already partially mapped from prior runs):
1. crt0 — DS/SS setup, stack init.
2. `windinit` — first INT 10h / INT 21h calls.  `_int86`/`_intdos`
   are now real.
3. `malloc` — real bump allocator; verify it works in .EXE layout.
4. Far pointer arithmetic — `0xF400800D` TI Pro detection should
   fail strncmp and fall back to IBM PC.
5. Screen drawing — VGA mode 3 text-mode I/O via INT 10h.

### Step 4 — fill in compact / large / huge

Defer until after medium is running.  Each one needs:
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

# Build stevie (current — tiny / .COM)
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

# Verify qbe -m medium emits far code (already works)
./qbe -t i8086 -m medium build/stevie-orig/alloc.ssa | grep -E "proc far|retf|call far" | head

# Cherry-pick from upstream qbe
git fetch upstream
git log upstream/master --oneline -- spill.c rega.c isel.c

# Inspect the pipeline for one file
cat build/stevie-orig/<base>.pp.c    # cpp output
cat build/stevie-orig/<base>.ssa     # minic's IR
cat build/stevie-orig/<base>.asm     # qbe i8086 emit
cat build/stevie-orig/<base>.nasm.asm
```
