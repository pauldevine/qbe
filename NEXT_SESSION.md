# Next session (§7u — continue Phase 6 libstub retirement / open compiler tracks.  §7t [2026-06-15, this session] **WIDENED `--no-libstub` to the far-DATA models compact + large — three probes (`printf_nolibstub_probe`, `dos_libc_probe`, `dos_file_probe`) now run libstub-free, byte-identical to their existing goldens, under compact AND large.  test-dos 348 → 360; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  §7q/§7p had the libstub-free path on small (raw near runtime) + medium (far code, near data via `near_to_far_rt.py`); §7t covers the far-DATA tier.  **KEY DISCOVERY: in THIS backend compact/large/huge are ALL far code + far data** — minic `NEAR_CODE()` EXCLUDES MCompact (`minic.y:46-52`), and the compact probe emits `call far`/`retf` — so the far-data path is UNIFORM (not the textbook compact=near-code split).  Two mismatches vs medium, both fixed COPY/ADD (all-new asm, no compiler touch): (1) **far-pointer syscall ABI** — `int86`/`intdos`/`segread` mangle to `_far_int86`/… (minic `far_stdlib[]`) and must read their `union REGS`/`struct SREGS` through the caller's FAR segment (ES:BX), so the new **`minic/dos/dos_syscall_far_data.asm`** ports `libstub_to_exe.py`'s `FAR_DOSIO_EXE` `_far_int86`/`_far_intdos`/`_far_segread` VERBATIM + a far-pointer **`_int86x`** (int86x is NOT in `far_stdlib[]` → unmangled `_int86x` with far-ptr args; `dos_vfs.c`'s `vfs_rename` needs it, and `dos_vfs` is ONE CODE segment so `vfs_write`/printf keeps `vfs_rename`+`_int86x` live through `--gc-sections`), linked INSTEAD of the near-data `dos_syscall.asm`.  (2) **far_stdlib name bridges** — printf/str*/mem* call sites mangle to `_far_X`, but newlibc/dos_libc DEFINE the plain `_X` (definitions emit the real name; only CALL SITES mangle) and, compiled `-m compact`, those plain defs ALREADY have the correct far-pointer ABI — the only gap is the NAME.  The new **`minic/dos/far_stdlib_bridge.asm`** bridges each with a far tail-call `_far_X: jmp far _X` (a `jmp`, NOT a `call far` thunk — preserves the variadic printf arg frame; `jmp far _sym` resolves through omf_link's location-3 far fixup just like `call far`, verified), each thunk in its own CODE segment so `--gc-sections` drops the unreferenced ones.  `qbe_rt` is the same `near_to_far_rt.py` far-code rewrite as medium; `heap.asm` unchanged.  **huge is NOT yet supported libstub-free**: printf + the far bridges + far int86 all work, but `malloc` returns NULL — newlibc's `_sbrk` brackets the heap with `next_heap > __heap_end`, a huge-model pointer compare against the UNNORMALIZED `__heap_end` symbol address that the huge-pointer path mis-evaluates (the §7g/§4s huge-normalization family; malloc_probe only ever ran small so this heap path was NEVER huge-exercised).  huge *libstub* is unaffected (libstub's own malloc), so it's a real but bounded compiler/runtime investigation; `build-example.sh` rejects `--no-libstub --model=huge` with a precise message.  Gate: the three §7q/§7r/§7s probe loops in `test-dos.sh` grew `small medium` → `small medium compact large` (12 new entries: compact+large × {libstub anchor, libstub-free}, all FIRST-RUN identical to the existing goldens — anchor = libstub's python printf, so a wrong far-pointer ABI corrupts the syscall/printf and diffs the golden, a missing bridge fails the link).  STRATEGY unchanged (COPY/ADD, NEVER MUTATE): `dos_syscall_far_data.asm`/`far_stdlib_bridge.asm` all-new; `build-example.sh`/`test-dos.sh` additive; `libstub.asm`/`libstub_to_exe.py`/`near_to_far_rt.py`/`dos_syscall.asm`/`qbe_rt.asm`/`heap.asm`/`dos_vfs.c`/newlibc UNTOUCHED, so MP/stevie/every gate provably can't regress (MP NOT rebuilt — links none of these).  **⇒ Next session (§7u): CONTINUE libstub retirement.**  (1) **huge libstub-free** — fix the `_sbrk` huge-pointer heap-compare (`next_heap > __heap_end` against the unnormalized `__heap_end` symbol address); this is likely a real compiler/runtime fix (huge pointer-relational compare of an unnormalized symbol address) → needs an emit-audit + MP byte-compare, unlike §7t.  Reduce first: a probe doing `&heap_end_sym` compared to a normalized huge pointer.  (2) **`build-stevie.sh --no-libstub --model=compact/large`** — stevie far-data; the file path (fopen/fputs/getc/remove/stat incl. int86x rename + the §7s `vfs_stat` no-write) is now PROVEN by `dos_file_probe` compact/large, so this is mostly build-glue (mirror the §7t far-data runtime branch into build-stevie.sh, watch `STEVIE_HEAP_SIZE` vs the far-data DGROUP).  (3) the end-state — make `--no-libstub` the default (retire `libstub_to_exe.py`'s python printf), needs huge done + broad re-verification.  Carried compiler tracks (await a consumer): the aoa sub-gaps (file-scope/static multi-decl array-first parse-error gap; plain `jmp_buf a, b;` multi-decl).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug currently open.)

