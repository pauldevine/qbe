# Claude Session Status: QBE C11 8086 Compiler

**Project:** C11 Compiler for 8086 DOS using QBE Backend
**Last Updated:** 2026-05-24 (t — far stdio FILE* under large/huge)
**Status:** ~96% Complete

---

## 📍 Current Project Status

**For up-to-date project status, progress tracking, and implementation details, see:**

### **→ [ROADMAP.md](./ROADMAP.md) ←**

The ROADMAP.md file contains:
- **Accurate current status** of all components (updated 2026-05-23)
- **Phase completion tracking** (Phases 0, 1, 2, 4 complete; Phase 3 ~90%)
- **Component status table** with evidence and file references
- **What's actually missing** vs what's been completed
- **Original planned roadmap** for reference

---

## Quick Status Summary

**Completed ✅:**
- MiniC Compiler (C89/C99/C11)
- i8086 Backend (all integer + FPU ops + 32-bit div/rem via libstub helpers)
- 8087 FPU Support (PR #11)
- Inline Assembly (commits d44ea80, c0ddbff)
- C11 Features: _Static_assert, _Generic, _Alignof/_Alignas, compound literals, designated initializers (PR #12)
- Far Pointers (PR #13)
- 32-bit long support (DX:AX pairs + libstub div/rem)
- Function pointers, struct bitfields
- ANSI C function definitions (PR #15)
- **DOS Runtime Library** — real printf/sprintf, freelist malloc/free, file I/O (commits 775fd38, fc8d2bc, 76c213e, 19f6029)
- **DOS API** — int86/int86x/intdos/intdosx/segread + video/keyboard/mouse wrappers (commits 28941ae, d36f103)
- **OMF link toolchain** — tools/omf_link.py, asm_to_omf.py, libstub_to_exe.py
- **Stevie editor** — full .EXE port (148 KB medium-model), `:w` round-trips, `/search` works
- **Examples** — 16 legacy + 3 modern `<dos.h>` demos (mouse_demo, vga_pixels, kbtest)

**In Progress ⚠️:**
- Memory Models — runtime gate covers tiny/medium/compact/large/huge (27/27 ok in `tools/test-dos.sh`).  Huge Phase C landed 2026-05-24 (r): per-symbol `_HUGE_<sym>` segments let huge mode hold arrays > 64K.  Far DOS-API + puts landed 2026-05-24 (s).  Far stdio FILE* landed 2026-05-24 (t): `_far_fopen`/`_far_fclose`/`_far_fputs`/`_far_fputc`/`_far_fgets` in FAR_STDIO_EXE; 4-byte `_stdin`/`_stdout`/`_stderr` sentinels; `stdio_far_probe.c` exercises under large + huge.
- Tiny memory model (.COM) — stevie.com still over 64 KB ([[minic-pointer-bloat]])
- Small .EXE — architecturally broken: libstub_to_exe.py rewrites every `ret` to `retf`, mismatches small's near-call ABI → DOSBox hangs.  Needs near+far libstub variants or model-conditional ret rewrite.  See [[per-model-gate]].
- Large/huge fread/fwrite/fflush/fprintf — still not in `far_stdlib[]`.  Probes don't exercise them under far-data yet; if a caller tries, near-pointer ABI mismatch.  Deferred until a real consumer needs them.
- Latent Kl-CAddr arith — Phase B routes most through `_qbe_huge_add`, but ~10 sites in i8086/emit.c (lines 986/1000/1047/...) and far-ptr inc/dec at minic.y:2189, 2234 still consult `bits.i` directly without a CAddr check.  Not exercised by any in-tree probe.
- Kl shift AX clobber ([[i8086-kl-shift-clobbers-ax]]) — Oshl/Oshr/Osar Kl handlers clobber AX/DX without telling rega; latent.  Fix mirror of [[i8086-kl-add-sub-mul-r1-alias]].
- Phase B storel-via-Kl-slot gap ([[huge-phase-b-storel-gap]]) — backend writes value→[bp+slot] directly even when slot holds a pointer VALUE; not yet exercised by any in-tree probe.

---

## Key Documentation Files

- **[ROADMAP.md](./ROADMAP.md)** - Current status and implementation plan (UPDATED 2026-05-23)
- **[C11_8086_ARCHITECTURE.md](./C11_8086_ARCHITECTURE.md)** - Architectural analysis
- **[NEW_FEATURES_DOCUMENTATION.md](./NEW_FEATURES_DOCUMENTATION.md)** - MiniC feature reference
- **[I8086_TARGET.md](./I8086_TARGET.md)** - i8086 backend reference
- **[i8086/README.md](./i8086/README.md)** - i8086 backend documentation
- **[NEXT_SESSION_PROMPT.md](./NEXT_SESSION_PROMPT.md)** - Resume prompt for the next session

---

## Recent Major Accomplishments

### Far DOS-API + puts under large/huge (2026-05-24, session s)
- ✅ `tools/libstub_to_exe.py` — new `FAR_DOSIO_EXE` constant in EPILOGUE with `_far_intdos`, `_far_int86`, `_far_segread`, `_far_puts`.  Each reads/writes its struct arg(s) via ES:BX (loaded from the 4-byte far ptr), swaps ES between in/out structs when needed, restores caller's ES on exit.  `_far_segread` stashes caller ES in SI before overwriting it.  `_far_puts` writes via `mov ds, es` temporarily then restores DS=DGROUP via `push ss; pop ds`.
- ✅ `minic/minic.y` — `far_stdlib[]` extended with `intdos`, `int86`, `segread` so calls under MCompact/MLarge/MHuge mangle to `_far_X` (puts/fputs/fputc/fgets were already listed but most had no near-helper backing).
- ✅ `minic/dos/examples/dos_far_probe.c` + golden — 8 assertions under `--model=large`: segread invariants, intdos AH=0x30/0x3300, int86 AH=0x30, `puts()` writes a string.
- ✅ `tools/test-dos.sh` — `dos_far_probe` wired under large + huge.  Gate now **25/25 ok**.
- Carry-over: `_far_fputs`/`_far_fputc`/`_far_fgets` still need FILE* sizing under far-data (4-byte representation, model-aware EPILOGUE for stdin/stdout/stderr sentinels, `_far_fopen`/`_far_fclose`).  See [[large-huge-bringup]] / NEXT_SESSION_PROMPT.md.

### Huge Phase C — per-symbol _HUGE_<sym> segments (2026-05-24, session r)
- ✅ `tools/asm_to_omf.py` — recognises `.section "_HUGE_<sym>"` markers, splits `times N db 0` bodies across paragraph-aligned chunks (HUGE_CHUNK_BYTES = 65520 = 4095 paragraphs each), emits each as `segment _HUGE_<sym>_<N> class=HUGE align=16 use16`.
- ✅ `tools/omf_link.py` — 4th layout phase after STACK places HUGE-class segments distinctly, sorted by name so consecutive `_0/_1/_2` chunks land at adjacent paragraph bases (no inter-chunk padding).  Existing fixup machinery handles MZ segment relocations unchanged.
- ✅ `minic/minic.y` — new `glosec[NGlo][NString]` + `maybe_mark_huge_global(idx, sym, total)`.  Under `MHuge` with `total > 65536`, sets `glosec[i] = "_HUGE_<sym>"`; global-emit loop prepends `section "..."` to the data decl.  Wired into file-scope-array and static-local-array rules.
- ✅ `minic/dos/examples/hugeprobe.c` + golden — `static char arr[80000]` exercises arr[0]/arr[65535]/arr[65536]/arr[79999] via both subscript and pointer arith.
- Gate now **23/23 ok**.  `large` and `huge` are finally genuinely distinct.

### Per-model runtime gate (2026-05-24, session o)
- ✅ `minic/dos/examples/tinyprobe.c` — first real tiny .COM runtime probe.  Uses inline-asm INT 21h AH=40h for output (libstub `_printf` is a stub for .COM; `_sprintf` IS implemented in libstub.asm so we sprintf-then-write).  17 verified lines: arithmetic, near-ptr pass, fn-ptr table, struct global, static local, 32-bit divmod, sprintf widths, near-pointer walk, local-array deref.
- ✅ `tools/test-dos.sh` extended with `COM_RUNTIME_TESTS` block + `run_com_runtime_probe` helper.  Gate now **21/21 ok**.
- Documented 3 architectural gaps in `[[per-model-gate]]`: small .EXE (libstub_to_exe ret→retf rewrite breaks small near-call ABI), large/huge DOS-API + stdio (libstub helpers consume near pointers), huge >64K data (no normalization + 64K/segment linker).

### Large + huge memory models bring-up (2026-05-24, session n)
- ✅ All 4 compact-mode runtime probes (cstrprobe / compactprobe_extra / fnptrprobe / farretprobe) pass verbatim under `--model=large` and `--model=huge` — same goldens.
- ✅ `tools/test-dos.sh` gate extended to 20/20 with 8 new entries (4 probes × {large, huge}).
- ✅ No qbe / minic / libstub changes were required — the existing `_far_X` helper family + `uses_far_code()` / `NEAR_CODE()` model gating already covered the surface.
- Carry-over: large/huge **DOS-API** (`intdos`/`int86`/`segread`) and **stdio** (`fputs`/`fputc`/`fgets`/`puts`) still read near pointers off the stack, so they corrupt under large/huge.  Needs `_far_intdos`/`_far_int86`/`_far_segread`/`_far_fputs`/... + adding those names to `far_stdlib[]` in `minic.y:1252`.  See [[large-huge-bringup]].

### Compact runtime test wired into test-dos.sh (2026-05-23)
- ✅ `tools/run-dos-exe.sh` — generic runner: copies .EXE to 8.3 short name, generates a DOSBox autoexec.bat-equivalent conf, captures `OUT.TXT`, strips CRLF.  Handles `$DOSBOX` env override, `dosbox` on PATH, and the macOS .app path; exit 77 = skip-not-fail when DOSBox is unavailable.
- ✅ `tools/test-dos.sh` adds a `compact runtime (cstrprobe)` step that builds via `tools/build-example.sh --model=compact` and diffs against `minic/dos/tests/cstrprobe.golden.txt`.  Now reports 5/5 ok.
- ✅ `cstrprobe.c` extended to cover all 13 `_far_X` helpers + `%p`.  Validation uses `strcmp`/`strlen`/`memcmp` returns (single int per printf) instead of multi-byte loadfb varargs, side-stepping the pre-existing `[[i8086-compact-loadfb-aliases-ax]]` register-allocation bug.

### Compact far-helpers + `_far_sprintf` (2026-05-23)
- ✅ 13 new `_far_X` helpers in `minic/dos/libstub.asm`: strlen, strcpy, strcmp, strncmp, strncpy, strchr, strrchr, strcat, strcspn, strstr, memcpy, memcmp, memset — each takes 4-byte far pointer args; pointer returns via DX:AX (seg:off).  Functions that need two distinct source segments swap DS/ES with save/restore.
- ✅ `_far_sprintf` added to `tools/libstub_to_exe.py` EPILOGUE: clones the `_sprintf` format engine but writes to a far dest (ES:DI), copies the far fmt into a DGROUP scratch up front, consumes %s/%p as 4-byte far ptrs (with DS-swap for the source-string copy), and forces %p to the 32-bit hex path.  `_far_printf` / `_far_fprintf` now delegate to it.
- ✅ Runtime-verified: `tools/build-example.sh --model=compact minic/dos/examples/cstrprobe.c` prints all 16 expected lines including `%s` over DGROUP literals + stack-local buffers, width/precision/left-align padding, and mixed `%s`/`%d` varargs.

### Compact memory model end-to-end (2026-05-23, commit 493b84b)
- ✅ `uses_far_code()` now includes Mcompact; selret emits `Jretf*`, selcall emits `Ocallfar`, crt0's `call far _main` lines up with main's `retf`
- ✅ minic mangles known stdlib calls to `far_X` (asm `_far_X`) in compact/large/huge
- ✅ `_far_printf` / `_far_fprintf` injected by `libstub_to_exe.py`; copy 4-byte far fmt into local DGROUP scratch, then call `_sprintf`
- ✅ `--model=<m>` plumbed through `asm_to_omf.py`, `libstub_to_exe.py`, `omf_link.py` (reserved for future near-code coalescing)
- ✅ Runtime-verified: `tools/build-example.sh --model=compact minic/dos/examples/cprobe.c` prints `x=42 / x=99` in DOSBox

### Tiny / cheap DOS API (2026-05-22, commits 28941ae + d36f103)
- ✅ int86 / int86x / intdos / intdosx / segread (full `union REGS` / `struct SREGS`)
- ✅ set_video_mode, putpixel (VGA mode 13h), kbhit, getche, bdos
- ✅ INT 33h mouse: mouse_reset / mouse_show / mouse_hide / mouse_get_pos
- ✅ Three new `#include <dos.h>` demos (mouse_demo, vga_pixels, kbtest)
- ✅ `tools/build-example.sh` parameterized build for any `<dos.h>` demo

### Real DOS Runtime (2026-05-20…22)
- ✅ Full sprintf/printf with width/precision/flags + `l` 32-bit modifier (commit 775fd38)
- ✅ File I/O: fopen mode-aware, fread/fwrite/fputc/fputs/fprintf, getc/fclose (commit fc8d2bc)
- ✅ Freelist malloc/free, ~39 KB heap (commits 76c213e, 19f6029)
- ✅ 32-bit div/rem via libstub helpers (commit c53ce0a)

### Stevie editor .EXE port (2026-05-15…21)
- ✅ Full medium-model .EXE build via `tools/build-stevie.sh --exe`
- ✅ File load/edit/`:w` round-trips real DOS files
- ✅ `/search` and regex work; render loop fixed
- ✅ Multiple i8086 codegen bugs flushed out and fixed along the way

### PR #11 - 8087 FPU & Long Support (2025-11-26)
- ✅ Full hardware float/double operations
- ✅ Comparisons with FPU status word
- ✅ Type conversions (int ↔ float/double)
- ✅ 32-bit long support with DX:AX pairs

### PR #12 - C11 Features (2025-11-26)
- ✅ _Static_assert, _Generic, _Alignof/_Alignas
- ✅ Compound literals, designated initializers
- ✅ Anonymous struct/union

### Inline Assembly Support (commits d44ea80, c0ddbff)
- ✅ GCC-style extended inline assembly with output/input operands and clobber lists

### PR #13 - Far Pointers (commit 6492370)
- ✅ Far pointer support for small memory model

### PR #15 - ANSI Functions (commit 03d0b81)
- ✅ ANSI C-style function definitions

---

## Next Priorities

1. **Large / huge memory models** — only tiny / small / medium / compact implemented

2. **Latent Kl-CAddr arith** — ~10 sites in `i8086/emit.c` (lines 986/1000/1047/1060/1105/1115/1150/1173/1196/...) and far-ptr inc/dec in `minic.y:2189, 2234` still use `bits.i` directly without a CAddr check.  Not exercised by cprobe/cstrprobe but a latent bug for `p + offsetof_constant` style code.

3. **Tiny memory model (.COM) stevie shrink** — orthogonal to compact work
   - Path A (near-pointer narrowing) is partially landed (commit 5125e70, 98K→81K)
   - Need further shrink: dead-code elimination, library partitioning, or pointer ABI tweaks
   - See `[[minic-pointer-bloat]]` and NEXT_SESSION_PROMPT.md

4. **211-commit upstream-qbe rebase** — pure plumbing; deferred until i8086 backend stabilises

---

## Repository Information

**Repository:** https://github.com/pauldevine/qbe
**Current Branch:** master
**Main Branch:** master

**Recent Key Commits:**
- `493b84b` - i8086+minic: compact uses far-code ABI; libstub _far_printf landed
- `1f197a0` - i8086+minic: compact far-data deref + Kl CAddr seg/off + Kl call return
- `e70a5dc` - Roadmap: reflect DOS API + runtime close-out (~90%)
- `d36f103` - libstub: INT 33h mouse wrappers + parameterized example build
- `28941ae` - libstub: complete int86x/intdosx/segread plus DOS API wrappers
- `c53ce0a` - i8086: 32-bit div/rem via libstub helpers
- `fc8d2bc` - libstub: real file I/O for .EXE so stevie's :w persists edits
- `775fd38` - libstub: full sprintf with width/precision/hex/octal/long
- `76c213e` - libstub: real freelist malloc/free + bump heap to ~34KB

---

## Project Contact

This project is developed by Paul Devine with assistance from Claude (Anthropic).

For detailed status, progress tracking, and implementation plans, always refer to **[ROADMAP.md](./ROADMAP.md)**.

---

*Last updated: 2026-05-23*
*See ROADMAP.md for current status*
