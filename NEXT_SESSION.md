# Next session (carried compiler tracks — the Phase-6 libstub-retirement campaign is now COMPLETE end-to-end (every gate probe AND every shipped program: stevie §7r–§7v, then MicroPython §8c–§8d).  §8d [2026-06-16, this session] **FLIPPED MicroPython's default to libstub-free and REBASELINED the regression corpus — `tools/build-micropython.sh` (no flag) now builds the libstub-FREE MP (compact default-heap body 689,760, was 731,088 under libstub); `--libstub` is the opt-out equivalence anchor (verified still body 731,088); `make check` green, no compiler/qbe/emit/minic source touched (→ no emit audit).**  §8c had landed libstub-free MP as an OPT-IN `--no-libstub` path (byte-exact vs host `python3` in DOSBox AND on MAME victor9k); §8d is the small end-state flip the user requested.  **The change is one line + doc rebaseline:** `build-micropython.sh` `NO_LIBSTUB=0`→`1` (default), `--libstub` now the opt-out anchor.  The produced `--model=compact` binary is BYTE-IDENTICAL to §8c's already-Victor-verified `--no-libstub` build (same code path; the flag only chooses which path is default), so NO new Victor run was needed.  **VERIFIED:** default compact = image 710,352 / body 689,760 (reproducible, matches §8c); `--libstub` compact = image 751,664 / body 731,088 (anchor intact); the default small-heap build (571,600 B) runs the §8c smoke test (language + FLOAT + `math` sqrt/pi/sin + recursion + ZeroDivisionError) BYTE-EXACT vs host `python3` in DOSBox; `make check` green.  **Rebaseline scope:** the 731,088 baseline was DOCUMENTATION-ONLY — `git grep 731088` over `tools/`/`*.sh`/`*.py` is EMPTY (no hardcoded assertion), and `recompile-mp-tu.sh` is data-driven from `/tmp/mp_objs.txt` (written by `build-micropython.sh`), so it adapts automatically.  Updated the FORWARD-looking refs only: `CLAUDE.md` (regression-corpus line + the "After ANY toolchain change" house rule + a §8d status clause + Last-Updated), `ROADMAP.md` (the two "Body 731,088 B" lines), and `build-micropython.sh`'s banner/arg comments.  HISTORICAL "MP body 731,088 byte-identical" session records (§4t…§8a) are LEFT verbatim — accurate point-in-time records of the libstub corpus.  STRATEGY (ADD/FLIP, NEVER MUTATE the runtime): `dos_shim.c`/`libstub.asm`/`libstub_to_exe.py`/`near_to_far_rt.py`/`builtins_rt.asm`/every runtime asm + newlibc are UNTOUCHED; only the default flag + docs changed.  `libstub_to_exe.py` now survives ONLY as: the `--libstub` MP anchor, the `build-example.sh`/`build-stevie.sh` `--libstub` anchor, the `build-{sprintf,int86x,divmod32}-probe.sh` probe scripts, and the tiny/.COM fallbacks — it is no longer the DEFAULT runtime of ANY shipped artifact.  **⇒ Next session: the libstub-retirement campaign is DONE; pick a carried COMPILER track (each awaits a real consumer — synthetic-but-bug-loud gating):** (1) the huge pointer EQUALITY flat-compare (the §7u relational fix's latent sibling — `==`/`!=` of two differently-normalised huge pointers; `_sbrk` only does `== NULL` so no live consumer); (2) the aoa sub-gaps (file-scope/static multi-decl array-first = a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl); (3) the minic file-scope-statics-need-a-trailing-`main` emission quirk surfaced in §8c (removing the last `main()` drops a TU's file-scope `static` data defs — confirmed via `.ssa` diff; no consumer, the `-Dmain` rename avoids it).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8d session notes (2026-06-16)

