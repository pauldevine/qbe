# Resume prompt — Stevie/QBE: Bug A + Bug B fixed, runtime smoke test next

## Status

Both blocking codegen bugs from the previous session are fixed.  Stevie
should now load HELLO.txt, render lines, and accept navigation keys.
Next session: verify in DOSBox and triage whatever else surfaces.

## What landed this session (2 commits on master)

```
7225d19 qbe rega+i8086 emit: stop caller-save reg from leaking across calls
e897434 minic: emit 'l' (32-bit on i8086) for LNG types
```

### Bug A fix — `e897434` (minic.y)

`irtyp` and `irtyp_ret` returned `'w'` for `long` because they checked
`SIZE(ctyp) == 8`, but the i8086 build sets `SIZE(LNG) = 4`.  All long
storage and the printf `%ld` vararg slot were 2 bytes, with the high
word coming from the next arg.  Added a `KIND(ctyp) == LNG → 'l'`
branch before the `SIZE == 8` check in both functions.

### Bug B fix — `7225d19` (rega.c + i8086/emit.c)

Two cooperating problems:

1. **rega.c**: `ralloctry`'s "any free reg" fallback was setting
   `sethint` + `tmp[t].visit` even when the picked reg was caller-save.
   That choice then propagated to subsequent blocks via `visit`.  In
   stevie, t39 (FILE*) was forced into RAX by pressure in `@l17`, then
   `@l15` (which had BX/SI/DI free) inherited that, and `filemess`
   wiped AX → getc(NULL) → -1.  Fix: skip the propagation when the
   fallback path is taken.

2. **i8086/emit.c**: `Ocopy Kl` from `RCon` to `RSlot` was going through
   AX:DX as scratch.  After (1), rega emitted `R1 =w copy R4; S231 =l
   copy 0` in the @l15→@l17 parallel-move block — the first move loaded
   t39 into AX, and the second instantly killed it.  Fix: use the
   8086 `mov word [mem], imm16` encoding directly when both ends are
   slots/constants.  No register touched.

## What's still TBD (next session)

1. **Smoke-test in DOSBox.**
   ```sh
   rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
   cat > build/stevie-orig/HELLO.TXT <<EOF
   Hello from a non-empty file!
   This is line 2.
   EOF
   dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "stevie.exe HELLO.txt"
   ```
   Expect the editor to show 2 lines of content, a status bar of the
   form `"HELLO.txt" 2 lines, 45 characters`, and j/k navigation +
   `:q!` to work.

2. **Audit other Ocopy Kl emit paths for the same scratch issue.**  The
   fix only addressed Con→Slot.  Other shapes (Slot→Slot, Tmp→Slot,
   Slot→Tmp) also go through AX:DX.  In a parallel-move block these
   could create the same hazard if a *prior* parallel move just landed
   t in AX.  Audit:
   - `Ocopy Kl Slot → Slot` (the most common 32-bit phi copy)
   - `Ocopy Kl Tmp → Slot` (spill of a Kl temp)
   - Any other case in `i8086/emit.c::Ocopy` that touches AX or DX.

   A general fix may be to model the AX/DX clobber in isel so rega
   never schedules conflicting copies — same pattern as the prior
   `imul DX` workaround.

3. **Other implicit-clobber audits (from prior playbook):**
   - `cbw` (AL→AX): writes AH.
   - `cwd` (AX→DX:AX): writes DX.
   - Shifts via CL on pre-286: writes CL.
   - DOS `int 21h` / BIOS `int 10h` wrappers in libstub: verify cdecl-
     conformant preservation.

4. **Write-back support (`:w`).**  Still stubbed in libstub — `_fopen
   "w"` (INT 21h AH=3C) and `_fputc`/`_fputs`/`_fwrite` are empty.
   Add real implementations in `tools/libstub_to_exe.py::FILEIO_EXE`.

## Build & test

```sh
make qbe
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going --model=medium
cat > build/stevie-orig/HELLO.TXT << 'EOF'
Hello from a non-empty file!
This is line 2.
EOF
dosbox -c "mount c $PWD/build/stevie-orig" -c "c:" -c "cls" -c "stevie.exe HELLO.txt"
```

Pre-existing-failure check: `make check` reports 3/62 failures, all
unrelated to these fixes (arm64 far-pointer support and a link error).

## Memory entries updated

- `feedback_minic_long_vararg_truncated.md` — marked fixed, root cause
  documented.
- `feedback_minic_readfile_register_clobber.md` — marked fixed, both
  sub-bugs documented.
- `MEMORY.md` index updated.

## Heisenbug lessons (carried forward)

- **rega's `visit` field is sticky across blocks.**  Once set, it
  bypasses `hint.m` (the avoid mask).  Any place rega may pick a reg
  it would normally avoid is a place that should *not* update `visit`.
- **Parallel-move blocks make implicit-clobber bugs vicious.**  rega
  emits a sequence of copies in one block; if any emit handler uses
  AX/DX as scratch, a copy upstream of it in the same block can be
  wiped without warning.  Audit handlers that touch AX/DX whenever a
  parallel-move block "loses" a value.
- **State the failure in registers, not C-source terms.** "Stevie loads
  zero lines" was the symptom; "rega put FILE* in AX in @l15, filemess
  clobbered AX, getc(NULL) returned -1" was the diagnosis.
