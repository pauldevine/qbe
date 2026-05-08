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

**Step 1 (minic memory-model awareness) is done this session.**

- `minic -m {tiny,small,medium,compact,large,huge}` parses argv,
  defaults to `small`.
- `irtyp` / `irtyp_ret` are model-aware: code pointers honour
  `NEAR_CODE`, data pointers honour `NEAR_DATA`, far stays `'l'`.
- Function-address sites that were hard-coded to `=w` (the
  `=w copy $name` for function-pointer fetch and the two function-
  pointer alloc sites) are now driven by `CODEPTR_T()` / `CODEPTR_SZ()`.
- `minic_cpp_v2` and `tools/build-stevie.sh` accept `--model=<m>`
  and forward it to both minic and qbe.

**24/24 sources compile + assemble for both small and medium model**:

```
# small (default) — bit-identical to pre-session baseline
tools/build-stevie.sh --keep-going
=> build/stevie-orig/stevie.com (80802 bytes)

# medium — minic+qbe both happy; nasm -f bin can't link `call far`
tools/build-stevie.sh --keep-going --model=medium
=> 24/24 PASS at the per-TU stage; link fails with
   "binary output format does not support segment base references"
   on every `call far _malloc` / `call far _emsg` site.
```

The medium-model link failure is the expected, intentional next
problem: medium requires `nasm -f obj` (OMF) plus a DOS linker, which
is exactly Step 2 below.

The data-pointer hard-coded `=w` sites from commit 5125e70 (alloc,
`%_clit*`, `%_zinit*`, struct-member adds) are correct as-is for
medium — they describe near-data arithmetic and medium still has
near data.  They will need another pass when compact/large/huge land
(Step 4): in those models alloc still returns a 16-bit offset but
data-pointer arithmetic on top should be `'l'` with an `extuw` lift.

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
   (far) by default; minic now emits `'l'` for those models, but
   alloca'd stack offsets are still 16-bit, so an `extuw`/segment-
   construct lift is missing.  Not on the medium critical path.

2. **No .EXE pipeline** — `tools/build-stevie.sh` only knows
   `nasm -f bin` → flat .COM.  Medium+ requires `nasm -f obj`
   (OMF) → DOS linker → MZ .EXE.  *This is the immediate blocker.*

3. **No DOS linker on macOS** — wlink (Open Watcom) builds from
   source; alternative is a custom OMF linker (~500-800 LOC).

4. **crt0 has no MZ variant** — current `crt0.asm` is .COM-only.
   .EXE entry needs to set DS to DGROUP and handle a separate stack
   segment.

5. **qbe's i8086 emit still mixes GNU-as / MASM directives** — the
   build script's `sed` pass papers over it for `nasm -f bin`, but
   `nasm -f obj` will need a cleaner upstream emit (or a more
   careful rewrite pass).

6. **libstub stack offsets depend on model** — for compact/large/
   huge, pointer args are 4-byte (segment:offset) again, so
   `_intdos`/`_strcpy`/`_strrchr`/`_int86` need model-aware variants
   or per-model rebuilds.

7. **The minic `=w` data-pointer table** — for compact/large/huge,
   alloc still returns a 16-bit offset; data-pointer arithmetic on
   top must be `'l'` with an `extuw` or DS-based segment-pair lift.
   Mapped out but deferred to Step 4.

## What's next — in dependency order

### Step 1 — minic learns memory-model awareness  ✅ DONE

(Landed this session.  See "Where we are right now" above.)

### Step 2 — stand up the .EXE pipeline (medium model)  ⬅ NEXT

**Goal:** `tools/build-stevie.sh --model=medium --exe` produces a
runnable `stevie.exe`.

Right now `--model=medium` already gets us 24/24 PASS at minic+qbe.
The build-stevie.sh "link" step then concatenates everything into
`stevie.full.asm` and runs `nasm -f bin` with `ORG 0x100`, which
fails on every `call far _malloc` etc.  That is the wall to break.

