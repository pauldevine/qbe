# Next session (§6n — continue Phase 6.  §6m [2026-06-13, this session] completed **a SECOND medium-model FAT-write gate, both halves, with ZERO toolchain changes: the UNMODIFIED upstream `fat_write_unit_test.c` now PASSES DOS-hosted (`test-dos` 291/291) AND bare-metal through bm_stdio test-host mode (battery 24/24).**  Where §6k/§6l's `fat_write_test` drove the FAT WRITE layer through `vfs_mount_fat_rw` over a RAM/SASI disk, this unit test exercises `fat_write.c`'s primitives DIRECTLY on hand-built RAM volumes — FAT16 entry write/read + both-FAT mirroring + cluster-chain alloc/free + create/write/truncate/unlink/mkdir/rename + ENOSPC, plus FAT12 entries straddling a FAT sector boundary in both parities — so there is no SASI dependency on either host.  Both halves were FIRST-RUN PASS on the same medium support landed in §6k (the `asm_to_omf.py` `split_sym_long = far_data or model == 'medium'` far CODE-pointer static-init fix, minic's `NEAR_DATA()` covering medium, the `libstub_to_exe.py` `near_data_model` `--no-stdio` guard); **no compiler/toolchain source changed** (git diff = docs + three harness scripts + two goldens only) → no emit audit, no MP byte-compare.  The only changes were build/harness plumbing: `tools/build-newlibc-baremetal.sh` gained `--stack-size=N` (mirroring the DOS build — the test's hand-built RAM-volume `media[]` arrays on top of the full bm_stdio driver set push data+bss to ~60.7 KB, so the default 8 KB stack overflows the 64 KB DGROUP; it runs at 4096), and `tools/test-newlibc.sh` grew an optional **seventh** `:<stack>` entry field — the entry parser was rewritten from nested `${x%%:*}`/`${x#*:}` peeling to clean `IFS=: read` field-splitting (no field contains a colon, so the `::` empty-middle gaps are preserved exactly), confirmed equivalent for all 24 entries via a `--show`-style field dump.  DOS gate `newlibc medium (fat_write_unit_test)` (`--model=medium --stack-size=5120`); bare-metal entry `fat_write_unit_test:60::::medium:4096` (RAM-only, no `-scsi:0`).  Goldens: `minic/dos/tests/newlibc_fat_write_unit_test.golden.txt` (DOS) and `minic/dos/tests/fat_write_unit_test.golden.txt` (bare-metal test-host).  Verified this session: DOS-hosted output byte-identical to golden via `run-dos-exe.sh`; bare-metal `test-newlibc.sh fat_write_unit_test` → **[ok]** under MAME.  Phase 6 step 4h now has TWO medium FAT-write gates on both hosts.  Next: `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`); the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; or pick from the carried open tracks below.)

## §6m session notes (2026-06-13)

### Pure plumbing — §6k's medium support already covered it
- `fat_write_unit_test` `#include`s `fat_write.h`, so `build-newlibc-test.sh`'s
  §6k `fat_write.h` probe already pulls `vfs/fat_write.c` + the FAT/VFS/block
  stack — no build-helper change needed.  DOS-hosted EXE: 140,288 bytes
  (body 136,688), data+bss 52,176, links and runs at `--stack-size=5120`.
- Bare-metal: same test-host mode as §6j (`-Dmain=newlibc_test_main` +
  `bm_testhost.c`), RAM volumes only so NO `-scsi:0` disk field.  Its
  `media[]` arrays + the full bm_stdio driver set push data+bss high enough
  that the default 8 KB stack overflows the 64 KB DGROUP → runs at 4096.

### The harness parser rewrite (the one real risk this session)
- Old `test-newlibc.sh` peeled fields with `${rest%%:*}`/`${rest#*:}` and
  detected "no model field" with `[ "$model" = "$disk" ]`.  Adding a seventh
  field made that brittle, so it was rewritten to `IFS=: read -r name secs
  keypost serial_bytes disk model stack <<EOF`.  Safe because NO field value
  contains a colon, so splitting is exact AND empty middle fields (the `::`
  keypost/serial gaps) are preserved.  Verified by dumping the parsed fields
  for all 24 entries: every 5-field entry → model=small/stack=empty, the two
  medium entries → their 6th/7th fields, `::` gaps intact.

### Open tracks (carried)
- `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`).
- newlibc-under-far-DATA-models (compact/large) stdio story — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- `fat_write` over the REAL `-scsi:0` disk read-WRITE for the six bare-metal
  FAT tests (bm_sasi WRITE(6) + `vfs_mount_victor_fat_rw` proven; the unit
  test deliberately stays RAM-only to isolate the `fat_write.c` primitives).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

