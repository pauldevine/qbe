# Next session (§8c — finish Phase‑6 libstub-retirement / open compiler tracks.  §8b [2026-06-16, this session] **CLOSED the `%p`/`%o` printf track — the LAST four `--libstub` pins (`cstrprobe`/`compactprobe_extra`/`huge_norm_probe`/`mediumprobe`) now run libstub-free byte-identical to their goldens, so NO gate probe pins `--libstub` anymore and the Phase-6 libstub-retirement campaign is COMPLETE; `tools/test-dos.sh` GREEN 366/366 (count unchanged — an unpin, not a new probe), `make check` green, no compiler/qbe/emit/minic source touched (→ no emit audit; MP links NONE of the changed files and builds via `libstub_to_exe.py` directly → no MP byte-compare).**  The four probes gate the §4i/§4s/§7g far/huge normalisation arithmetic against libstub-captured goldens via `%p`/`%o`, but newlibc's `tiny_vformat` (`printf_wrappers.c`) prints `%p` as a `0x`-prefixed width-4 hex and does NOT implement `%o` (echoes the literal), so they pinned the libstub python-printf anchor.  **FIX (COPY/ADD, NEVER MUTATE — the dos_vfs.c pattern):** new **`minic/dos/newlibc/dos_printf.c`** is a VERBATIM fork of `printf_wrappers.c` with exactly two deltas — `%p` (raw value, lowercase, NO `0x`, zero-padded to the full pointer width: 8 hex far / 4 hex near; the far branch reads the arg as `va_arg(ap, unsigned long)` because minic's `(uintptr_t)(void*)` cast DROPS the segment under far-data, recovering the raw `(seg<<16)|off` so `1734:0007`→`17340007`; the near branch keeps the offset cast → `5678`) and `%o` (base-8, no prefix → `0777`→`777`) — linked INSTEAD of `printf_wrappers.c` ONLY on the `build-example.sh`/`build-stevie.sh` `--no-libstub` path (the `NL_SUPPORT` swap), so newlibc stays pristine and the bare-metal (`bm_stdio`) + `build-newlibc-test.sh` paths keep newlibc's printf → their goldens + the newlibc corpus (no `%p`/`%o`) are byte-untouched.  Gate: removed the four-probe `libstubflag="--libstub"` case in `test-dos.sh` (goldens UNTOUCHED — libstub-captured, the libstub-free path now reproduces them).  VERIFIED: each probe byte-identical to its golden libstub-free across every gated model; full gate 366/366; stevie startup screen BYTE-IDENTICAL 152 B (libstub-free default vs `--libstub` anchor, §7r/§7v); `make check` green; the §7y/§7z caches auto-invalidate (stamp hashes `NL_SUPPORT`).  With setjmp (§7x), the DGROUP heap (§8a), and now printf `%p`/`%o` (§8b) all libstub-free, libstub is no longer a correctness dependency of ANY gate probe — `--libstub` survives only as the optional equivalence anchor.  **⇒ Next session (§8c):** the literal end-state — delete `libstub_to_exe.py`'s python printf for non-anchor use (or retire `libstub_to_exe.py` outright), now that nothing depends on it; OR a carried compiler track (the aoa sub-gaps; the huge pointer EQUALITY flat-compare, the §7u relational fix's latent sibling — both await a consumer).  Bare-metal phase-3 bm_testhost tests EXHAUSTED.  NO QBE backend bug open; easy frame-size levers spent (§7k).)

## §8b session notes (2026-06-16)

