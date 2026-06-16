# Next session (§8b — finish Phase‑6 libstub-retirement cleanup / open compiler tracks.  §8a [2026-06-15, this session] **UNPINNED `split_stack_probe` FROM `--libstub` — the LAST setjmp-family pin (the §7w/§7x carry) is closed: the probe now runs libstub-free byte-identical to its golden under compact AND large, by teaching `tools/omf_link.py` to resolve EVERY far reference to a DGROUP member against the canonical DGROUP group frame; `tools/test-dos.sh` GREEN 366/366 (unchanged count — no new probe, just an unpin), `make check` green, MicroPython compact BYTE-IDENTICAL (no Victor run), stevie startup screen byte-identical (152 B).**  This is an `omf_link.py` (toolchain) change, NOT an `i8086/emit.c` change → no emit audit; the toolchain-change MP byte-compare was MANDATORY and came back identical.  **The bug:** `split_stack_probe`'s `ok7` asserts `heapseg == dgroupseg` (malloc memory lands in the SAME far segment as a global).  Under a far-data model + `--split-stack`, omf_link laid the libstub-free BSS heap (`heap.asm`'s `_BSS`) ABOVE `_DATA` inside DGROUP (`_DATA` para `0x06F2`, `_BSS` para `0x070D`), and resolved a far reference's SELECTOR to a DGROUP member against that member's OWN paragraph base — so `seg ___heap_start` (in `_BSS`) yielded `0x070D` while a global's `seg _g` (in `_DATA`, the LOWEST member = the group base) yielded `0x06F2`.  Two different segment words for the same group → `heapseg != dgroupseg`.  (The heap was still usable: the OFFSET `mov ax, ___heap_start` was ALREADY group-framed = `0x298` while the SELECTOR was `_BSS`-framed = `0x070D`, so the heap pointer was self-consistent-but-non-canonical `0x070D:0x298` — a valid address 432 B into the heap region, not exactly `___heap_start`.)  libstub's single-base DGROUP heap never exposed this, which is why the probe stayed pinned `--libstub` through §7x.  **The fix (`omf_link.py` `_frame_para`, ~3 edits):** a far reference to a DGROUP member now resolves its frame to the canonical GROUP base (the min member paragraph) for EVERY frame method (0 = segment, 2 = external, 5 = target-frame) and EVERY location — previously method 5 group-framed only the 16-bit OFFSET (loc 1/5), not the SELECTOR (loc 2) or the 32-bit far ptr (loc 3); the loc-3 far-ptr OFFSET was also generalized to be frame-relative (`tgt_abs_byte − frame_byte`, identical for the common frame==target-seg case).  This is a NO-OP for the lowest DGROUP member (`_DATA`, already the group base, so a global's selector was always canonical) and for ungrouped CODE / FAR_* segments → it only normalizes non-lowest members like `_BSS`.  Offset and selector stay mutually consistent because both derive from the one `_frame_para` result.  The heap reference is nasm target-frame (method 5), NOT external/segment (method 0/2) — confirmed via a throwaway `OMF_DEBUG_HEAP` instrumentation; the method 0/2 edits are kept for rule-completeness (correct OMF group semantics) though no current consumer exercises them, proven harmless by MP byte-identity.  **VERIFICATION:** built MP compact once, then re-linked the SAME objects with the OLD (git-stashed) vs the NEW omf_link and `cmp`'d → BYTE-IDENTICAL (image 751,664, body 731,088), so MP has zero far references to a non-lowest DGROUP member and needs no Victor run; stevie rendered startup screen byte-identical 152 B (libstub-free vs `--libstub` anchor, the §7r/§7v pattern, stripping the run-dos-exe banner line); the `split_stack_probe` `--libstub` anchor still matches its golden (the change is benign for libstub's heap too); full gate 366/366.  **Gate change:** removed `split_stack_probe` from `build_runtime_probe`'s `libstubflag` pin (so it takes the libstub-free default like the §7x setjmp family); golden UNTOUCHED (it matched both engines already).  STRATEGY: the COPY/ADD-NEVER-MUTATE runtime toolchain (`heap.asm`, `qbe_rt.asm`, `dos_syscall*.asm`, `far_stdlib_bridge.asm`, `setjmp_rt.asm`, `dos_vfs.c`, `libstub.asm`, `libstub_to_exe.py`, `near_to_far_rt.py`, newlibc) is UNTOUCHED; the only source change is the `omf_link.py` `_frame_para` group-framing rule (correctness-preserving for every existing pointer) + the one-line gate unpin.  **⇒ Next session (§8b): finish the retirement / remaining cleanup.**  Pick by appetite: **(1)** the `%p`/`%o` printf track to UNPIN the four remaining `--libstub` pins (`cstrprobe`, `compactprobe_extra`, `huge_norm_probe`, `mediumprobe`) — requires making newlibc's SHARED `printf_wrappers.c::tiny_vformat` match libstub's `%p` (full 32-bit far ptr, 8 lowercase hex, no `0x`) and add `%o`, which DIVERGES from the upstream newlibc corpus and risks other `%p` goldens (the messiest track; a local-patch-or-override decision); **(2)** delete `libstub_to_exe.py`'s python printf engine for non-anchor use (the literal 'retire outright'), now that the asm-runtime gaps — setjmp (§7x) and the DGROUP heap layout (§8a) — are all closed and the only remaining libstub dependency is the four printf-format pins.  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; no consumer, `_sbrk` only does `== NULL`).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8a session notes (2026-06-15)