### The ask
- User: "go ahead and make libstub-free the default" — the §8d end-state §8c
  had deferred (flip + rebaseline; the user's call since it rebaselines the
  project's regression baseline).

### The change (tiny — one flag + docs)
- build-micropython.sh: NO_LIBSTUB default 0 → 1; --libstub now the opt-out
  anchor; --no-libstub re-asserts the default.  Comment + banner rebaselined.
- The 731,088 baseline was DOCUMENTATION-ONLY: `git grep 731088` over tools/
  *.sh *.py is EMPTY (no script assertion).  recompile-mp-tu.sh reads
  /tmp/mp_objs.txt (written by build-micropython.sh) so it adapts automatically.
- Doc rebaseline (forward-looking only): CLAUDE.md (corpus line + "After ANY
  toolchain change" house rule + §8d status clause + Last-Updated→§8d),
  ROADMAP.md (two "Body 731,088 B" lines).  Historical "731,088 byte-identical"
  session records LEFT verbatim (accurate for their time).

### Verification (no compiler change → no emit audit; binary == §8c's Victor-verified build → no new Victor run)
- Default (no flag) compact: image 710,352 / body 689,760 (reproducible; ==§8c).
- --libstub compact: image 751,664 / body 731,088 (anchor INTACT).
- Default small-heap build (571,600 B): smoke test (build/mp-nl-smoke.py)
  BYTE-EXACT vs host python3 in DOSBox.
- make check green.

### ⇒ Next session
- libstub-retirement campaign COMPLETE (gate probes + stevie + MicroPython all
  libstub-free by default; libstub_to_exe.py survives only as the --libstub
  anchor + probe scripts + tiny/.COM fallbacks).
- Carried compiler tracks (await a consumer): huge pointer EQUALITY flat-compare
  (§7u sibling); aoa sub-gaps; the minic trailing-main file-scope-statics quirk.
---

# Next session (§8d — optional: flip MicroPython's default to libstub-free (rebaseline the regression corpus) / carried compiler tracks.  §8c [2026-06-16, this session] **MOVED MicroPython ONTO THE LIBSTUB-FREE RUNTIME — the byte-frozen regression corpus now builds AND runs with NO libstub, byte-exact vs host `python3` in BOTH DOSBox and on real-hardware-equivalent MAME victor9k; `tools/build-micropython.sh --no-libstub` is the new path (OPT-IN — the default still builds the libstub corpus, byte-identical at image 751,664 / body 731,088), `make check` green, no compiler/qbe/emit/minic source touched (→ no emit audit).**  §8c's brief was "retire `libstub_to_exe.py`'s printf"; the FIRST finding overturned the §8b handoff's premise that "nothing depends on it now" — `build-micropython.sh` builds MP DIRECTLY through `libstub_to_exe.py` (no `--no-stdio`) and MP's gc'd map links `_far_printf`/`_far_sprintf`/`_far_fprintf`/`_printf`/`_sprintf` from `libstub_exe.obj`, so libstub's printf engine IS part of MP's frozen 731,088-byte body (and the `--libstub` anchor + tiny/.COM/absent-tree fallbacks + the probe scripts use it too).  The user directed: "go ahead and update MP, don't worry about the byte-freeze" — i.e. genuinely retire libstub for MP, the last big non-anchor consumer (the §7r/§7v stevie migration scaled up).  **THE TRUE GAP WAS TINY, NOT 124 SYMBOLS:** MP's libstub map lists 124 symbols, but `libstub_exe.obj` is a MONOLITHIC hand-asm object so `--gc-sections` can't strip individual functions — most (`_mouse_*`, `_putpixel`, `_set_video_mode`, `_getch`/`_kbhit`, `_dos_*`) are PASSENGERS MP never calls.  An empirical relink of MP's existing `.obj` against the libstub-free runtime showed the real undefined set: **(1) `___builtin_clzl` (103 refs) + `___builtin_clz`/`expect`/`unreachable`** — the one symbol set MP needs that libstub provided and the libstub-free runtime did not; **(2)** `_fopen`/`_fread`/…/`_read`/`__impure_ptr`/`_timer_*` — ALL provided by `dos_shim.c` (lost only because excluding it for its `_main` collision).  MP's file reader is fd-based (`read(fd)`/`close()` POSIX via `mp_reader_new_file_from_fd`), NOT stdio `FILE`, so the §7s cross-regime `FILE`-ABI landmine does NOT apply (`_far_fopen` is referenced only inside `libstub_exe.obj`).  **THE FIX (COPY/ADD, NEVER MUTATE):** (a) NEW **`minic/dos/builtins_rt.asm`** — the 4 `__builtin_*` bodies COPIED VERBATIM from `libstub.asm` (NEAR form, `[bp+N]` args; far-rewritten by `near_to_far_rt.py --seg-name=BUILTINS_RT_TEXT` for far models, exactly like `qbe_rt.asm`); `libstub.asm` UNTOUCHED.  (b) `build-micropython.sh` gained an OPT-IN `--no-libstub` branch (the §7v stevie far-data recipe: `dos_printf`/`scanf_wrappers`/`syscalls`/`reent_stubs`/`dirent`/`unlink`/`rename`/`dos_vfs`/`dos_shim`/`dos_libc` compiled in newlibc's regime + `qbe_rt`-far/`dos_syscall_far_data`/`far_stdlib_bridge`/`builtins_rt`-far/`setjmp_rt`(SJ_FAR_DATA)/`heap` INSTEAD of `libstub_exe.obj`), with MP's own `main()` renamed to `newlibc_test_main` via `-Dmain` so `dos_shim.c`'s `main` wrapper (`vfs_init()`→ a no-op on the DOS path → MP's main) is the crt0 entry — the §7r pattern.  `dos_shim.c` stays PRISTINE: a tried `-DNO_SHIM_MAIN` variant hit a **minic emission quirk** (removing the trailing `main()` drops ALL file-scope FILE-layer statics — `shim_files`/`shim_file_used`/`_impure_ptr` — from the TU; a latent minic bug noted, not fixed), so the `-Dmain` rename keeps `main()` present and sidesteps it.  **VERIFIED:** libstub-free MP (compact) links clean (110 modules, 0 undefined, image 710,352 / body 689,760 — smaller, the monolith passengers gone); a DOSBox-sized build (`MP_HEAP_SIZE=8192 MP_HEAP2_SIZE=12288 MP_STACK_SIZE=16384`, 571,600 B) ran a 13-line smoke test (filter/reversed/comprehensions/dict+sorted/`%`-format/str methods/slicing + FLOAT + `math` sqrt/pi/sin + recursion + ZeroDivisionError) BYTE-EXACT vs host `python3`; the default-heap build (710,352 B) ran the SAME test BYTE-EXACT on MAME victor9k (`run-victor-sasi.sh`, real SASI disk); the DEFAULT (libstub) build re-confirmed BYTE-IDENTICAL to the frozen corpus (751,664 / 731,088 — my `OBJS` refactor is a no-op for it); `make check` green.  STRATEGY: `builtins_rt.asm` is all-new; `build-micropython.sh`'s change is an additive opt-in branch (default path byte-unchanged); `dos_shim.c`/`libstub.asm`/`libstub_to_exe.py`/`near_to_far_rt.py`/every runtime asm + newlibc are UNTOUCHED.  libstub is now retired as MP's runtime *capability* (opt-in, verified); `libstub_to_exe.py` survives as MP's DEFAULT runtime + the `--libstub` equivalence anchor + the fallbacks.  **⇒ Next session (§8d):** the literal end-state would be to FLIP `build-micropython.sh`'s default to `--no-libstub` and REBASELINE the regression corpus (the frozen body becomes ~689,760; update every "731,088 byte-identical" reference + the MP byte-compare tooling/docs) — deferred because it rebaselines the project's regression baseline and is the user's call (the opt-in path is proven, so the flip is de-risked).  Carried compiler tracks (await a consumer, unchanged): the aoa sub-gaps (file-scope/static multi-decl array-first parse-error gap; plain `jmp_buf a, b;` multi-decl); the huge pointer EQUALITY flat-compare (the §7u relational sibling); the minic file-scope-statics-need-a-trailing-main quirk surfaced this session (no consumer — the `-Dmain` rename avoids it).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open.)

## §8c session notes (2026-06-16)

### The pick + the overturned premise
- §8b handoff offered "retire libstub_to_exe.py's printf (nothing depends on it
  now)".  The user (AskUserQuestion) chose it.  Investigation showed the premise
  is FALSE: build-micropython.sh builds MP through libstub_to_exe.py (no
  --no-stdio); MP's gc'd map links _far_printf/_far_sprintf/_printf/_sprintf
  from libstub_exe.obj → libstub's printf is in MP's frozen 731,088-byte body.
  It's also the --libstub anchor (build-example/stevie) + tiny/.COM/absent-tree
  fallbacks + build-{sprintf,int86x,divmod32}-probe.sh.  Surfaced this; the user
  said "go ahead and update MP, don't worry about the byte-freeze" → the real
  goal = move MP onto the libstub-free runtime (the §7r/§7v stevie migration).

### The gap was tiny (124-symbol map was mostly monolith passengers)
- libstub_exe.obj is a MONOLITHIC hand-asm object → --gc-sections can't strip
  individual functions, so _mouse_*/_putpixel/_set_video_mode/_getch/_kbhit/
  _dos_* are passengers MP never calls.  Empirical relink of MP's existing .obj
  against the libstub-free runtime gave the TRUE undefined set:
    (1) ___builtin_clzl (103) + ___builtin_clz/expect/unreachable — NEW work.
    (2) _fopen/_fread/.../_read/__impure_ptr/_timer_* — all in dos_shim.c
        (undefined only because I'd excluded it for its _main collision).
- FILE-ABI (§7s) does NOT apply: MP's reader is fd-based (read(fd)/close via
  mp_reader_new_file_from_fd), not stdio FILE; _far_fopen is referenced only
  inside libstub_exe.obj.

### The fix
- NEW minic/dos/builtins_rt.asm: the 4 __builtin_* COPIED VERBATIM from
  libstub.asm (near form; far-rewritten via near_to_far_rt.py
  --seg-name=BUILTINS_RT_TEXT, like qbe_rt.asm).  libstub.asm UNTOUCHED.
- build-micropython.sh: opt-in --no-libstub branch (default keeps libstub).
  The §7v far-data recipe + builtins_rt + -Dmain=newlibc_test_main so
  dos_shim.c's main wrapper is the crt0 entry (vfs_init() is a no-op on DOS).
- dos_shim.c PRISTINE.  A -DNO_SHIM_MAIN variant hit a minic quirk: removing
  the trailing main() drops the file-scope FILE-layer statics (shim_files/
  shim_file_used/_impure_ptr) from the TU entirely (confirmed via .ssa diff —
  0 data defs without main, 4 with).  The -Dmain rename keeps main() present
  and sidesteps it.  (Latent minic bug noted, no consumer → not fixed.)

### Verification (no compiler change → no emit audit)
- libstub-free MP compact: links clean, 110 modules, 0 undefined, image
  710,352 / body 689,760 (smaller — monolith passengers gone).
- DOSBox (571,600 B small-heap build): smoke test BYTE-EXACT vs host python3
  (build/mp-nl-smoke.py / .golden.txt — language + FLOAT + math + recursion +
  ZeroDivisionError).
- MAME victor9k (710,352 B default-heap, run-victor-sasi.sh, real SASI):
  SAME smoke test BYTE-EXACT vs host python3.
- DEFAULT (libstub) MP: rebuilt, BYTE-IDENTICAL frozen corpus 751,664/731,088
  (the OBJS-assembly refactor is a no-op for the default path).
- make check green.

### ⇒ Next session (§8d)
- Optional end-state: flip build-micropython.sh's default to --no-libstub and
  REBASELINE the corpus (frozen body → ~689,760; update every "731,088"
  reference + MP byte-compare tooling/docs).  Deferred — rebaselines the
  project's regression baseline, the user's call; the opt-in path is proven so
  the flip is de-risked.
- Carried compiler tracks (await a consumer): aoa sub-gaps; huge pointer
  EQUALITY flat-compare; the minic file-scope-statics-need-a-trailing-main
  quirk (no consumer — the -Dmain rename avoids it).
---

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