## §7t session notes (2026-06-15)

### The pick (continued §7s handoff — user chose far-DATA --no-libstub)
- The §7s handoff listed three §7t tracks; the user picked far-DATA --no-libstub
  (compact/large/huge), the only fully-autonomous one (deeper stevie verify is
  keyboard-bound, the default is premature).

### Key discovery — compact/large/huge are ALL far code + far data here
- minic NEAR_CODE() EXCLUDES MCompact (minic.y:46-52); the compact probe emits
  `call far`/`retf`.  So the far-data path is UNIFORM across compact/large/huge,
  NOT the textbook compact=near-code/far-data split.  This made the design one
  branch, reusing the medium qbe_rt far-code rewrite (near_to_far_rt.py).

### Two new all-new asm files (COPY/ADD, no compiler touch)
- minic/dos/dos_syscall_far_data.asm: _far_int86/_far_intdos/_far_segread ported
  VERBATIM from libstub_to_exe.py FAR_DOSIO_EXE (far-ptr ES:BX, retf, args @
  [bp+6]) + a far-ptr _int86x (unmangled name — int86x not in far_stdlib[]).
  Linked INSTEAD of the near dos_syscall.asm under far data.
- minic/dos/far_stdlib_bridge.asm: _far_X: jmp far _X tail-call thunks (printf/
  fprintf/sprintf/puts/fputs/fputc/fgets/fopen/fclose/fread/fwrite + str*/mem*),
  each in its own CODE segment for per-thunk --gc-sections.  jmp (not call far)
  to preserve the variadic printf arg frame; nasm needs `extern _X` per thunk.

