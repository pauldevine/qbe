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

# Next session (§7y — continue Phase 6 libstub retirement / open compiler tracks.  §7x [2026-06-15, this session] **GAVE THE LIBSTUB-FREE RUNTIME ITS OWN `setjmp`/`longjmp` — the new `minic/dos/setjmp_rt.asm` (all three ABI forms) supplies the setjmp surface the `--no-libstub` runtime previously lacked, so FOUR of the five §7w-pinned setjmp probes are now UNPINNED and run libstub-free byte-identical to their existing goldens; `tools/test-dos.sh` GREEN 366/366, no compiler/qbe/emit/minic source touched (→ no emit audit; MP provably unaffected — it links NONE of the changed files, so NO MP byte-compare).**  §7w had pinned five probes to `--libstub` for want of a setjmp impl: `setjmp_probe`, `setjmp_clobber_probe`, `arr_jmpbuf_probe`, `aoa_extended_probe`, `split_stack_probe`.  **The new TU (`minic/dos/setjmp_rt.asm`):** a standalone object with three `%ifdef`-selected ABI forms, each COPIED VERBATIM from `tools/libstub_to_exe.py` — the near form from `NEAR_SETJMP_EXE` (small: near code, near data, `_setjmp`/`_longjmp`, env at `[bp+4]`, near `ret`, 6-word jmp_buf), the medium form from `SETJMP_EXE` (`-dSJ_FAR_CODE`: far code / near data, `_setjmp`/`_longjmp`, env near-ptr at `[bp+6]` via DS:BX, `retf`, 7-word jmp_buf carrying the ret CS), and the far-data form from `FAR_SETJMP_EXE` (`-dSJ_FAR_DATA`: compact/large/huge, minic mangles setjmp/longjmp → `_far_setjmp`/`_far_longjmp` via `far_stdlib[]`, env a 4-byte far ptr off `[bp+6]`/seg `[bp+8]` via ES:BX, `retf`).  The two far forms live in a UNIQUE far-code segment `SETJMP_RT_TEXT` so omf_link's `call far` fixup resolves, mirroring `near_to_far_rt.py`'s `QBE_RT_TEXT`.  **Deliberately NOT routed through `near_to_far_rt.py`** — that mechanical `[bp+N]+2` shift would corrupt the jmp_buf INTERNAL `[bx+10]`/`[bx+12]` ret-IP/CS offsets (the same reason `libstub_to_exe.py` hand-writes `NEAR_SETJMP_EXE` rather than reverse-transforming `SETJMP_EXE`); each form is authored explicitly.  **Wiring (COPY/ADD):** `build-example.sh` + `build-stevie.sh` each link `setjmp_rt.obj` in all three runtime branches with the matching `-d` flag; `--gc-sections` drops it when unused.  stevie calls neither setjmp nor longjmp, so it is gc-stripped there — VERIFIED stevie startup screen still BYTE-IDENTICAL (152 B) libstub-free-default vs `--libstub` anchor (the §7r/§7v rendered-screen byte-compare).  **Gate:** the four probes that now pass libstub-free were UNPINNED in `build_runtime_probe`'s `libstubflag` case; verified byte-identical to goldens at every gated model (setjmp_probe small/medium/compact/large + huge; setjmp_clobber small/medium/compact; arr_jmpbuf medium/compact/large; aoa_extended medium/compact/large).  **`split_stack_probe` STAYS pinned `--libstub`, but for an UNRELATED reason** (re-documented in the pin comment): its `ok7` asserts `heapseg == dgroupseg` (malloc memory lands in the SAME far segment as a global), which holds for libstub's single-base DGROUP heap but NOT for the libstub-free BSS heap — `heap.asm`'s `_BSS` gets its own DGROUP paragraph (0x070D) distinct from `_DATA` globals (0x06F2) under far-data + `--split-stack`, so a heap pointer's normalised far segment differs from a global's.  That is a LAYOUT difference, not a correctness bug — and setjmp itself (`ok6`) passes libstub-free in split_stack_probe, confirming the impl is correct even under split-stack far-data.  **VERIFIED:** gate 366/366 (0 fail, 0 skip); `make check` green; setjmp symbols resolve from `setjmp_rt.obj`/`SETJMP_RT_TEXT` (bug-loud — without the TU the link fails undefined).  STRATEGY unchanged (COPY/ADD, NEVER MUTATE): `setjmp_rt.asm` is all-new; `libstub_to_exe.py`/`libstub.asm`/`near_to_far_rt.py`/`qbe_rt.asm`/`dos_syscall*.asm`/`far_stdlib_bridge.asm`/`heap.asm`/`dos_vfs.c`/newlibc all UNTOUCHED; the build scripts gained additive link steps and the gate an additive unpin.  **⇒ Next session (§7y): finish the retirement / pay down the §7w costs.**  Pick by appetite: **(1)** a tiny `%p`/`%o` shim for newlibc's `tiny_vformat` (or accept its `%p` + regenerate those goldens against both engines) → UNPIN the 4 printf probes (`cstrprobe`, `compactprobe_extra`, `huge_norm_probe`, `mediumprobe`); **(2)** the gate build-speed optimization — precompile the newlibc support set once and link the archive, addressing the real §7w regression (every codegen probe now recompiles the full newlibc stack libstub-free, so the gate BUILD phase is markedly slower — observed this session); **(3)** delete `libstub_to_exe.py`'s python printf for non-anchor use (the literal 'retire outright'), now that setjmp is the last asm-runtime gap closed; **(4)** make `split_stack_probe` pass libstub-free — give `heap.asm`'s `_BSS` heap the same DGROUP paragraph base as `_DATA` globals under far-data (a heap-layout / omf_link DGROUP-normalisation track; unpins the last setjmp-family probe but is deeper than build-glue).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7x session notes (2026-06-15)

