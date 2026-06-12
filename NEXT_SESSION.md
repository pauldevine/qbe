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

# Next session (§6b — continue Phase 6.  §6a [2026-06-11, this session] ran the newlibc TRIAGE SWEEP (Phase-6 step 1) and fixed SEVEN minic dialect gaps it exposed: the per-TU sweep (`build/newlibc-triage/sweep.sh` + `shiminc/` newlib-shaped shim headers) took phase3_newlib from **21/66 → 46/66 TUs compiling** under BOTH small and medium, and the **entire portable subset (libgloss ×7, vfs ×2, every non-asm test) now compiles** — exactly the step-2 target population.  Every remaining failure is inline-asm-flavored (drivers + dos_tests) or the ISR-definition gap — i.e. step-4 work, none of it portable-subset.  Six new probes, gate 262→274.  Next: **step 2 proper** — link the portable subset against libstub, run VFS/FAT/printf tests DOS-hosted in DOSBox, gate them; then step 3 (omf_link raw-binary output + minic crt0).)

## §6a session notes (2026-06-11)

### The seven minic fixes (each probe-gated, all loud-verified pre-fix)
1. **`extern T *f(args);`** — extern + pointer-return + ANSI params had NO production
   (only K&R `*f()`); errno.h's `extern int *__errno(void);` killed ~29 TUs at line 1.
   New ext_decl kind `'H'` (par1 stashed on `->l`, fnproto_record'd in the extern walks).
   `extern_ptrret_probe`.
2. **File-scope prototype param leak** — par1's param() varadds names at file scope and
   nothing removed them after a PROTOTYPE; a later decl reusing the name with a different
   type died "double definition" (`int first(char *buf,…); extern long second(…,const
   void *buf,…)`).  Fixed: `varclr()` at the end of every file-scope prototype-only
   reduction (ansi_proto_register, EXTERN par1, both ext_decllist walks).  Definitions
   were always safe (init_ansi/init_kr varclr first).  `proto_param_leak_probe`.
3. **Array parameter declarators** — par1 had no `'['…']'` forms at all: `uint8_t out[11]`,
   `char buf[]`, `char *const argv[]` all parse-errored (the *const was a red herring —
   `type '*' CONST` existed).  Four new par1 productions, decay to (far-aware) pointer,
   dimension folded and discarded.  `array_param_probe`.
4. **`void __far __attribute__((interrupt)) f(void);`** — the ia16-gcc far-ISR spelling
   (interrupts.h) had no production for the interposed `__far`; new
   `type TFAR attropt IDENT` in type_and_ident accepts-and-drops the __far.  PROTOTYPE
   only: ISR *definitions* remain a designed gap (the vestigial interrupt emission
   produces `asm "iret"` with no block terminator — QBE rejects — and would skip the
   epilogue anyway; Phase-6 step 4 decides the real ISR strategy).  `isr_far_attr_probe`.
5. **`const volatile T`** — qualifier pair missing everywhere; new `vol_qual` nonterminal
   (VOLATILE | CONST VOLATILE | VOLATILE CONST) replaced the bare-VOLATILE heads in all
   type productions (incl. STRUCT/UNION/TNAME).  Covered by the font_test-shaped probe
   cases inside array_param/others; no dedicated probe (parse-only, exercised by sweep).
6. **Scalar global symbol-address init** — `char **environ = __env;` / `int *p = &x;` /
   `int *mid = &arr[2];` died (the `'=' expr ';'` rule folded with const_eval only; the
   aggregate path §1b/§1g could already emit `$sym+off`).  Now routed through cival_eval
   → new emit_global_sym_init.  `static_sym_init_probe` (small+medium).  **The probe
   under compact CONFIRMED the §1g far static-DATA-ptr reloc gap at runtime** (prints raw
   offsets 4194/4192 — segment missing) — now a reproducible open track, NOT gated far.
7. **Locals shadow file-scope bindings** — minic had NO local-shadows-global support
   (`int g; int f(){int g;}` died), only §1k local-vs-local inner-block renames.  newlibc
   vfs_open declares `const fat_mount_t *fat_mount;` next to the file-scope function
   fat_mount() (found by automated delta-reduction of the 785-line failing prefix).
   Fixed: block_scope_decl's rename trigger extended to any global/extern/function/enum
   binding + block_scope_decl wired into the dcls-chain local rules (fn-body depth) —
   the stmt-context rules already had it.  `local_shadow_probe` (global var + function +
   enum constant all shadowed, post-shadow global intact).
   Plus: postfix prototype attribute `void _init(void) __attribute__((weak));` (new
   `ansi_proto_register ATTRIBUTE…';'` production), and `die("undefined variable")` now
   prints the NAME (4 shim-gap diagnoses fell out instantly).

### Sweep infrastructure (build/newlibc-triage/, intentionally untracked probe-grade)
- `sweep.sh [model]`: per-TU clang -E (-nostdinc **-D__ia16__** — keeps `__far` real and
  selects the GCC MK_FP branch in v9k_hw.h, which matches minic semantics) → minic →
  qbe → asm_to_omf → nasm, keep-going, stage-bucketed report.
- `shiminc/`: newlib-shaped shim headers (errno/unistd/fcntl/reent/dirent/stdio with
  struct FILE._file/sys/stat with S_IF*+S_BLKSIZE/sys/types with dev_t/time/limits/io/
  conio→dos.h/i86→dos.h).  These prefigure the real newlibc-port headers.
- minic line numbers in errors are 0-based-ish (error:0 = line 1); statements are emitted
  at the function-close reduce, so stmt-level errors report the `}` line — bisect inside.

### Remaining failures (all step-4 flavored, NONE portable-subset)
- 6 dos_tests: Watcom `_asm { … }` blocks (phase-1-style TUs; park or rewrite later).
- 5 qbe-stage: extended-asm `"=r"` OUTPUT constraints — minic substitutes the slot into
  the template but QBE sees a slot that is read-never-stored and rejects; ALSO
  interrupts.c ISR definition (designed gap, see fix 4).
- 8 nasm-stage: AT&T/ia16 mnemonics leak through the template (`pushfw/popw %0`,
  `movw %es`) — nasm wants Intel.  Driver asm will need per-target porting in step 4
  anyway (it was written for ia16-gcc).
- 46/66 small AND medium, identical fail sets.

### Open tracks (new + carried)
- far static-DATA-ptr reloc (§1g, now runtime-reproduced by static_sym_init_probe under
  compact) — needed before newlibc-style tables-of-pointers work in far-data models.
- minic extended-asm output-constraint store marking + Intel-syntax template translation
  (the qbe/nasm buckets above) — step 4.
- ISR definition strategy (minic save-all+iret codegen vs asm shims) — step 4.
- Param/static-local shadowing a global still dies (only plain locals fixed).
- Carried: huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v, unreduced); minic
  static-init FLOAT const-expr folding; small setjmp/longjmp; multi-decl items after the
  first skip block_scope_decl; Kw spill-slot sharing.

---

Older session headers (§5c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