### Traps / findings
- nasm: `jmp far _X` needs `extern _X` declared in the module (else "symbol not
  defined").  Verified `jmp far _sym` resolves through omf_link location-3 far
  fixup exactly like `call far`.
- gc-sections is PER-SEGMENT (reachable-from-entry).  dos_vfs is ONE segment, so
  printf (→ vfs_write) keeps the whole module live incl. vfs_rename → _int86x;
  hence _int86x must exist even for a printf-only program (and it's needed for
  stevie's rename anyway).
- huge: lines 1-4 (printf/dec/hex/sum via far bridges + far int86) WORK; only
  malloc fails at _sbrk's `next_heap > __heap_end` huge-pointer compare against
  the UNNORMALIZED __heap_end symbol address.  huge+libstub works (own malloc).
  Deferred — rejected in the build-example.sh guard.

### Gate
- build-example.sh: guard relaxed to allow compact/large (reject tiny + huge);
  runtime block split medium (near-data) vs compact/large/huge (far-data:
  qbe_rt far rewrite + dos_syscall_far_data.asm + far_stdlib_bridge.asm).
- test-dos.sh: the printf_nolibstub / dos_libc / dos_file probe loops grew
  `small medium` → `small medium compact large`.  test-dos 348 → 360, all green.
  No compiler source → no emit audit, no MP byte-compare.

### ⇒ Next session (§7u)
- huge libstub-free: fix _sbrk huge-pointer heap-compare (emit-audit + MP compare).
- build-stevie.sh --no-libstub --model=compact/large (file path proven by
  dos_file_probe compact/large; mostly build-glue + STEVIE_HEAP_SIZE vs DGROUP).
- Ultimate: make --no-libstub the default.
---

# Next session (§7t — continue Phase 6 libstub retirement / open compiler tracks.  §7s [2026-06-14, this session] **FIXED libstub-free file I/O — stevie now LOADS and SAVES real DOS files.  §7r got stevie running libstub-free but its file I/O was broken (the user hit "New File" on `stevie config.sys` and "Can't open file for writing!" on save); the §7r smoke test only reached the input loop, never a real file.  test-dos 344 → 348; no compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare.**  ROOT CAUSE: a normal program's `fopen`/`getc`/`fputs` funnel through newlibc's stdio → `_read`/`_write(fp->_file)` → syscalls.c → `vfs_*`, and newlibc's `vfs.c` is the BARE-METAL VFS (FAT-on-block/SASI + ramfs + device table) — it has no backend for a plain DOS path like `config.sys`, so `vfs_open` returned -ENOENT (load → "New File") and could not create (save → the error).  The bare-metal VFS is simply the wrong filesystem for a DOS host, where DOS itself *is* the filesystem.  FIX: a new `minic/dos/newlibc/dos_vfs.c` implementing exactly the `vfs_*` surface syscalls.c/dirent.c/unlink.c/rename.c call (`vfs_open`/`close`/`read`/`write`/`lseek`/`stat`/`fstat`/`isatty`/`unlink`/`rename` + `vfs_init` no-op + dir stubs), each mapped to the matching DOS INT 21h file function (AH=3Dh open / 3Ch create / 3Fh read / 40h write / 3Eh close / 42h lseek / 41h unlink / 56h rename via int86x for ES:DI).  The fd IS the DOS handle: 0/1/2 are DOS's pre-opened CON handles, so the console path (printf's `_write(1)`, getchar's `_read(0)`) is unchanged with no special-casing, and opened files get DOS handles ≥5 — no fd table, no FAT/block/ramfs.  This is what libstub's open/read/write/fopen did directly; here it sits behind newlibc's stdio so the rest of the libstub-free stack (printf_wrappers, the dos_shim FILE layer, dos_libc) is untouched.  `dos_vfs.c` is linked INSTEAD of `vfs.c`+`fat.c`+`block.c` on the `build-example.sh`/`build-stevie.sh` `--no-libstub` path (the "real DOS program" path); `build-newlibc-test.sh` is UNCHANGED — its RAM/FAT-volume tests still use the bare-metal `vfs.c`+`fat.c`+`block.c` (those test the VFS/FAT machinery itself, which DOS-passthrough would bypass).  Dropping fat.c/block.c also shrank stevie's code 123,648 → 84,627 B.  **Gate:** the all-new `minic/dos/examples/dos_file_probe.c` round-trips a real DOS file (fopen"w" + fputs/fputc, fclose, fopen"r" + getc to EOF, remove, reopen→gone), gated FOUR ways like dos_libc_probe — small + medium × {libstub anchor (libstub's own INT 21h fopen/getc), libstub-free (dos_vfs.c)} against ONE golden; both runtimes do real INT 21h file I/O so the round-tripped bytes are identical, all four first-run identical.  **Verified on stevie itself:** `stevie TFILE.TXT` in DOSBox now renders the file content and the status line `"TFILE.TXT" 3 lines, 34 characters` (was "New File"); the dos_file_probe proves the write path (fopen"w"+fputs+fclose) stevie's save uses.  The two existing probes (printf_nolibstub, dos_libc) stay golden-identical on the dos_vfs.c stack (no file I/O → output unchanged).  **STRATEGY unchanged (COPY/ADD, NEVER MUTATE):** `dos_vfs.c` + `dos_file_probe.c` + golden are all-new; `build-example.sh`/`build-stevie.sh` swapped the support-set TUs on the `--no-libstub` branch only; newlibc's `vfs.c`/`fat.c`/`block.c`/`syscalls.c` and libstub are UNTOUCHED, so the newlibc FAT/VFS gate + MP + the libstub stevie provably can't regress.  Trap recorded: `rename.c` calls `vfs_rename` (so `dos_vfs.c` must provide it — INT 21h AH=56h needs ES:DI for the new name → `segread`+`int86x`); off_t is 16-bit so `vfs_lseek` returns the low word only (fine — stevie reads/writes sequentially).  **CRITICAL SECOND BUG (save was STILL broken; fixed same session):** the user reported load works but `:x`/`:w` on an EXISTING file gave status `"(null)" N lines` with the file UNCHANGED (`:w NEWFILE` and load both worked).  ROOT CAUSE: `struct stat` has DIFFERENT sizes in the two compile regimes that meet at `stat()` — stevie's minic/include `{int st_mode; int st_size; time_t}` (~8 B) vs the newlibc shiminc struct (~30 B, 11 fields) — and `vfs_stat`'s `for(i<sizeof(*st)) p[i]=0` zeroed the ~30 B shiminc size into stevie's ~8 B stack `statbuf` inside `writeit`, OVERFLOWING the stack and clobbering the adjacent `fname` local to NULL → the write went to a garbage name, config.sys untouched, status "(null)".  It only fired when `stat` SUCCEEDS (an existing file); a new file's `stat` fails early (-ENOENT, before the write) — exactly why `:w NEWFILE` worked.  libstub's `_stat` is a `-1` no-write stub, so it never overflowed — why the libstub build worked.  FIX: `vfs_stat`/`vfs_fstat` write NOTHING to `*st`, return status only (stevie uses `statbuf` solely for a no-op `chmod`; the cross-regime `struct stat` has no shared ABI, so any field write lands at the wrong offset anyway).  LESSON: passing a struct by pointer across the minic/include vs shiminc compile regimes is an ABI landmine (sizes/offsets differ) — `stat`/`fstat` were the only such cross-regime struct calls.  Gated by extending `dos_file_probe.c` with `stat_keeps_fname()` (mirrors writeit's frame — a `char *fname` param next to a stack `struct stat`, `stat()` on the existing file, return `fname`); bug-loud — the unfixed overflow sends the probe's `main` into a repeating crash loop (verified by reverting the fix).  Verified on stevie: `stevie IN0.TXT`, edit, `:x` now shows `"IN0.TXT" 3 lines, 14 characters` and the file is correctly changed — load AND save both work.  **⇒ Next session (§7t): CONTINUE libstub retirement.**  (1) **deeper stevie verification** — exercise edit→`:w`→reload, multiple files, `:e`, tags; confirm the SAVE path writes correct bytes on the real DOS disk (the probe + load are proven; a full edit/save/verify cycle is the remaining interactive check).  Watch the carried §7r notes: `system`/`getenv` are no-op stubs (`:!`/`$COMSPEC` won't work), the 32 KB `STEVIE_HEAP_SIZE` caps file size (DGROUP-bounded — now with fat/block gone there is MORE DGROUP headroom, so the heap could grow).  (2) far-DATA models (compact/large/huge) `--no-libstub` — `far_stdlib`-aware stdio + a far-pointer libc fill + a far-pointer `dos_vfs` (buffers would be far → INT 21h needs DS:DX set per-call via int86x), still awaits a consumer.  (3) the ultimate end-state — make `--no-libstub` the default (retire `libstub_to_exe.py`'s python printf engine outright).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps.  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open.)

## §7s session notes (2026-06-14)

### The report
- `tools/build-stevie.sh --exe` (libstub) works; `--no-libstub --exe` broke file
  I/O: `stevie config.sys` → "New File", save → "Can't open file for writing!".
  §7r's smoke test only reached the input loop (stevie reads keys via INT 21h
  AH=07h, not redirectable), so file I/O was never exercised.

### Root cause
- stevie fopen/getc/fputs -> newlibc stdio -> _read/_write(fp->_file) ->
  syscalls.c -> vfs_*.  newlibc's vfs.c is the BARE-METAL VFS (FAT-on-disk +
  ramfs + device table); no backend for a plain DOS path -> open fails.