Suggested order of attack:

**(2a) Single-TU OMF round-trip.**  Pick the simplest source
(`alloc.c` or `version.c`), generate `.nasm.asm` exactly like today,
prepend an OMF-friendly preamble (no `BITS 16 / ORG 0x100`; instead
proper `segment _TEXT class=CODE` / `segment _DATA class=DATA` /
`group DGROUP _DATA _BSS`), and assemble with `nasm -f obj`.
Iterate on what the per-TU rewrite needs to emit until NASM is happy.
*Don't* try to link yet — just confirm clean OMF object files.

   The qbe i8086 emit currently produces things like
   `name proc far`, `call far _malloc`, `; XXX 32-bit op stub`, plus
   GNU-as `.text` / `.balign` mixed in.  The `sed` pass strips the
   GNU-as bits.  For OMF we additionally need:
   - `name proc far` → `name:` plus a separate `..start` / `global`
     declaration in the right segment.
   - `call far _malloc` becomes a far reference to an external symbol
     in another module's `_TEXT` segment (NASM does this for free in
     `-f obj` if the symbol is declared with `extern` *and* the call
     site uses `call far func` — the OMF FIXUPP record will carry it).
   - String literals `_glo1: db ...` need to live in `_DATA`, not
     interleaved with `_TEXT`.  Currently they're inline.  Either
     re-section per literal, or buffer them to emit at end of TU.

**(2b) Pick a DOS linker.**

   - **Easiest path:** install Open Watcom.  On macOS, the binary
     bundle from `https://github.com/open-watcom/open-watcom-v2/releases`
     gives you a working `wlink`.  (Homebrew tap `open-watcom/v2`
     existed at some point; verify before relying on it.)  Then:
     ```
     wlink system dos format dos exe \
           file alloc.obj file version.obj ... \
           file crt0_exe.obj file libstub.obj file doslib.obj \
           name stevie.exe
     ```
   - **Fallback (more fun, more work):** write an OMF→MZ linker in
     Python.  Need to handle THEADR, SEGDEF, GRPDEF, EXTDEF, PUBDEF,
     LEDATA, LIDATA, FIXUPP, MODEND, plus segment ordering and
     fixup resolution into MZ relocation table.  Spec lives in the
     Open Watcom Linker Reference (`/openwatcom/docs/pdf/lr.pdf`)
     and the OMF reference (search "Tomb of the Unknown OMF").
     Estimate ~500-800 LOC of Python for medium-only support.

**(2c) Write `crt0_exe.asm`.**  Diff from `crt0.asm` (.COM):
   - On entry, DS=ES=PSP, SS:SP set up by DOS to the stack segment
     declared in the MZ header.  Set DS to `DGROUP` (`mov ax, dgroup
     / mov ds, ax`).
   - Call `_main` with a *far* call (medium = far code).
   - On return, `mov ah, 4Ch / mov al, return_code / int 21h`.
   - Don't `ORG 0x100` — let the linker place us.

**(2d) Update `_malloc`.**  Today `_malloc` bumps from
`_heap_end_of_image` (a label injected at link concat).  In .EXE
the linker provides `_end` (or whatever symbol marks BSS end);
`_malloc` should bump from there up to bottom of stack segment.

**(2e) Drive it from `build-stevie.sh`.**  Add `--exe` (or imply
from `--model in {medium,large,...}`).  When set, run `nasm -f obj`
per TU, hand objs to `wlink`, write `stevie.exe`.

**Validation.**  `dosbox-x build/stevie-orig/stevie.exe` reaches
`_main` without truncation, runs `windinit`, hits `_int86` cleanly.
The likely failure stack is in Step 3 below.

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

# Build stevie (default = small, .COM)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going

# Build stevie medium model (24/24 compiles + assembles; link fails
# until step 2 is done)
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium

# Drive minic alone with a model selector
./minic/minic -m medium < build/stevie-orig/search.pp.c | head

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
