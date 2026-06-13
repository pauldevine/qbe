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

# Next session (§6l — continue Phase 6.  §6k [2026-06-13, this session] completed **step 4h, DOS-hosted half: the unmodified upstream `fat_write_test.c` now PASSES through the full newlibc FAT WRITE layer — and to do it, brought up the first MEDIUM-model DOS-hosted newlibc port.**  The plan was "both, DOS-hosted first"; the first finding killed the easy half: `fat_write.c` (~18 KB code) on top of the FAT/VFS/stdio stack overflows the **small model's single-`_TEXT` 64 KB code ceiling** in EVERY host — three data points all over (bare-metal `fatwrite_bm` 81 KB, DOS upstream `fat_write_test` 77 KB, a stripped-to-the-bone hand-written DOS test still 68.6 KB) because the floor is `fat.c`(20K)+`fat_write.c`(18K)+`vfs.c`(17.5K) and `fat_write.c` builds ON the other two.  So there is NO small-model path; **medium is the only path** (far CODE → code splits across per-TU `<=64 KB` CS segments, escaping the ceiling; near DATA → one DGROUP, and minic's `call_target_name()` does NOT `far_stdlib`-mangle under `NEAR_DATA()` (= tiny/small/**medium**), so newlibc's own printf/str/mem stdio is called by real name — the far_stdlib concern is a far-DATA compact/large/huge issue only).  Two over-conservative guards wrongly lumped medium with far-data and were relaxed: `build-newlibc-test.sh` (small-only → small+medium, + a `--stack-size` knob) and `libstub_to_exe.py`'s `--no-stdio` guard (`near_code_model` → new `near_data_model`).  That got medium to LINK but **hang before any output**; markers in `dos_shim`'s `main()` localized it precisely — `console_dev_write` direct (MARK-A/B) worked, but `_write → vfs_write` (MARK-C) hung, and `vfs_write` dispatches through a **function pointer** (`fd_table[fd].target.dev->write`, set in a static device-ops initializer).  **Root cause: a medium far CODE-pointer static initializer.** `asm_to_omf.py` split a relocatable `.long _sym` into `dw off + dw seg sym` only for far-DATA models; in medium (near data) every `.long _sym` is a 4-byte far CODE pointer, but it fell through to `dd _sym` (offset only, **segment word = 0**), so the indirect far CALL through it jumped to segment 0 → wild jump → hang.  Fix: `split_sym_long = far_data or model == 'medium'` (scoped to medium so the compact/large/huge corpus path stays byte-identical).  With that, `console_device.write` carries `dw seg`, and the unmodified `fat_write_test.c` PASSES — `vfs_mount_fat_rw` + create/multi-cluster-write/read-back/append/truncate/unlink/mkdir/rmdir/rename over a RAM disk, README.TXT fixture intact throughout.  `dos_shim` grew the write side of its FILE layer en route: `fwrite()`, and a proper `fopen` mode map (`"w"/"a"/"+"` → `O_CREAT/O_TRUNC/O_APPEND/O_RDWR`; before, only `"r"` was ever used).  Gated as `newlibc medium (fat_write_test)` in `test-dos.sh` (a new per-test `--model=medium --stack-size=5120` path through `build_newlibc_test`; the ramdisk `media[]` crowds the near-data DGROUP so the stack shrinks).  Gates ALL GREEN: **test-dos 290/290** (289 → +1), all 11 small newlibc tests still golden-identical (the `dos_shim` edits are `"r"`-safe), **MP compact body 731,088 bytes byte-identical** (compact provably unaffected — `split_sym_long == far_data` there; MP uses no `--no-stdio`), and **stevie (medium) still builds** under the size gate (so the `asm_to_omf` medium change didn't regress the flagship medium consumer).  No `i8086/emit.c` change → no emit audit.  Phase B (the bare-metal half) remains: `minic/dos/newlibc/fatwrite_bm.c` is already written and the `fat_write.h` probe is in `build-newlibc-baremetal.sh`, but running it on the real `-scsi:0` disk needs the medium-model BARE-METAL port (the same §6k far-code-ptr fix applies; still TODO: `build-newlibc-baremetal.sh --model=medium`, far crt0 + raw-binary register-setup stub in medium, the §6d ISR ABI under medium far-`iret`).  Next: that medium bare-metal port; optionally `fat_write_unit_test.c` (FAT16+FAT12) as a second medium gate test; `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`).)