### The pick (continued §7z handoff — the split_stack heap-layout track)
- §7z listed four §8a options; the user (AskUserQuestion) chose **make
  split_stack_probe pass libstub-free** (the heap.asm _BSS DGROUP-base track)
  over the %p/%o printf shim (messiest — edits newlibc's shared
  printf_wrappers.c, diverges from the upstream corpus), the
  delete-python-printf track, and the carried compiler tracks.  Rationale:
  self-contained, toolchain-only, lowest-risk, and it closes the LAST
  setjmp-family pin carried since §7w/§7x.

### The bug (ok7 0 libstub-free; everything else passed)
- ok7 asserts heapseg == dgroupseg: malloc memory in the SAME far segment as a
  global.  The map showed _DATA at para 0x06F2 (the lowest DGROUP member = the
  group base) and the libstub-free BSS heap (heap.asm _BSS) at 0x070D, above it.
- omf_link resolved a far reference's SELECTOR to a DGROUP member against the
  member's OWN para_base, so `seg ___heap_start` (in _BSS) = 0x070D while a
  global's `seg _g` (in _DATA) = 0x06F2 → different segment words, same group.
- Subtle: the heap OFFSET (mov ax, ___heap_start) was ALREADY group-framed
  (0x298) but the SELECTOR was not (0x070D), so the live heap pointer was a
  self-consistent-but-non-canonical 0x070D:0x298 — valid (432 B into the heap
  region), so hp[0]=='Z' passed; only the segment compare failed.
- libstub's single-base DGROUP heap never exposed this → the §7x pin.

### The fix (tools/omf_link.py _frame_para, ~3 edits)
- A far reference to a DGROUP member now resolves to the canonical GROUP base
  (min member para) for EVERY frame method (0 segment, 2 external, 5
  target-frame) AND every location.  Method 5 previously group-framed only the
  16-bit offset (loc 1/5), not the selector (loc 2) or far ptr (loc 3).
- The loc-3 far-ptr OFFSET was generalized to be frame-relative
  (tgt_abs_byte - frame_byte) — identical to the old tgt_byte_in_out whenever
  the frame is the target segment, so the common case is byte-unchanged.
- No-op for the lowest member (_DATA, already the group base) and for ungrouped
  CODE / FAR_* segments → only non-lowest members (_BSS) are normalized.  Offset
  and selector stay consistent because both come from the one _frame_para call.
- The heap reference is nasm method 5 (target-frame), NOT 0/2 — confirmed with a
  throwaway OMF_DEBUG_HEAP print.  The method 0/2 edits are kept for
  rule-completeness (correct OMF group semantics) though no current consumer
  hits them; harmless per MP byte-identity.

### Verification (toolchain change → MP byte-compare MANDATORY; not emit.c → no emit audit)
- split_stack_probe libstub-free: ok7 1, byte-identical to golden, compact+large.
- split_stack_probe --libstub anchor: still matches golden (change benign for
  libstub's single-base heap too).
- MP compact: built once, re-linked the SAME objs with old (git-stashed) vs new
  omf_link → cmp BYTE-IDENTICAL (image 751,664, body 731,088).  ⇒ MP has zero far
  refs to a non-lowest DGROUP member; no Victor run.
- stevie: rendered startup screen byte-identical 152 B (libstub-free vs --libstub
  anchor, §7r/§7v pattern — capture via run-dos-exe at timeout on STDERR, strip
  the first banner line that embeds the exe name).
- make check green; full gate 366/366 (count unchanged — unpin, not a new probe).

### Gate change
- Removed split_stack_probe from build_runtime_probe's libstubflag pin (it takes
  the libstub-free default now, like the §7x setjmp family).  Golden UNTOUCHED
  (it already matched both engines).

### ⇒ Next session (§8b)
- %p/%o printf shim (unpin cstrprobe/compactprobe_extra/huge_norm_probe/
  mediumprobe — edits newlibc's SHARED printf_wrappers.c; messiest) /
  delete libstub_to_exe.py's python printf for non-anchor use (the asm-runtime
  gaps — setjmp §7x, DGROUP heap §8a — are now all closed; the four printf
  pins are the only remaining libstub dependency).
