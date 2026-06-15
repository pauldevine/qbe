# Next session (§7w — continue Phase 6 libstub retirement / open compiler tracks.  §7v [2026-06-15, this session] **WIDENED `build-stevie.sh --no-libstub` to the far-DATA models compact + large + huge — the full 24-TU stevie editor now builds libstub-free as a compact/large/huge .EXE, and its startup screen is BYTE-IDENTICAL to the libstub anchor at each model AND to the §7r medium baseline.  No compiler/qbe/emit/minic source touched → no emit audit, no MP byte-compare; test-dos UNCHANGED at 366/366 (this is build-glue, verified by a one-time startup-screen byte-compare, the §7r pattern — stevie is interactively verified, not a standing gate entry).**  §7r had stevie libstub-free at medium; §7t/§7u proved the far-DATA libstub-free RUNTIME (qbe_rt far rewrite + `dos_syscall_far_data.asm` ES:BX far-pointer int86 family + `far_stdlib_bridge.asm` `_far_X: jmp far _X` name bridges + the §7u MHuge `_sbrk` heap-compare fix) byte-identical to goldens under compact/large/huge via three build-example probes (`printf_nolibstub_probe`/`dos_libc_probe`/`dos_file_probe`), and the stevie far-data FILE path (fopen/fputs/getc/remove/`stat` incl. the §7s `int86x` rename + `vfs_stat` no-write) is exactly what `dos_file_probe` exercises — so §7v is the build-glue that points stevie at that already-proven runtime.  **The change (`tools/build-stevie.sh` only, +33/-11):** (1) the `--no-libstub` guard relaxed from `small|medium`-only to reject ONLY `tiny` (the .COM-only model); (2) the runtime block, previously two-way (`small` raw / `else`=medium near_to_far), became the SAME three-way as `build-example.sh` — `small` (raw near qbe_rt/dos_syscall), `medium` (qbe_rt + dos_syscall both `near_to_far_rt.py`, near data), and `compact|large|huge` (qbe_rt far rewrite + `dos_syscall_far_data.asm` INSTEAD of the near dos_syscall + `far_stdlib_bridge.asm`, all far data).  `crt0_exe` already branched on FAR_DATA for compact/large/huge; the newlibc support TUs already compile `-m $MODEL`; `heap.asm` + `--gc-sections` unchanged.  **Verification:** all three far-data models link clean (0 libstub symbols in the map, 37 far-bridge/far-syscall symbols present; compact/large code 207,821 B + data+bss 56,996 B; huge code 224,714 B), and — since stevie renders its screen through the newlibc console write path (`write(1)` → dos_shim → INT 21h AH=40h, captured by `run-dos-exe.sh`'s `> OUT.TXT`) — running stevie under DOSBox (it renders the full vi startup then blocks on a keypress; the idle block trips the run-dos-exe timeout, which dumps the COMPLETE rendered screen) shows the `Empty Buffer` status + `~` tildes + Victor `^[Y` cursor escapes BYTE-IDENTICAL across {compact,large,huge} × {libstub anchor, libstub-free} and equal to the compact baseline.  STRATEGY unchanged (COPY/ADD, NEVER MUTATE): only `build-stevie.sh`'s `--no-libstub` branch changed (default libstub path byte-unchanged — `make check` green, full test-dos 366/366 incl. the default-medium stevie size gate); all the far-data runtime asm/python/C (qbe_rt/dos_syscall_far_data/far_stdlib_bridge/heap/dos_vfs/dos_libc/near_to_far_rt + libstub.asm/libstub_to_exe.py) is UNTOUCHED, so MP/the libstub stevie/every gate provably can't regress.  Interactive edit/save on the far-data libstub-free stevie (open/edit/`:w`/`:q`) is keyboard-bound (INT 21h AH=07h, not redirectable) → handed to the user, exactly as §7r/§7s did for medium.  **⇒ Next session (§7w): CONTINUE libstub retirement.**  With every model (small/medium/compact/large/huge) now proven libstub-free for BOTH the build-example probes AND stevie, the remaining headline is **(1) the ultimate end-state — make `--no-libstub` the DEFAULT** (retire `libstub_to_exe.py`'s python printf engine outright): flip the default in `build-example.sh`/`build-stevie.sh`, re-verify EVERY model/program (the gate's per-model `runtime` vs `libstub-free` probe pairs collapse to one path; the libstub anchor must stay reachable as the equivalence reference during the transition, or the goldens lose their cross-check), and confirm MP (which links NONE of this — it stays libstub) and stevie still byte-match.  This is broad-but-mechanical; sequence it carefully so a regression is attributable.  Lower-appetite alternatives: deeper interactive stevie verification on Victor/DOSBox (edit→`:w`→reload, multi-file, tags — keyboard-bound, needs the user); OR pick from the carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; no consumer, `_sbrk` only does `== NULL`).  Bare-metal phase-3 bm_testhost tests are EXHAUSTED (`interrupt_test` SKIPPED §6v; `font_layout_test` declined §7m).  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7v session notes (2026-06-15)