## §6k session notes (2026-06-13)

### The size wall (why small-model is dead for fat_write.c)
- Small model coalesces ALL code into ONE `_TEXT` (near calls), capped at
  64 KB.  `fat.c`(20K obj) + `fat_write.c`(18K) + `vfs.c`(17.5K) is the
  irreducible floor — `fat_write.c` builds on the other two.  Measured
  `_TEXT`: bare-metal 81 KB, DOS upstream `fat_write_test` 77 KB, a minimal
  hand-written DOS test 68.6 KB — all over 64 KB.  No trim fits.
- The hand-written minimal probe was deleted once the upstream test ran in
  medium; it had served its measurement purpose.

### Medium = far code + near data (the unlock)
- `NEAR_CODE()` is tiny/small ONLY (minic.y:52 — "excludes MCompact");
  medium/compact/large/huge are all far-code (`CODEPTR_T()` = 'l', 4-byte).
- `NEAR_DATA()` is tiny/small/medium → `call_target_name()` returns the
  real name (no `far_*` mangling) → newlibc stdio links by real name under
  `--no-stdio`.  The "far models need far_stdlib stdio" comments were a
  far-DATA truth mis-applied to medium.

### The bug: medium far CODE-pointer static initializers (asm_to_omf.py)
- A relocatable `.long _sym` is ALWAYS a 4-byte far pointer (seg:off).
  asm_to_omf only split it for far-DATA models; medium fell through to
  `dd _sym` → 32-bit OFFSET fixup, segment word 0.  An indirect far CALL
  through such a pointer (a function-pointer field in a static device-ops
  table — `console_device.write = console_dev_write`) jumps to seg 0.
- Fix: `split_sym_long = far_data or model == 'medium'` → `dw _sym + dw seg
  _sym`.  Scoped to medium so compact/large/huge stay byte-identical (their
  code pointers ride the existing far_data split when --far-static-data is
  used; MP/stevie corpus unchanged — MP body still 731088).
- Localized with `console_dev_write` markers in dos_shim main(): direct
  device write worked, the function-pointer dispatch (`vfs_write`) hung.

### dos_shim FILE-layer write side (new)
- `fwrite()` (mirror of `fread` over `_write`); `fopen` now maps
  `"w"/"a"/"r+"/"w+"/"a+"` to O_CREAT/O_TRUNC/O_APPEND/O_RDWR (was "r"-only).
  `"r"` path byte-identical → small gate unaffected.  `bm_shim.c` should
  get the same `fwrite`/`fopen` for Phase B parity (NOT done yet).

### Gating / harness
- `build-newlibc-test.sh`: `--model=small|medium`, `--stack-size=N`, crt0
  `-DNEAR_CODE` only for small, `fat_write.h` probe (+ mkdir/rmdir/rename).
- `test-dos.sh`: `build_newlibc_test` forwards extra args; new `newlibc
  medium (fat_write_test)` staged case (medium, 5120 stack).  290/290.
- Medium fat_write_test image ~152 KB, fits DOSBox; golden is the test's
  quiet success (banner + "PASS" — any FAIL prints and breaks the diff).

### Open tracks (new + carried)
- **Phase B**: medium-model BARE-METAL port → run `fatwrite_bm.c` on the
  real `-scsi:0` disk.  `build-newlibc-baremetal.sh` is still small-only;
  needs `--model=medium`, far crt0 + raw-binary register-setup stub in
  medium, and the §6d ISR ABI under medium far-`iret`.  The §6k far-code-
  ptr fix already applies (bare-metal medium hits the same device-ops bug).
- `fat_write_unit_test.c` (FAT16+FAT12) as a second medium gate test.
- `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` >=0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---


Older session headers (§6j and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
