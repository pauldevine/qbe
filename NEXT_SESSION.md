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


# Next session (§7z — continue Phase 6 libstub-retirement cleanup / open compiler tracks.  §7y [2026-06-15, this session] **CACHED THE NEWLIBC SUPPORT SET PER MODEL — paid down the §7w gate-build-time regression: `tools/build-example.sh` now compiles the ten libstub-free newlibc support TUs ONCE per memory model into `build/nl-cache/<model>` and reuses the objs across every probe in a run, cutting the full `tools/test-dos.sh` wall from 18m24 (the `NL_CACHE=0` baseline = the pre-§7y per-build recompile) to 8m06 — both GREEN 366/366, 0 FAIL, 0 skip; no compiler/qbe/emit/minic source touched (→ no emit audit; MP provably unaffected — it links NONE of the changed files → no MP byte-compare).**  §7w made libstub-free the default, so every codegen probe now builds the full newlibc portable-stdio stack; before §7y `build-example.sh` recompiled all ten support TUs (clang -E → minic → qbe → asm_to_omf → nasm, ~2 s) on EVERY libstub-free build, even though those objs are byte-identical across all probes of a given model (the support set depends only on (model, its sources+headers, the toolchain) — never on the example).  **THE FIX (`build-example.sh` ONLY, additive):** the support TUs (`printf_wrappers`/`scanf_wrappers`/`syscalls`/`reent_stubs`/`dirent`/`unlink`/`rename` + `dos_vfs`/`dos_shim`/`dos_libc`) compile into a per-model cache dir `build/nl-cache/<model>`; a build links the cached objs when a stamp matches, else rebuilds them and rewrites the stamp.  The stamp is `shasum` over the size+mtime of every dependency — the ten support sources, the `minic/dos/*.asm` runtime, ALL headers under `shiminc`/`$NL/{include,drivers,libgloss,vfs}`/`minic/include`, the `minic`+`qbe` binaries, `asm_to_omf.py`, `near_to_far_rt.py`, and `build-example.sh` itself (the clang flags + `sed` normalizers live in it) — so editing ANY of them invalidates the cache (NO stale-obj trap; cf. the §7q stale-binary illusion).  `NL_CACHE=0` forces the pre-§7y per-`OUT_DIR` recompile (how the 18m24 baseline was measured); `NL_CACHE_DIR` overrides the cache root.  The stamp is written LAST so `set -e` aborts a failed compile before a valid stamp exists, and a hit additionally re-checks every expected obj is present.  **VERIFIED:** cache hit 0.51 s vs 2.68 s miss (≈5× per build), the resulting `.exe` BYTE-IDENTICAL to a `NL_CACHE=0` build; `touch dos_libc.c` → next build MISS then HIT; per-model isolation (small/medium/compact/large/huge each get their own cache); full gate 366/366 from a COLD cache (8m06) and the `NL_CACHE=0` baseline 366/366 (18m24); `make check` green.  Scope is deliberately just the build-example.sh codegen-probe path — the §7w regression that this pays down; `build-newlibc-test.sh`'s own (heavier, sometimes test-varying) support compile is NOT cached here (it predates §7w; a §7z option).  STRATEGY unchanged (ADD, NEVER MUTATE): the `--libstub` anchor path, the tiny/.COM/absent-tree fallbacks, and every other toolchain file (`libstub*.py`/`*.asm`/`dos_*.c`/newlibc) are UNTOUCHED; the cache dir lives under untracked `build/`.  **⇒ Next session (§7z): finish the retirement / remaining cleanup.**  Pick by appetite: **(1)** the `%p`/`%o` printf track to UNPIN `cstrprobe`/`compactprobe_extra`/`huge_norm_probe`/`mediumprobe` — but note (this session's investigation) it requires making newlibc's SHARED `printf_wrappers.c::tiny_vformat` match libstub's `%p` (full 32-bit far ptr, 8 lowercase hex digits, NO `0x`) and add `%o`, which DIVERGES from the upstream newlibc corpus and risks other newlibc `%p` goldens (the messiest track; a local-patch-or-override decision); **(2)** extend the §7y per-model cache to `build-newlibc-test.sh`'s support compile (the newlibc-tree FAT/VFS/block tests — heavier TUs, but the linked support subset varies per test, so the key is per-(model, subset)); **(3)** delete `libstub_to_exe.py`'s python printf engine for non-anchor use (the literal 'retire outright'); **(4)** make `split_stack_probe` pass libstub-free — give `heap.asm`'s `_BSS` the same DGROUP paragraph base as `_DATA` under far-data (an omf_link DGROUP-normalisation track; unpins the last setjmp-family probe).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; no consumer, `_sbrk` only does `== NULL`).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7y session notes (2026-06-15)

### The pick (continued §7x handoff — the gate build-speed track)
- §7x listed four §7y options.  Asked the user (AskUserQuestion) which to take;
  they chose the **build-speed archive** over the %p/%o shim, the
  delete-python-printf track, and the split_stack heap-layout track.  Rationale
  surfaced: the printf shim is the messiest (it must edit newlibc's SHARED
  printf_wrappers.c, diverging from the upstream corpus and risking other %p
  goldens), while the build-speed regression is real, explicitly flagged in §7w,
  and build-script-only (clean + low-risk).

### The cost (measured this session)
- One libstub-free build-example.sh run = 2.68 s; the --libstub anchor build of
  the same probe = 0.63 s.  The ~2 s delta is the ten newlibc support TUs being
  recompiled from scratch on EVERY libstub-free build.  With ~287 codegen probes
  (plus the §7q/§7r/§7s probes × 5 models), that redundant recompile dominated
  the gate BUILD phase: NL_CACHE=0 full gate = 18m24 wall.

### The fix (build-example.sh, additive — see the §7z header for the mechanism)
- Per-model obj cache under build/nl-cache/<model>; content stamp = shasum over
  size+mtime of (support sources, runtime asm, all relevant headers, minic+qbe,
  asm_to_omf.py, near_to_far_rt.py, build-example.sh).  Hit → link cached objs;
  miss → recompile into the cache + rewrite stamp (stamp last, so set -e leaves
  no valid stamp on a failed compile; a hit also re-verifies every obj exists).
- compile_newlibc_unit gained an explicit <out-dir> 3rd arg (was hardcoded
  $OUT_DIR) so the same function writes either the cache or, under NL_CACHE=0,
  the per-probe OUT_DIR (the exact pre-§7y behavior, kept for the baseline).
- Scope: build-example.sh ONLY.  build-newlibc-test.sh's support compile is the
  pre-§7w status quo (not the regression) and is left for §7z.

### Verification
- Cache hit 0.51 s vs 2.68 s miss (~5×); resulting .exe BYTE-IDENTICAL to a
  NL_CACHE=0 build (cmp clean).  touch dos_libc.c → MISS then HIT.  Per-model
  isolation (small + medium each built, separate dirs).
- Full gate from a COLD cache: 366/366 ok, 0 FAIL, 0 skip, 8m06 wall.
- NL_CACHE=0 baseline gate: 366/366 ok, 18m24 wall (the before number).
- make check green.  No compiler/qbe/emit/minic change → no emit audit, MP not
  rebuilt (links none of the changed files → no byte-compare).

### ⇒ Next session (§7z)
- %p/%o printf shim (unpin 4 printf probes — but it edits newlibc's shared
  printf_wrappers.c; messiest) / extend the per-model cache to
  build-newlibc-test.sh / delete libstub python printf for non-anchor / make
  split_stack_probe pass libstub-free (heap.asm _BSS DGROUP-base track).
---

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
