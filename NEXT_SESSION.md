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

# Next session (§7x — continue Phase 6 libstub retirement / open compiler tracks.  §7w [2026-06-15, this session] **MADE `--no-libstub` THE DEFAULT — the end-state of the Phase-6 libstub-retirement campaign: `build-example.sh` and `build-stevie.sh` now build libstub-free by DEFAULT, with `--libstub` the opt-out equivalence anchor; the whole 287-probe codegen `RUNTIME_TESTS` gate, the §7q/§7r/§7s program probes, AND stevie all run libstub-free; `tools/test-dos.sh` GREEN 366/366, no compiler/qbe/emit/minic source touched (→ no emit audit; MP provably unaffected — it links NONE of the changed files and builds via `libstub_to_exe.py` directly, so NO MP byte-compare).**  THE FLIP (`build-example.sh`/`build-stevie.sh`): `NO_LIBSTUB` defaults 1; `--libstub` sets it 0 (anchor), `--no-libstub` still accepted (re-asserts default).  Libstub-free is .EXE-only, needs the newlibc tree, and can't build tiny, so both scripts AUTO-FALL-BACK to libstub for tiny / the stevie .COM path / an absent newlibc tree — but ONLY when libstub-free was the implicit default; an EXPLICIT `--no-libstub` keeps strict (error on tiny, `exit 77` skip when tree absent, the §7n convention).  FILLS (COPY/ADD): `dos_libc.c` grew `strstr`/`memmove`/`fflush`/`ftell`/`bdos` (the ONLY libc gaps across a build-only sweep of every probe×model; `ftell`/`fflush` return 0 matching libstub's stubs, `bdos` is C over `int86` so one def fits all models), `far_stdlib_bridge.asm` grew `strstr`/`memmove`/`fflush` bridges.  PINS to `--libstub` (`build_runtime_probe` case, EMPIRICAL build+diff, not static grep): (1) printf divergence — newlibc's `tiny_vformat` formats `%p` as `0x`-prefixed+16-bit-truncated and lacks `%o` → `cstrprobe`/`compactprobe_extra`/`huge_norm_probe`/`mediumprobe`; (2) `setjmp`/`longjmp` have NO libstub-free impl yet (far `jmp_buf` + asm save/restore, DEFERRED) → `setjmp_probe`/`setjmp_clobber_probe`/`arr_jmpbuf_probe`/`aoa_extended_probe`/`split_stack_probe`.  The §7q/§7r/§7s probe pairs were rewired (anchor → `--libstub`, default → libstub-free); `run_stevie_size` pinned `--libstub` (libstub size-regression anchor, libstub-calibrated budget).  VERIFIED: gate 366/366; stevie startup screen BYTE-IDENTICAL (152 B) libstub-free-default vs `--libstub` anchor (§7r/§7v rendered-screen byte-compare); `make check` green; all fallbacks correct.  STRATEGY unchanged (COPY/ADD): `libstub.asm`/`libstub_to_exe.py`/`near_to_far_rt.py`/`qbe_rt.asm`/`dos_syscall*.asm`/`heap.asm`/`dos_vfs.c`/newlibc UNTOUCHED, build scripts additive flag+fallback (default libstub path byte-unchanged).  COST: every codegen probe now compiles the full newlibc stack libstub-free → the gate BUILD phase is markedly slower (a shared precompiled newlibc-support archive would recover it).  **⇒ Next session (§7x): finish the retirement / pay down the §7w costs.**  Pick by appetite: **(1)** give the libstub-free runtime its own `setjmp`/`longjmp` (a `setjmp_rt.asm` near/far/far-data, porting `libstub_to_exe.py`'s `NEAR_SETJMP_EXE`/`SETJMP_EXE`) → UNPIN the 5 setjmp probes; **(2)** a tiny `%p`/`%o` shim (or accept newlibc's `%p` + regenerate those goldens against BOTH engines) → UNPIN the 4 printf probes; **(3)** the gate build-speed optimization (precompile the newlibc support set once, link the archive) — addresses the real §7w regression; **(4)** delete `libstub_to_exe.py`'s python printf for non-anchor use (the literal 'retire outright').  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first = grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §7w session notes (2026-06-15)