### The pick (continued §7w handoff — give the libstub-free runtime setjmp/longjmp)
- §7w listed four §7y... §7x tracks; the user prompt was "continue", so I took
  the first-listed and most bounded "finish the retirement" one: provide the
  libstub-free runtime its own setjmp/longjmp, unpinning the five §7w setjmp
  probes.  The %p/%o shim, the precompiled-archive build-speed fix, and the
  delete-python-printf track are left for §7y.

### The new TU — minic/dos/setjmp_rt.asm (COPY/ADD)
- One standalone object, three %ifdef-selected ABI forms, each VERBATIM from
  libstub_to_exe.py: NEAR_SETJMP_EXE (default → near/small), SETJMP_EXE
  (-dSJ_FAR_CODE → medium, far code / near data), FAR_SETJMP_EXE (-dSJ_FAR_DATA
  → compact/large/huge).  Far forms in a unique SETJMP_RT_TEXT far-code segment
  (mirrors near_to_far_rt.py's QBE_RT_TEXT) so `call far _setjmp`/`_far_setjmp`
  resolves.  Cross-reference comment: a future fix to the save/restore or
  jmp_buf-offset logic MUST land in BOTH this file and libstub_to_exe.py.
- NOT routed through near_to_far_rt.py: its [bp+N]+2 shift would corrupt the
  jmp_buf INTERNAL [bx+10]/[bx+12] ret-IP/CS offsets (same reason
  libstub_to_exe.py hand-writes the near form).  So each ABI form is explicit.

### Wiring (build-example.sh + build-stevie.sh, additive)
- Each links setjmp_rt.obj in all three runtime branches with the matching -d
  flag.  --gc-sections drops it when unused.
- stevie calls neither setjmp nor longjmp → gc-stripped there; VERIFIED startup
  screen still BYTE-IDENTICAL 152 B (libstub-free default vs --libstub anchor,
  the §7r/§7v rendered-screen byte-compare).  Linked only so a future stevie TU
  that uses setjmp links cleanly.

### Gate — unpin 4, keep split_stack_probe pinned
- Removed setjmp_probe/setjmp_clobber_probe/arr_jmpbuf_probe/aoa_extended_probe
  from build_runtime_probe's libstubflag case → they take the libstub-free
  default.  Verified byte-identical to goldens at every gated model (incl. an
  extra huge check of setjmp_probe), symbols resolving from setjmp_rt.obj.
- split_stack_probe STAYS pinned, NOT for setjmp (its ok6 setjmp step passes
  libstub-free) but for ok7: `heapseg == dgroupseg`.  Under libstub-free the
  BSS heap (heap.asm _BSS, para 0x070D) gets a different DGROUP paragraph than
  _DATA globals (0x06F2) under far-data + --split-stack, so a heap pointer's
  far segment differs from a global's — a LAYOUT difference, not a bug.  The
  §7w pin comment was rewritten to say so.  Making it pass libstub-free is a
  heap-layout / omf_link DGROUP-base track (a §7y option).

### Verification
- Full gate 366/366 (0 fail, 0 skip).  make check green.  Setjmp-family
  verdicts all [ok]: setjmp_probe ×4, arr_jmpbuf ×3, aoa_extended ×3,
  setjmp_clobber ×3, split_stack ×2 (pinned).
- Bug-loud: _setjmp/_far_setjmp resolve from setjmp_rt.obj (SETJMP_RT_TEXT);
  without the TU the link fails undefined (the reason §7w pinned them).
- GATE ARCHITECTURE NOTE (cost ~0, but worth recording): test-dos.sh builds +
  stages ALL ~346 runtime probes first ([built] lines, no DOSBox), then runs
  the whole set in ONE DOSBox boot at the end (flush_runtime_batch /
  run-dos-batch.sh) — only that run phase prints [ok]/[FAIL] and counts toward
  pass/fail.  So a long DOSBox-less build stretch then one batch boot is NORMAL,
  not a stall; the §7w libstub-free-everywhere default makes that build stretch
  markedly longer (the §7y precompiled-archive track).
- No compiler/qbe/emit/minic change → no emit audit, no MP byte-compare.

### ⇒ Next session (§7y)
- %p/%o printf shim (unpin the 4 printf probes) / precompiled newlibc-support
  archive (the real §7w build-speed cost) / delete libstub python printf for
  non-anchor / make split_stack_probe pass libstub-free (heap DGROUP-base track).
---

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