- Can't modify upstream vfs.c/syscalls.c, and vfs_open has no catch-all hook.

### Fix — dos_vfs.c (DOS INT 21h, replaces vfs.c on the DOS-program path)
- New minic/dos/newlibc/dos_vfs.c implements the vfs_* surface via INT 21h
  (3Dh/3Ch/3Fh/40h/3Eh/42h/41h/56h).  fd = DOS handle: 0/1/2 = CON (console/
  printf path unchanged), files >=5.  No fd table / FAT / block / ramfs.
- Linked INSTEAD of vfs.c+fat.c+block.c on build-example/build-stevie
  --no-libstub.  build-newlibc-test.sh UNCHANGED (RAM/FAT tests need vfs.c).
  Dropping fat/block shrank stevie code 123,648 -> 84,627 B.
- Traps: rename.c calls vfs_rename -> dos_vfs provides it (AH=56h, ES:DI via
  segread+int86x); off_t is 16-bit so vfs_lseek returns low word only (stevie
  is sequential).

### SECOND bug — save STILL broken after the above (fixed same session)
- User: load works, but :x/:w on an EXISTING file -> status `"(null)" N lines`
  and the file is UNCHANGED.  (:w to a NEW file worked; load worked.)
- Root cause: struct stat has DIFFERENT sizes across the two regimes that meet
  at stat() — stevie minic/include {int st_mode; int st_size; time_t} ~8B vs
  newlibc shiminc ~30B (11 fields).  vfs_stat's `for(i<sizeof(*st)) p[i]=0`
  zeroed ~30B into writeit's ~8B stack statbuf -> STACK OVERFLOW clobbered the
  adjacent `fname` local -> NULL -> wrote to a garbage name, file untouched.
  Only fires when stat SUCCEEDS (existing file); a new file's stat fails early
  (-ENOENT, no write) -> why :w NEWFILE worked.  libstub's _stat is a -1
  no-write stub -> never overflowed -> why the libstub build worked.