### The pick (continued §7v handoff — the headline §7w track)
- §7v listed §7w's headline as "make --no-libstub the DEFAULT".  The user
  (AskUserQuestion) chose "Full flip + re-verify all" over the narrower
  options after I surfaced the blast radius: 287 RUNTIME_TESTS codegen probes
  build through build-example.sh's default path, all with libstub-captured
  goldens, gaining a hard newlibc-tree dependency and no tiny support.

### De-risk FIRST (before any change)
- Built a representative spread + then ALL 287 probe×model combos libstub-free
  via the existing --no-libstub flag (build-only sweep, no DOSBox).  Result:
  the ONLY undefined symbols were strstr + bdos (cmp's _main was a stale Jun-5
  leftover dir, deleted).  Most probes matched their goldens byte-identical.

### The flip — build-example.sh + build-stevie.sh
- NO_LIBSTUB defaults 1; --libstub opt-out (LIBSTUB_EXPLICIT tracks whether the
  user forced a choice).  --no-libstub still accepted (re-asserts default).
- Auto-fallback to libstub: tiny / stevie .COM path / absent newlibc tree, but
  ONLY for the implicit default.  Explicit --no-libstub keeps strict: error on
  tiny, exit 77 (gate skip) when tree absent.  The fallback makes the secondary
  callers (run-emit-audit.sh, test-victor.sh) robust without pinning.

### dos_libc.c + far_stdlib_bridge.asm fills (COPY/ADD)
- dos_libc.c: +strstr, +memmove, +fflush(return 0), +ftell(return 0L), +bdos
  (C over int86 — one def, right ABI in every model; ftell/fflush match
  libstub's xor-ax/dx stubs; ftell_null_probe needs the full 32-bit zero).
- far_stdlib_bridge.asm: +strstr, +memmove, +fflush (the far_stdlib-mangled
  ones; ftell/bdos are NOT in minic far_stdlib[] -> plain name, no bridge).

### Divergence sweep -> pins (EMPIRICAL, not static grep)
- newlibc tiny_vformat: %p = "0x"+16-bit-trunc hex, NO %o (echoes literal), NO
  %f (float probes print raw bits via %lx, so they're fine).
- Static grep OVER-predicts: phase_bprime_probe/farlocal_probe use %p/%o but
  their values MATCH -> NOT pinned.  Pin set is the build+diff result.
- PIN --libstub (build_runtime_probe): cstrprobe, compactprobe_extra,
  huge_norm_probe, mediumprobe (printf); setjmp_probe, setjmp_clobber_probe,
  arr_jmpbuf_probe, aoa_extended_probe, split_stack_probe (setjmp/longjmp).

### Gate wiring
- §7q/§7r/§7s loops: "$model runtime" -> "$model libstub anchor" (--libstub) +
  "$model libstub-free" (default, no flag); both snapshot the same exe (cp at
  stage time) and diff the same golden.
- run_stevie_size pinned --libstub (libstub size-regression anchor).

### First full gate: 22 FAIL (all BUILD-time undefined symbols, not diffs)
- memmove (mp_str_int_probe=1), setjmp/longjmp (15), fflush (stdio_far_probe=3),
  ftell (ftell_null_probe=3) = 22.  My earlier sweep was killed before reaching
  these (its log mis-classified link errors anyway — omf_link's error goes to
  build.err not stdout).  Aggregating build.err gave the true symbol set.
- After the fills + pins: re-run GREEN 366/366.

### Verification
- Full gate 366/366 (0 fail, 0 skip).  make check green.
- stevie: built --exe --libstub (146,144 B anchor) and --exe default
  (152,016 B, 0 libstub refs, newlibc syms present); rendered startup screen
  (Empty Buffer + ~ tildes + Victor ^[Y escapes) BYTE-IDENTICAL 152 B both ways.
  (run-dos-exe surfaces a blocking program's screen on stderr at timeout.)
- Fallbacks: tiny->libstub .EXE; no-arg stevie->.COM libstub (87,273 B);
  newlibc-absent default->libstub; explicit --no-libstub+absent->exit 77.
- MP NOT rebuilt (provably unaffected — links none of the changed files).
- No compiler/qbe/emit/minic change -> no emit audit, no MP byte-compare.

### ⇒ Next session (§7x)
- setjmp_rt.asm (unpin 5) / %p,%o shim (unpin 4) / gate build-speed (precompiled
  newlibc-support archive — the real §7w cost) / delete libstub python printf.

---

Older session headers (§7v and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
