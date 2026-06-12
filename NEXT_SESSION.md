# Next session (§6d — continue Phase 6.  §6c [2026-06-11, this session] completed **step 3: the toolchain's first BARE-METAL program runs on the Victor 9000.**  `omf_link.py --raw-binary --load-addr 0x3000` emits a flat binary (selectors resolved at link time against the load paragraph, no MZ header, a 32-byte synthesized register-setup stub at the image head — the MAME Lua loader enters at 0:0x3000 with CS=DS=SS=0 — and BSS rides as zeros, no clear loop).  A **minic-built crt0** (`bm_crt0.c` `start()` → board init → `main()` → hlt loop) plus a minic-dialect **polled NEC 7201 serial console** (`bm_console.c`, newlibc's validated VIA2/8253-counter-0/WR-register sequence, pure volatile-far MMIO — the original's inline asm was all ia16-gcc workarounds) carry `hello_bm.c` to a PASS over serial under MAME: `tools/build-newlibc-baremetal.sh` + `tools/run-victor-baremetal.sh` (Lua autoboot loader, null_modem bitbanger capture).  Gated: `test_omf_link.sh` test 3 (deterministic raw-image structure asserts) + a `tools/test-victor.sh` golden-diff entry (victor pipeline 3/3).  DOSBox gate stays **287/287**; MP compact **byte-identical** (MZ-path refactor also proven by relink `cmp`).  Next: **step 4** — drivers/ISRs: ISR definition strategy, extended-asm output constraints + Intel template translation; port timer/display; more newlibc tests bare-metal; `tools/test-newlibc.sh` once a battery exists.)

## §6c session notes (2026-06-11)

### omf_link.py raw-binary mode (the step-3 enabler)
- `--raw-binary --load-addr 0x3000` (default 0x3000, paragraph-aligned): all
  loc==2/loc==3 selector fixups get `frame_para + base_para` patched in at
  link time (base_para = load_addr>>4) instead of an MZ reloc record; layout
  starts at byte 32 (`RAW_STUB_SIZE`) so segment paragraph alignment holds.
- The synthesized head stub: `cli; mov ss/sp; mov ds/es=DGROUP; jmp far
  entry` — all constants known at link time (same `_compute_ss_sp()` as the
  MZ header, SS=DGROUP + SP=stack-top-in-DGROUP for the small model).  The
  hlt-padded 32-byte head means entry lands at image para 2.
- Program-RAM ceiling check: image end past 0x9F000 (video RAM at 0xA0000)
  is a link error.
- MZ path refactor (shared `_concat_segments`/`_compute_ss_sp`) verified
  byte-identical: snprintf_test relink `cmp` + MP compact whole-image `cmp`.
- `test_omf_link.sh` test 3 asserts the raw structure (no MZ sig, stub
  opcodes at fixed offsets, entry 0302:0000, far-call selector 0303
  absolute); also fixed test 2's stale-crt0 collision (stevie-orig now
  carries crt0_exe.obj — excluded from the smoke link).

### Bare-metal runtime story (minic/dos/newlibc/)
- `bm_crt0.c`: C `start()` (OMF `_start`) → `bm_board_init()` → `main(0,0)`
  → `while(1) hlt`.  No DOS crt0, no PSP, no HALT2DOS rewrite — bare metal
  WANTS the hlt idle loop.  BSS zero-fill not needed (in-image zeros).
- `bm_console.c` + `.h`: polled 7201 channel-A TX at 9600.  Mirrors
  newlibc drivers/console.c exactly (VIA2 port A bit0 internal clock; 8253
  counter 0 — NOT counter 1 — mode 2 LSB+MSB divisor 8; channel reset +
  WR0/WR4=0x44/WR3=0xC1/WR5=0xEA/WR1=0); all access via v9k_hw.h
  HW_READ/WRITE_BYTE volatile-far MMIO.  The original's inline asm
  (SAVE_ES/RESTORE_ES, forced byte stores) is ia16-gcc damage control this
  backend doesn't need.  Plus bm_puts/bm_putu (32-bit udiv)/bm_puthex.
