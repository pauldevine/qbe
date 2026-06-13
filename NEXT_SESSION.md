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

# Next session (§6k — continue Phase 6.  §6j [2026-06-12, this session] completed **step 4g: the ten portable DOS-hosted newlibc tests now run UNMODIFIED on the bare machine through bm_stdio — battery 12/12 → 22/22.**  The mechanism is a **test-host mode** in `tools/build-newlibc-baremetal.sh`: a source path that resolves into newlibc's `tests/` directory (`$NL/tests/*.c`) is recognized as an upstream test TU and gets the SAME `-Dmain=newlibc_test_main` rename the DOS-hosted gate (`build-newlibc-test.sh`) uses — with new `minic/dos/newlibc/bm_testhost.c` linked as `main()`: it does the §6d-ordered driver bring-up (`bm_interrupts_init` → `bm_timer_init` → `bm_tty_init` → `sti` → `bm_stdio_init`/vfs), calls `newlibc_test_main()`, then prints a `bm_testhost: test returned N` line and `__V9END__`.  Test-host mode auto-pulls the full bm_stdio TU set (bm_shim + bm_tty + drivers + printf/scanf wrappers + syscalls + vfs + fat + block).  The ten tests are snprintf_test, stdio_route_test, the six FAT/VFS tests (fat_bpb/chain/root/dir/file/vfs), terminal_meta_test, and ramfs_test; each one's bare-metal serial output is **line-identical to its existing DOS-hosted golden** between the testhost preamble and the result line — the ten new goldens (`minic/dos/tests/<name>.golden.txt`) were verified by `diff` against `newlibc_<name>.golden.txt` before locking, and all ten were **FIRST-RUN PASS with zero compiler changes** (sixth straight session on the §6d ISR ABI).  `bss_test` deliberately stays DOS-hosted only (display-only output ending in a `hlt` idle loop, no serial/stdio path; `memory_bm` already covers bare-metal RAM write-readback).  Harness: `tools/test-newlibc.sh` grew the ten entries (budgets follow output length per the §6f scroll lesson — most 45–90 s, but ramfs_test's 103 output lines need the **300-emulated-second** budget; the 180 s first try truncated mid-line, slowness not a hang).  Upstream `~/projects/newlibc` moved again since §6i — the FAT-write work plus mkdir/rmdir/rename merged through PR #19 (HEAD `16d54ac`) — with NO golden impact: `tools/test-dos.sh` (which rebuilds the moved tree) is still **289/289**, because the write support stays a runtime-installed dispatch table (`fat_write_ops`) that read-only mounts never touch and we never link.  Gates: test-newlibc **22/22**, test-dos **289/289**; NO toolchain change (new TU + build-script logic + harness only) so no emit audit / MP byte-compare triggered.  Next: port the upstream FAT-WRITE path (`vfs_mount_victor_fat_rw` + the `fat_write.c` dispatch install) — bm_sasi WRITE(6) is proven hardware under it, and the six bare-metal FAT tests could then run read-WRITE against the real `-scsi:0` disk; `run-dos-exe.sh` stdin redirect (unlocks `stdin_test`/`scanf_test`); scanf-over-cooked-tty when a consumer appears; newlibc-under-far-models stdio story.)

## §6j session notes (2026-06-12)

### Test-host mode (build-newlibc-baremetal.sh)
- A source resolving to `$NL/tests/*.c` flips `TESTHOST=1`; everything in
  `minic/dos/newlibc/*.c` (the hand-written bare-metal tests) keeps the
  old bm_crt0 `start()`-calls-`main()` arrangement with no rename.
- `bm_testhost.c` is the bare-metal seat of dos_shim's main(): it owns
  the interrupt-window bring-up order (PIC re-init BEFORE sti, §6d), runs
  `vfs_init()`, calls `newlibc_test_main()`, prints the result line
  through the very stack the test exercised, then `__V9END__`.
- Test-host mode implies the full bm_stdio TU set even without the
  program `#include`-ing bm_stdio.h (the rename means the test's own
  includes are just `<stdio.h>` etc.).
- The preamble (`bm_testhost: pic+timer / tty+sti / vfs`) prints through
  the POLLED serial console (bm_puts) before vfs is up; the test body and
  result line go through the newlibc stack.

### Golden equivalence (the load-bearing claim)
- Each bare-metal golden, stripped of the 3 preamble lines and the
  trailing result+`__V9END__`, is `diff`-identical to the DOS-hosted
  `newlibc_<name>.golden.txt`.  Same newlibc sources, same minic, only
  the bottom shim and the host differ — exactly the §6h parallel.
- Locked the goldens by copying the verified `build/nl-bm-golden/*.out`
  captures (full, including preamble + result line) to
  `minic/dos/tests/<name>.golden.txt`.

### Budgets / harness facts
- ramfs_test: 103 output lines ⇒ 300 s emulated budget (display scroll
  dominates 8088 time; the §6f lesson, now the longest battery entry).
- terminal_meta_test (36 lines) 90 s, the FAT tests (≤20 lines) 60 s,
  snprintf/stdio_route 45 s — all comfortable.
- No new harness env vars; the ten entries use empty keypost/serial/disk
  fields (`<name>:<secs>:::`).

### Open tracks (new + carried)
- Port upstream FAT WRITE: `vfs_mount_victor_fat_rw` + `fat_write.c`'s
  runtime dispatch install (`vfs_set_fat_write_ops`) — then the six
  bare-metal FAT tests can run read-WRITE against the real `-scsi:0`
  disk instead of RAM/label fixtures.
- run-dos-exe.sh stdin redirect (unlocks `stdin_test`, `scanf_test`).
- scanf-over-cooked-tty; serial TX ISR — both when a consumer appears.
- newlibc-under-far-models stdio story (when a far consumer appears).
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---


Older session headers (§6i and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
