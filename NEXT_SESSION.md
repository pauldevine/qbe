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

Older session headers (§7u and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