- `hello_bm.c`: 48 KB raw image; checks volatile 16-bit mul, 32-bit
  unsigned divide, strlen, hex print; __V9BEGIN__/__V9END__ sentinels +
  PASS:/FAIL: verdict (newlibc run_test.sh regex convention).
- libstub on bare metal: `--no-stdio` libstub links fine (its INT 21h
  sites — exit/putc/dos_*/int86 — are functions, nothing runs at startup;
  `_dgroup_para: dw DGROUP` resolves via the raw selector patch).  They are
  LANDMINES if called; the real fix is newlibc replacing libstub (the
  Phase-6 end state).

### Harness
- `tools/build-newlibc-baremetal.sh [--load-addr=] <name|path.c>`: test TU +
  bm_crt0 + bm_console, small model, `--no-stdio` libstub, raw link.  Bare
  name resolves minic/dos/newlibc/ first, then newlibc tests/.
- `tools/run-victor-baremetal.sh <bin> [secs]`: MAME victor9k + Lua
  autoboot loader (newlibc phase-3 pattern: write_u8 loop at 0x3000, zero
  segment regs, IP=0x3000), serial via `-rs232a null_modem -bitbanger`,
  sentinel-trimmed stdout, exit 77 skips, same orphan-killer watchdog as
  run-victor-sasi.sh.
- `tools/test-victor.sh` new entry "victor bare-metal (hello_bm)" diffs
  against `minic/dos/tests/hello_bm.golden.txt`; skips when newlibc tree or
  MAME absent.  Victor pipeline 3/3.

### Open tracks (new + carried)
- newlibc step 4: ISR definition strategy; extended-asm output-constraint
  store marking + Intel-syntax template translation; port timer/display
  drivers to minic dialect (the bm_console port shows most driver asm is
  removable); grow the bare-metal test battery → tools/test-newlibc.sh.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp; multi-decl items after the first skip block_scope_decl;
  Kw spill-slot sharing.

---

# Next session (§6c — continue Phase 6.  §6b [2026-06-11, this session] completed **step 2: the newlibc portable subset runs DOS-hosted and is gated.**  ELEVEN phase3 tests (snprintf, fat_bpb/chain/root/dir/file, fat_vfs, ramfs, stdio_route, bss, terminal_meta) now build small-model via `tools/build-newlibc-test.sh` — test TU (`-Dmain=newlibc_test_main`) + libgloss/VFS/FAT/block + `minic/dos/newlibc/dos_shim.c` against crt0 + `libstub_to_exe.py --no-stdio` — and byte-diff against goldens in `tools/test-dos.sh`.  The full newlibc stack (printf wrappers → syscalls → VFS → devices/FAT-over-RAM-block) executes through THIS toolchain in DOSBox.  Two toolchain bugs fixed en route (static file-scope DATA linkage; decimal `UL` literal typing), both probe-gated.  Gate 274→**287/287**.  MP compact byte-identical (rigorous: pre-change toolchain rebuilt from git and whole-image `cmp`'d).  Next: **step 3** — omf_link raw-binary output + minic-built crt0 + MAME bare-metal hello at load addr 0x3000; then step 4 (drivers/ISRs/extended-asm).)

## §6b session notes (2026-06-11)