- Carried compiler tracks (await a consumer): aoa sub-gaps; huge pointer
  EQUALITY flat-compare.
---


# Next session (§8a — finish Phase‑6 libstub-retirement cleanup / open compiler tracks.  §7z [2026-06-15, this session] **EXTENDED THE §7y PER-MODEL SUPPORT-OBJ CACHE TO `tools/build-newlibc-test.sh` — the newlibc-test build path now compiles each portable-subset support TU ONCE per (model, libstub-mode) into `build/nl-test-cache/<model>[-nolibstub]` and reuses the objs across every test in a `tools/test-dos.sh` run, cutting the full gate wall from the §7y 8m06 to 6m33 — both GREEN 366/366, 0 FAIL, 0 skip; no compiler/qbe/emit/minic source touched (→ no emit audit; MP provably unaffected — it links NONE of the changed files → no MP byte-compare).**  §7y had cached the libstub-free support set for `build-example.sh` (the codegen-probe path); §7z does the same for `build-newlibc-test.sh` (the newlibc FAT/VFS/block test path), the other support-recompile hot spot §7y explicitly left for later.  Before §7z each of the 13+ small `NEWLIBC_TESTS` × {libstub, `--no-libstub`} plus the medium `fat_write` pair recompiled its WHOLE support subset (clang‑E → minic → qbe → asm_to_omf → nasm, ~2‑s/TU) even though those objs are byte-identical across every test of a given (model, libstub) pair.  **THE FIX (`build-newlibc-test.sh` ONLY, additive):** `compile_unit` gained an explicit `<out-dir>` 3rd arg (was hardcoded `$OUT_DIR`) so it can write either the per-test `OUT_DIR` (the test TU — the one TU that differs per build — and the `NL_CACHE=0` path) or the shared cache.  **Unlike §7y's FIXED 10-TU set, the support SUBSET varies per test** (fat_write tests add `fat_write`/`mkdir`/`rmdir`/`rename`; `--no-libstub` adds `dos_libc`), so the cache is **per-TU with GAP-FILL**: a matching stamp means every obj already in the cache was built from the current inputs, and the loop then compiles only the TUs THIS test needs that aren't present yet (a fat_write test gap-fills its 4 extra TUs into a cache a plain test seeded).  The stamp hashes a **FIXED SUPERSET** of every possible support source (test-independent — the base 10 + the fat_write group + dos_libc) plus the runtime asm, the headers under `shiminc`/`$NL/{include,drivers,libgloss,vfs}`/`minic/include`, the `minic`+`qbe` binaries, `asm_to_omf.py`, `near_to_far_rt.py`, and the script itself — so a fat_write test and a plain test compute the SAME stamp and SHARE one cache (a per-test `SUPPORT_TUS` hash would make them perpetually invalidate each other).  **Separate cache dirs per libstub mode** because `NL_DEFS` (`-DNO_LIBSTUB` vs empty) changes the objs (`dos_shim.c` is `#ifndef NO_LIBSTUB`-guarded).  On a stamp mismatch the whole `<key>` dir is `rm -rf`'d (every obj keyed to stale inputs) then rebuilt; the stamp is written LAST so `set -e` leaves no valid stamp if a compile fails mid-rebuild (idempotent on a hit).  `NL_CACHE=0` forces the pre-§7z per-`OUT_DIR` recompile; `NL_TEST_CACHE_DIR` overrides the cache root (distinct default from §7y's `build/nl-cache` so the differently-compiled `dos_shim.obj` can't collide).  **VERIFIED:** cache hit 0.72‑s vs cold 3.6‑s (~5× per build); resulting `.exe` BYTE-IDENTICAL to an `NL_CACHE=0` build for both small libstub `snprintf_test` AND medium `fat_write_test`; `touch dos_shim.c` → next build MISS then HIT; four cache keys built independently (small 10 / small-nolibstub 11 / medium 14 / medium-nolibstub 15 objs), `dos_libc.obj` ONLY in the `-nolibstub` keys, fat_write group gap-filled into `medium` without rebuilding the existing 10; full gate 366/366 from a COLD cache in 6m33 (vs §7y's 8m06), `make check` green.  STRATEGY unchanged (ADD, NEVER MUTATE): the libstub `--no-stdio` path, the per-test TU compile, and every toolchain file (`libstub*.py`/`*.asm`/`dos_*.c`/newlibc) are UNTOUCHED; the cache lives under untracked `build/`.  Both support-recompile hot spots (§7y build-example, §7z build-newlibc-test) are now cached — the §7w gate-build-time regression is fully paid down.  **⇒ Next session (§8a): finish the retirement / remaining cleanup.**  Pick by appetite: **(1)** the `%p`/`%o` printf track to UNPIN `cstrprobe`/`compactprobe_extra`/`huge_norm_probe`/`mediumprobe` — requires making newlibc's SHARED `printf_wrappers.c::tiny_vformat` match libstub's `%p` (full 32-bit far ptr, 8 lowercase hex, no `0x`) + add `%o`, which DIVERGES from the upstream newlibc corpus and risks other `%p` goldens (the messiest track; a local-patch-or-override decision); **(2)** delete `libstub_to_exe.py`'s python printf engine for non-anchor use (the literal 'retire outright'); **(3)** make `split_stack_probe` pass libstub-free — give `heap.asm`'s `_BSS` the same DGROUP paragraph base as `_DATA` under far-data (an omf_link DGROUP-normalisation track; unpins the last setjmp-family probe).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; no consumer, `_sbrk` only does `== NULL`).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7z session notes (2026-06-15)

