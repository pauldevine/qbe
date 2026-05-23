# Claude Session Status: QBE C11 8086 Compiler

**Project:** C11 Compiler for 8086 DOS using QBE Backend
**Last Updated:** 2026-05-22
**Status:** ~90% Complete

---

## 📍 Current Project Status

**For up-to-date project status, progress tracking, and implementation details, see:**

### **→ [ROADMAP.md](./ROADMAP.md) ←**

The ROADMAP.md file contains:
- **Accurate current status** of all components (updated 2026-05-22)
- **Phase completion tracking** (Phases 0, 1, 2, 4 complete; Phase 3 ~85%)
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
- Memory Models — small + medium .EXE work; tiny (.COM) still over 64 KB

**Missing ❌:**
- Tiny memory model (.COM) — stevie.com over 64 KB ([[minic-pointer-bloat]])
- Large/huge memory models

---

## Key Documentation Files

- **[ROADMAP.md](./ROADMAP.md)** - Current status and implementation plan (UPDATED 2026-05-22)
- **[C11_8086_ARCHITECTURE.md](./C11_8086_ARCHITECTURE.md)** - Architectural analysis
- **[NEW_FEATURES_DOCUMENTATION.md](./NEW_FEATURES_DOCUMENTATION.md)** - MiniC feature reference
- **[I8086_TARGET.md](./I8086_TARGET.md)** - i8086 backend reference
- **[i8086/README.md](./i8086/README.md)** - i8086 backend documentation
- **[NEXT_SESSION_PROMPT.md](./NEXT_SESSION_PROMPT.md)** - Resume prompt for the next session

---

## Recent Major Accomplishments

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

1. **Tiny memory model (.COM)** — shrink stevie.com under the 64 KB ceiling
   - Path A (near-pointer narrowing) is partially landed (commit 5125e70, 98K→81K)
   - Need further shrink: dead-code elimination, library partitioning, or pointer ABI tweaks
   - See `[[minic-pointer-bloat]]` and NEXT_SESSION_PROMPT.md

2. **Large / huge memory models** — only small + medium implemented

3. **211-commit upstream-qbe rebase** — pure plumbing; deferred until i8086 backend stabilises

---

## Repository Information

**Repository:** https://github.com/pauldevine/qbe
**Current Branch:** master
**Main Branch:** master

**Recent Key Commits:**
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

*Last updated: 2026-05-22*
*See ROADMAP.md for current status*