### The two toolchain fixes (probe-gated, loud-verified pre-fix)
1. **Static file-scope DATA had no internal linkage** — minic NEVER emitted `export`
   on data (`export data` is valid QBE it just didn't use), and asm_to_omf.py
   compensated by auto-promoting EVERY data `_xxx:` label to an OMF public; two TUs
   reusing a static name died "duplicate public symbol" (newlibc: libgloss/dirent.c
   and vfs/vfs.c both define `static … dir_table[]`).  Fix mirrors §1q's function
   story end-to-end: minic marks non-static file-scope data `export data` (new
   `glostatic[]` flag; an `nglo` watermark `glo_decl_start` captured in
   type_and_ident and retro-marked by the STATIC typed_decl variants — the variants
   reduce AFTER typed_decl_rest registers the slots, and the lexer's pending_static
   is already reset by then; `emit_static_local` marks mangled function-locals;
   `STATIC structstart` marks its sai slot) and asm_to_omf.py's data auto-promotion
   is REMOVED — `.globl` is now authoritative for code AND data.
   `static_data_probe` (TWO TUs, small+medium gate entries: same-name file statics,
   same-name block statics behind same-name static fns, plus a cross-TU extern
   global proving export still works).
2. **Decimal `12345UL` was typed int** — the decimal lexer's `u`-suffix branch
   consumed a trailing `L` without setting suffix_l (hex/octal were correct), so a
   `UL` literal ≤0xFFFF was pushed as ONE stack word; newlibc's snprintf_test `%lu`
   read a garbage high word (`12345UL` printed 1093808185 = 0x4132<<16 | 12345).
   longconst_probe gained ulvararg/ulassign cases (golden regenerated, medium+large).

### Step-2 infrastructure (committed, not probe-grade)
- `tools/build-newlibc-test.sh`: small model only (far models would need newlibc's
  printf to answer minic's far_stdlib mangling — later phase).  `NEWLIBC_DIR`
  overrides `~/projects/newlibc/phase3_newlib`.
- `minic/dos/newlibc/shiminc/`: the §6a triage shim headers PROMOTED to the
  committed tree (gate reproducibility); `build/newlibc-triage/sweep.sh` still has
  its own copy (probe-grade, can be repointed later).
- `minic/dos/newlibc/dos_shim.c`: console/tty device ops (INT 21h AH=3F/40 via
  libstub int86), 100Hz wall-clock timer (AH=2Ch), display_puts→console,
  POSIX unprefixed aliases (open/read/write/…→`_`-syscalls), minimal
  FILE-table fopen/fclose/fread/fgetc over VFS fds (libstub's one-word
  `_stdin/_stdout/_stderr` sentinels are layout-compatible with newlib's
  `FILE{int _file;…}` first member — kept), `_impure_ptr`/`__heap_start/_end`
  link satisfaction, and `main()` = `vfs_init()` (board_init()'s job on metal)
  then `newlibc_test_main()`.
- `libstub_to_exe.py --no-stdio` (near-code only): drops FILEIO_EXE+FAR_SPRINTF_EXE
  and skips libstub.asm `_sprintf/_fgets/_putchar/_abort/_stat` so the newlibc
  printf family + dos_shim own those names; keeps malloc/free, str/mem fns,
  int86/intdos, `_qbe_*` helpers, and the stdio sentinels.
- Gate: `run_newlibc_test` in tools/test-dos.sh, skip (77) when the newlibc tree
  is absent; goldens `minic/dos/tests/newlibc_<test>.golden.txt`.

### Excluded / deferred tests (with reasons)
- memory_test: scans the Victor physical memory map — hardware-flavored, fails
  DOS-hosted by design; belongs to step-3+ MAME bare-metal.
- stdin_test / scanf_test / read_test: need stdin feeding — run-dos-exe.sh has no
  input-redirect support yet (DOS `< file` works per the §3n REPL work; small
  harness lever if wanted).
- hello: BUILDS and is shimmed (timer/display/malloc) but prints wall-clock tick
  values — not golden-able as-is.

### Open tracks (new + carried)
- newlibc step 3: omf_link raw-binary output mode + minic-built crt0 + MAME
  bare-metal hello (load addr 0x3000, serial output); then tools/test-newlibc.sh.
- run-dos-exe.sh stdin redirect (unlocks 3 more newlibc tests).
- newlibc-under-far-models stdio story (minic far_stdlib mangling vs newlibc's
  own printf) — decide when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g, static_sym_init_probe reproduces);
  extended-asm output constraints + Intel template translation (step 4); ISR
  definition strategy (step 4); param/static-local shadowing a global; huge
  `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic
  static-init FLOAT const-expr folding; small setjmp/longjmp; multi-decl items
  after the first skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§6a and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