### The pick (continued §8a handoff — track 1 of the remaining cleanup)
- §8a listed two §8b options + the carried compiler tracks; the user
  (AskUserQuestion) chose the **%p/%o printf shim** — unpin the last four
  `--libstub` pins — over deleting libstub_to_exe.py's python printf and the
  carried tracks.  Knowingly the "messiest" one (it had been flagged as a
  local-patch-or-override decision touching newlibc's shared printf).

### The bug (why the four probes pinned --libstub)
- cstrprobe / compactprobe_extra / huge_norm_probe / mediumprobe assert the
  §4i/§4s/§7g far/huge normalisation arithmetic by printing pointers with %p
  (and one %o), against libstub-captured goldens.
- newlibc's tiny_vformat (printf_wrappers.c): %p → out_string("0x") +
  width-4 hex; %o → unimplemented (default arm echoes "%o").  libstub's python
  printf instead prints %p as raw lowercase hex, NO "0x", zero-padded to the
  full pointer width, and implements %o.

### The decision: OVERRIDE, not patch-in-place
- COPY/ADD-NEVER-MUTATE is the project invariant; dos_vfs.c (§7s) is exact
  precedent (shadows newlibc's vfs.c on the build-example/build-stevie path
  while build-newlibc-test.sh keeps the real one).
- printf_wrappers.c is actively developed upstream in newlibc (last commit adds
  precision support) → patching in place would conflict on every sync.
- Patching in place would also reach bm_stdio (bare-metal) and
  build-newlibc-test.sh, where the corpus/goldens must stay pristine.
- Blast-radius check: no newlibc TEST uses %p/%o (vshell.c even avoids %o
  deliberately); no bare-metal bm_* probe uses %p.  So the only %p/%o consumers
  on the build-example path are exactly these four probes.

### The fix (minic/dos/newlibc/dos_printf.c + 3 build/gate edits)
- dos_printf.c = VERBATIM copy of printf_wrappers.c (the whole printf family +
  getchar/fgets/putchar/puts/fputc/fputs — they share the static tiny_vformat,
  so a partial override is impossible) with two deltas:
    %p: branch on sizeof(void*).  Far (==4): out_unsigned(va_arg(ap,
        unsigned long), 16, 0, 8, -1, 1, 0) — reading the 4-byte far-ptr arg as
        unsigned long recovers the raw (seg<<16)|off, because minic's
        (uintptr_t)(void*) cast yields ONLY the offset under far-data (the
        first attempt printed 00005678 for (char*)0x12345678L — segment lost).
        Near (==2): (uintptr_t)va_arg(ap, void*), width 4 (offset → 5678).
    %o: base-8, no prefix (mediumprobe's 0777 → 777).
- build-example.sh + build-stevie.sh: NL_SUPPORT swaps
  $NL/libgloss/printf_wrappers.c → $DOS_DIR/newlibc/dos_printf.c.  The §7y/§7z
  caches auto-invalidate (their stamp iterates NL_SUPPORT).  bare-metal and
  build-newlibc-test.sh untouched → keep newlibc's printf.
- test-dos.sh: deleted the cstrprobe|compactprobe_extra|huge_norm_probe|
  mediumprobe libstubflag case; rewrote the comment to record the campaign is
  complete (no probe pins --libstub).

### Verification (no compiler change → no emit audit; MP unaffected → no byte-compare)
- All four probes byte-identical to their goldens libstub-free at the gated
  models: cstrprobe small=5678 / compact=12345678,00000042,cafe0004;
  compactprobe_extra compact; huge_norm_probe huge=17340007,...; mediumprobe
  medium oct=777.
- Full gate 366/366 ok from cold §7y/§7z caches.  make check green.
- stevie startup screen BYTE-IDENTICAL 152 B, libstub-free default vs --libstub
  anchor (build both --exe ways, capture run-dos-exe's STDERR timeout dump,
  strip the first banner line).  stevie uses no %p/%o so the swap is a no-op for
  it, as expected.
- MP build (build-micropython.sh) links none of dos_printf.c / NL_SUPPORT /
  the build-example path (grep = 0) → no rebuild/byte-compare needed.
- Structural diff confirms dos_printf.c is verbatim vs printf_wrappers.c except
  the %o case (new) and the %p case (rewritten).

### ⇒ Next session (§8c)
- Delete libstub_to_exe.py's python printf for non-anchor use (or retire
  libstub_to_exe.py outright) — nothing depends on it now.
- Carried compiler tracks (await a consumer): the aoa sub-gaps (file-scope/
  static multi-decl array-first parse-error gap; plain `jmp_buf a, b;`
  multi-decl); the huge pointer EQUALITY flat-compare (§7u relational sibling).
---


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

Older session headers (§7w and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
