# Next session (§7v — continue Phase 6 libstub retirement / open compiler tracks.  §7u [2026-06-15, this session] **COMPLETED huge libstub-free — fixed the `_sbrk` MHuge pointer-relational heap-compare, so malloc + the whole heap chain now works under huge with NO libstub; the three build-example probes (`printf_nolibstub_probe`, `dos_libc_probe`, `dos_file_probe`) now run libstub-free byte-identical to their existing goldens under huge too.  test-dos 360 → 366.  The fix is in `minic.y` (frontend, MHuge-gated) → no emit audit; MP compact body 731,088 byte-identical → no Victor run.**  §7t had the libstub-free path on small/medium/compact/large but DEFERRED huge: newlibc's `_sbrk` (libgloss/syscalls.c) brackets the heap with `next_heap < __heap_start || next_heap > __heap_end`, and under huge those relational compares were emitted as a FLAT 32-bit unsigned `cultl` of the seg:off words — correct ONLY when both operands are NORMALISED huge pointers (so the seg:off word is monotonic in linear address).  But `__heap_end`/`__heap_start` are bare symbol addresses (`$sym`, raw `DGROUP:offset`, offset can exceed 0xF = UNNORMALISED), while `next_heap` went through `_qbe_huge_add` (normalised) — so `cultl normalised, unnormalised` mis-ordered them and `malloc` returned NULL (the §7g/§4s huge-normalization family; `malloc_probe` only ever ran small so this path was never huge-exercised).  **FIX (minic.y, ~33 lines, the `Binop` default arm):** under `memmodel == MHuge`, a pointer RELATIONAL compare (`p < q`, `p <= q`; the parser already lowers `p > q`/`p >= q` to swapped-operand `<`/`<=`) with BOTH operands pointers (non-function) now routes through `_qbe_huge_cmp(p, q)` (the same helper ptr−ptr subtraction already uses — it recomputes `seg*16+off` from the raw words → linear(p)−linear(q), NORMALISATION-INVARIANT) and tests its sign: `p<q ⟺ csltl cmp,0`, `p<=q ⟺ cslel cmp,0`.  MHuge-gated: compact/large/near keep the flat `cultl` (verified compact still emits `cultl`, no `huge_cmp`), so the MP compact corpus is byte-identical (confirmed: body still 731,088 B) and the §7t compact/large probes are untouched.  Equality (`==`/`!=`) of two non-null huge pointers from different normalisations has the SAME latent flat-compare gap but `_sbrk` only uses `== NULL` (0:0, linear 0 — fine) so it stays unfixed (no consumer; relational was the bug).  **Gate:** `build-example.sh` dropped its `--no-libstub --model=huge` rejection (the far-DATA runtime branch already handled huge identically to compact/large from §7t — only the guard blocked it); the three §7t probe loops in `test-dos.sh` grew `small medium compact large` → `+ huge` (6 new entries: 3 probes × {libstub anchor, libstub-free}).  Bug-loud VERIFIED: reverting the minic fix makes the unfixed compiler emit `cultl` and `printf_nolibstub_probe` (which does a malloc/strcpy round trip) print `FAIL: malloc returned NULL` under huge.  All 366 green; the existing huge probes (hugeprobe, gc_bigheap_probe, huge_norm_probe, the caddr family, …) all still pass → no huge codegen regression.  **STRATEGY:** the COPY/ADD-NEVER-MUTATE asm/python toolchain (libstub.asm, libstub_to_exe.py, near_to_far_rt.py, qbe_rt.asm, dos_syscall*.asm, far_stdlib_bridge.asm, heap.asm, dos_vfs.c, newlibc) is all UNTOUCHED; the only source change is the MHuge-gated minic.y arm + two additive build/gate edits, so MP/stevie/every existing gate provably can't regress.  **⇒ Next session (§7v): CONTINUE libstub retirement.**  (1) **`build-stevie.sh --no-libstub --model=compact|large|huge`** — stevie far-data.  The far-data file path (fopen/fputs/getc/remove/`stat` incl. the §7s `int86x` rename + `vfs_stat` no-write) is now PROVEN by `dos_file_probe` under compact/large/huge, so this is mostly build-glue: mirror the §7t/§7u far-DATA runtime branch from `build-example.sh` into `build-stevie.sh` (qbe_rt far rewrite + `dos_syscall_far_data.asm` + `far_stdlib_bridge.asm`), and watch `STEVIE_HEAP_SIZE` vs the far-DATA DGROUP (more headroom now that fat/block are dropped — §7s).  Startup-screen byte-compare vs the libstub baseline is auto-checkable (§7r pattern); interactive edit/save is handed to the user (keyboard-bound).  (2) the ultimate end-state — make `--no-libstub` the DEFAULT (retire `libstub_to_exe.py`'s python printf engine outright), now that every model (small/medium/compact/large/huge) is proven libstub-free; needs broad re-verification of every model/program (MP stays libstub — it links none of this).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (no consumer).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7u session notes (2026-06-15)