# Next session (§6m — continue Phase 6.  §6l [2026-06-13, this session] completed **step 4h, Phase B (bare-metal half): the bare-metal `fatwrite_bm.c` now PASSES on the real `-scsi:0` SASI disk through the full newlibc FAT WRITE layer — the toolchain's first MEDIUM-model BARE-METAL program.**  This is the §6k twin: §6k proved fat_write DOS-hosted (and found that `fat_write.c` over the FAT/VFS/stdio stack overflows the small model's single-`_TEXT` 64 KB code ceiling in EVERY host — bare-metal measured **88 KB** code here, confirming the §6k 81 KB estimate), so medium (far CODE → per-TU `<=64 KB` CS segments; near DATA → one DGROUP, no `far_stdlib` mangle) was the only path on the bare machine too.  **It was a FIRST-RUN PASS with ZERO compiler/toolchain changes** — everything medium needs was already in place from §6k: `asm_to_omf.py`'s `split_sym_long = far_data or model == 'medium'` (the far CODE-pointer static-init fix for device-ops tables like `console_device.write`), minic's `NEAR_DATA()` covering medium (newlibc stdio linked by real name under `--no-stdio`), and `libstub_to_exe.py`'s `near_data_model` `--no-stdio` guard.  omf_link's raw-binary path is natively medium (multi-CS) and resolved the entry symbol's CS:IP for the register-setup stub with no change; the §6d ISR ABI works unchanged under medium (the timer ISR's far function pointer carries its real CS via the in-code far-pointer materialization — the `bm_install_isr` `seg==0` fallback is only for small/near-code; `iret` always pops CS, so it is model-independent); `bm_crt0.c` is plain C so minic emits the far `call main` automatically.  **The only changes this session were build-script + harness plumbing**: `tools/build-newlibc-baremetal.sh` gained `--model=small|medium` (mirroring the DOS gate; medium = far code + near data, validated, default small), and `tools/test-newlibc.sh` grew an optional sixth `:<model>` entry field (default small via a `[ "$model" = "$disk" ]` no-field test) threaded into the build call — `fatwrite_bm:240:::hd:medium`.  The new `fatwrite_bm` entry uses the §6i `V9K_HARD_DISK` scratch-copy path (the base `victor_30mb.img` never mutates) and an **240-emulated-second budget**: phase 8's multi-cluster write over REAL SASI on the 5 MHz 8088 dominates (90 s truncated mid-`create+write` — the §6f slowness lesson, NOT a hang; 240 s ran clean through all 11 phases + CONFIG.SYS-intact before/after to PASS).  Golden `minic/dos/tests/fatwrite_bm.golden.txt` captured from a clean stdout-only run; verified end-to-end through `test-newlibc.sh fatwrite_bm` → **[ok]**, and the parser change was confirmed non-regressing for all five existing entry shapes in isolation (every 5-field entry → model=small, unchanged disk/keypost/serial).  Gates: bare-metal **fatwrite_bm PASS** (battery would be **23/23**); **no toolchain change** so no emit audit, and **no MP byte-compare** triggered (the §6k far-code-ptr fix MP rides was already committed and proven byte-identical last session).  Phase 6 step 4h is now COMPLETE on both halves (DOS-hosted §6k + bare-metal §6l).  Next: optionally `fat_write_unit_test.c` (FAT16+FAT12) as a second medium gate test (DOS-hosted and/or bare-metal); `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`); the newlibc-under-far-DATA-models stdio story when a compact/large consumer appears; or pick from the carried open tracks.)

## §6l session notes (2026-06-13)

### The port was pure plumbing — medium was already ready
- `fatwrite_bm` built clean on the FIRST try under `--model=medium`:
  87,887 bytes of code (over the 64 KB single-`_TEXT` ceiling → confirms
  medium multi-CS is mandatory on bare metal too), 148,160-byte image,
  loads at 0x3000, entry `038F:0000`.  No new compiler/toolchain change —
  the §6k far CODE-pointer static-init fix (`split_sym_long = far_data or
  model == 'medium'` in `asm_to_omf.py`) already covers the device-ops
  tables, and the in-code far-function-pointer path (used to install the
  timer ISR vector) was always model-correct.
- omf_link `--raw-binary` is natively the medium-model linker (multi-CS):
  it resolved the entry symbol's CS:IP for the register-setup stub with
  zero change.  `bm_crt0.c`'s `start()` → `main()` becomes a far call
  automatically under `minic -m medium`; no `-DNEAR_CODE`.
- §6d ISR ABI under medium: `bm_install_isr(timer_isr)` takes a far
  function pointer carrying the ISR's real CS (the `seg == 0 →
  qbe_get_cs()` fallback in bm_interrupts.c is the small/near-code case
  only).  `iret` pops CS unconditionally, so the ISR epilogue is
  model-independent.

### Slowness, not a hang (the §6f lesson again)
- 90 s budget: output stopped mid-`phase 8: create+write 2000 bytes` —
  the multi-cluster SASI WRITE(6) sequence (allocate clusters, write data
  sectors, update FAT, update dir) over a real 5 MHz 8088 is far heavier
  than `sasi_bm`'s single WRITE(6).  240 s ran clean to PASS.  Budget set
  to 240 with margin (config-after + PASS lines printed well inside it).

### Harness model field
- `test-newlibc.sh` entries are now
  `<name>:<secs>:<keypost>:<serial>:<disk>:<model>`; the 6th field is
  optional (absent → small, detected by `${rest#*:}` == disk).  Verified
  all existing 5-field entries still parse to model=small with correct
  disk/keypost/serial before trusting it.

### Open tracks (new + carried)
- `fat_write_unit_test.c` (FAT16+FAT12) as a second medium gate test
  (DOS-hosted in test-dos.sh and/or bare-metal in test-newlibc.sh).
- `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`).
- newlibc-under-far-DATA-models (compact/large) stdio story — needs
  `far_stdlib`-aware routing; defer until a far-DATA consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6k and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