### The pick (continued §7u handoff — build-stevie far-data, the autonomous track)
- §7u listed the §7v tracks; "continue" → took the meaty autonomous one:
  build-stevie.sh --no-libstub --model=compact|large|huge.  The runtime was
  already proven by §7t/§7u (the three build-example probes pass libstub-free
  under all three far-data models), and the stevie far-data FILE path by
  dos_file_probe, so this was build-glue + verification, not new runtime work.
  The other §7v track (make --no-libstub the default) is broad re-verification,
  left for §7w; interactive edit/save is keyboard-bound, left for the user.

### The change — tools/build-stevie.sh only (+33/-11)
- Guard (was small|medium-only): now rejects ONLY tiny (.COM-only).
- Runtime block: was two-way (small raw / else=medium near_to_far).  Now the
  SAME three-way as build-example.sh:
    small            — raw near qbe_rt.asm + dos_syscall.asm.
    medium           — qbe_rt + dos_syscall both near_to_far_rt.py (near data).
    compact/large/   — qbe_rt far rewrite + dos_syscall_far_data.asm (ES:BX
    huge               far-ptr int86 family) INSTEAD of near dos_syscall +
                       far_stdlib_bridge.asm (_far_X: jmp far _X name bridges).
- crt0_exe already branched FAR_DATA for compact/large/huge; the newlibc support
  TUs already compile -m $MODEL; heap.asm + --gc-sections unchanged.

### Verification (no new gate entry — §7r pattern, stevie is interactive)
- All 3 far-data models link clean: 0 libstub in the map, 37 far-bridge/far-
  syscall symbols present.  compact/large code 207,821 + data+bss 56,996 B
  (+4 KB stack < 64 KB DGROUP, comfortable — fat/block dropped per §7s); huge
  code 224,714 B.
- Startup-screen byte-compare (the §7r auto-check): stevie renders its screen via
  the newlibc console write path (write(1) → dos_shim → INT 21h AH=40h), which
  run-dos-exe.sh captures to OUT.TXT.  stevie renders the full vi startup
  (`Empty Buffer` + ~ tildes + Victor ^[Y cursor escapes) then blocks on a
  keypress; the idle block trips run-dos-exe's timeout, which dumps the COMPLETE
  rendered screen.  That capture is BYTE-IDENTICAL across {compact,large,huge} ×
  {libstub anchor, libstub-free} and equal to the compact baseline (153 B each).
- make check green; full test-dos 366/366 (UNCHANGED — build-glue, no gate
  entry added; the default-medium-libstub stevie size gate still [ok]).  No
  compiler/qbe/emit/minic touched → no emit audit, no MP byte-compare.

### ⇒ Next session (§7w)
- Make --no-libstub the DEFAULT (retire libstub_to_exe.py python printf): flip
  the default in build-example.sh/build-stevie.sh, re-verify every model/program,
  keep the libstub anchor reachable as the golden cross-check during transition;
  confirm MP (links none of this) + stevie still byte-match.  Broad-but-
  mechanical — sequence so a regression is attributable.
- Lower appetite: deeper interactive stevie verify (keyboard-bound, user); the
  carried compiler tracks (aoa sub-gaps; huge pointer EQUALITY flat-compare —
  §7u's latent sibling, no consumer).
---

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

Older session headers (§7u and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