### The pick (continued §7t handoff — huge libstub-free, the autonomous track)
- §7t listed three §7v... §7u tracks; the user prompt was "continue", so I took
  the meatiest fully-autonomous one: huge libstub-free (the real compiler fix the
  §7t handoff flagged as needing emit-audit + MP compare).  build-stevie far-data
  and the --no-libstub default both involve interactive/broad verification, left
  for §7v.

### Root cause (reduced first, per the §7t handoff advice)
- Reduced _sbrk to /tmp/sbrk_red.c (extern char __heap_start[]/__heap_end[],
  heap_ptr+incr, the two relational bounds) and emitted `minic -m huge`: the
  compares came out `%t =w cultl %next, $__heap_start` and `cultl $__heap_end,
  %next` — a FLAT 32-bit unsigned compare of seg:off words.
- next went through _qbe_huge_add → NORMALISED; $__heap_start/$__heap_end are raw
  symbol addresses (DGROUP:offset, UNNORMALISED, offset can exceed 0xF).  Flat
  `cultl normalised, unnormalised` mis-orders them → _sbrk's `next > __heap_end`
  wrongly true → malloc NULL.  The line-4493 comment in minic.y ("normalisation
  makes the seg:off word monotonic") assumed BOTH operands normalised.

### Fix — route MHuge pointer relational compares through _qbe_huge_cmp
- minic.y Binop default arm, right after the existing MHuge ptr−ptr-subtraction
  block (which already uses _qbe_huge_cmp): under MHuge, o in {'<','l'} with both
  operands PTR (non-FUN), emit `%cmp =l call $qbe_huge_cmp(l p, l q)` then
  `%res =w csltl/cslel %cmp, 0`.  huge_cmp recomputes seg*16+off from raw words →
  linear difference, correct for normalised AND unnormalised operands.
- MHuge-gated (compact/large/near keep flat cultl).  '>'/'>='  already lower to
  swapped '<'/'<=' at parse time, so only two op cases needed.  Conflicts
  UNCHANGED at 115 (action-only, no new productions).

### Verification
- huge SSA now emits 2× huge_cmp (was 2× cultl); compact still emits cultl, no
  huge_cmp → MHuge-gating confirmed.
- All 6 huge probe cases (printf_nolibstub/dos_libc/dos_file × {libstub anchor,
  libstub-free}) build + run byte-identical to their goldens.
- BUG-LOUD verified: reverting minic.y → unfixed `cultl` → printf_nolibstub_probe
  prints "FAIL: malloc returned NULL" under huge.
- make check green; test-dos 360 → 366 (0 fail), all existing huge probes still
  [ok]; MP compact body 731,088 B byte-identical (frontend MHuge-gated change →
  no emit audit, compact unchanged → no Victor run).

### Gate plumbing (additive)
- build-example.sh: removed the `huge) ... exit 2` --no-libstub rejection; the
  far-DATA runtime branch (else: qbe_rt far + dos_syscall_far_data + far bridge)
  already covered huge.
- test-dos.sh: the printf_nolibstub / dos_libc / dos_file probe loops grew
  `small medium compact large` → `... huge`.

### ⇒ Next session (§7v)
- build-stevie.sh --no-libstub --model=compact|large|huge (file path proven by
  dos_file_probe; mostly build-glue + STEVIE_HEAP_SIZE vs far-DATA DGROUP;
  startup-screen byte-compare auto, interactive edit/save to user).
- Ultimate: make --no-libstub the default (retire libstub_to_exe.py python printf).
---

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
Older session headers (§7t and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