### The pick (continued §7y handoff — track 2 of four)
- §7y listed four §7z options; asked the user (AskUserQuestion) which to take.
  They chose **extend the §7y cache to build-newlibc-test.sh** over the %p/%o
  printf shim (messiest — edits newlibc's shared printf_wrappers.c, diverges
  from the upstream corpus), the delete-python-printf track, and the
  split_stack heap-layout track.  Rationale: build-script-only, low-risk, and
  it pays down the *other* half of the §7w gate-build-time regression that §7y
  explicitly left for §7z.

### The cost (the §7w regression's second hot spot)
- §7y cached build-example.sh's codegen-probe support compile.  But
  build-newlibc-test.sh recompiles its OWN portable-subset support set
  (printf/scanf wrappers, syscalls, reent_stubs, dirent, unlink, vfs, fat,
  block, dos_shim [+ fat_write/mkdir/rmdir/rename for write tests, + dos_libc
  under --no-libstub]) on EVERY test build.  The gate builds the 13 small
  NEWLIBC_TESTS twice (libstub + --no-libstub) + malloc_probe + the medium
  fat_write pair both ways — ~30 builds, each redundantly recompiling ~10-15
  identical support TUs.

### The fix (build-newlibc-test.sh, additive — see the §8a header for the mechanism)
- compile_unit gained an explicit <out-dir> 3rd arg (was hardcoded $OUT_DIR).
- Per-(model, libstub-mode) cache under build/nl-test-cache/<key>; key =
  $MODEL or $MODEL-nolibstub (NL_DEFS changes the objs, dos_shim.c is
  #ifndef NO_LIBSTUB-guarded).
- KEY DIFFERENCE from §7y's fixed-set cache: the support SUBSET varies per
  test, so the cache is PER-TU with GAP-FILL — a fat_write test adds its 4
  extra TUs into the same cache a plain test seeded.  The stamp therefore
  hashes a FIXED SUPERSET of all possible support sources (test-independent),
  not the per-test SUPPORT_TUS, or fat_write and plain tests would keep
  invalidating each other's shared cache.
- Stamp = shasum over size+mtime of (the support superset, the runtime asm,
  all relevant headers, minic+qbe, asm_to_omf.py, near_to_far_rt.py, this
  script).  Mismatch → rm -rf the whole key dir + rebuild; stamp written LAST
  (set -e leaves no valid stamp on a failed mid-rebuild).  NL_CACHE=0 forces
  the pre-§7z per-OUT_DIR recompile; NL_TEST_CACHE_DIR overrides the root
  (distinct default from §7y's build/nl-cache).
- Scope: build-newlibc-test.sh ONLY.  The test TU itself stays per-OUT_DIR
  (it differs per build).  The libstub --no-stdio path is untouched.

### Verification
- Cache hit 0.72 s vs cold 3.6 s (~5×).  .exe BYTE-IDENTICAL to NL_CACHE=0 for
  small libstub snprintf_test AND medium fat_write_test (cmp clean).
- touch dos_shim.c → next build MISS (stamp changed) then HIT.
- Four cache keys built independently: small 10 / small-nolibstub 11 /
  medium 14 / medium-nolibstub 15 objs.  dos_libc.obj only in -nolibstub keys;
  fat_write group gap-filled into medium without rebuilding the base 10.
- Full gate from a COLD cache: 366/366 ok, 0 FAIL, 0 skip, 6m33 wall (vs §7y's
  8m06 — both support hot spots now cached).  make check green.
- No compiler/qbe/emit/minic change → no emit audit; MP not rebuilt (links
  none of the changed files → no byte-compare).

### ⇒ Next session (§8a)
- %p/%o printf shim (unpin 4 printf probes — but it edits newlibc's shared
  printf_wrappers.c; messiest) / delete libstub python printf for non-anchor /
  make split_stack_probe pass libstub-free (heap.asm _BSS DGROUP-base track).
---

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