- Fix: vfs_stat/vfs_fstat write NOTHING to *st, return status only (stevie uses
  statbuf only for a no-op chmod; cross-regime struct stat has no shared ABI,
  so any field write lands at the wrong offset anyway).  LESSON: passing a
  struct by pointer across the minic/include vs shiminc regimes is an ABI
  landmine; stat/fstat were the only such cross-regime struct calls.
- Verified: stevie IN0.TXT, edit (x), :x -> `"IN0.TXT" 3 lines, 14 chars`,
  file CHANGED correctly.  Load + save both work now.

### Gate + verification
- NEW dos_file_probe.c: fopen w + fputs/fputc, fclose, fopen r + getc to EOF,
  stat() on the existing file via stat_keeps_fname() (mirrors writeit's frame:
  char* fname + stack struct stat -> the overflow path), remove, reopen.
  4-way gate (small/medium x {libstub anchor, libstub-free}), one golden, all
  first-run identical.  test-dos 344 -> 348.  Bug-loud: the unfixed overflow
  sends the probe's main into a crash loop (verified by reverting the fix).
- stevie TFILE.TXT in DOSBox loads the file (status `"TFILE.TXT" 3 lines,
  34 characters`).  Existing probes (printf_nolibstub, dos_libc) stay golden
  on the dos_vfs stack.

### ⇒ Next session (§7t)
- Deeper stevie verify (multi-file, tags, large files, edit -> :w -> reload).
- far-DATA --no-libstub (far-ptr dos_vfs + stdio), awaits consumer.
- Ultimate: make --no-libstub the default.
---
Older session headers (§7s and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
