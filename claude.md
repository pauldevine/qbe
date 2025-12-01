# Claude Session Status: QBE C11 8086 Compiler

**Project:** C11 Compiler for 8086 DOS using QBE Backend
**Last Updated:** 2025-12-01
**Status:** ~70-80% Complete

---

## 📍 Current Project Status

**For up-to-date project status, progress tracking, and implementation details, see:**

### **→ [ROADMAP.md](./ROADMAP.md) ←**

The ROADMAP.md file contains:
- **Accurate current status** of all components (updated 2025-12-01)
- **Phase completion tracking** (Phases 0, 2, 4 complete; Phases 1, 3 partial)
- **Component status table** with evidence and file references
- **What's actually missing** vs what's been completed
- **Original planned roadmap** for reference

---

## Quick Status Summary

**Completed ✅:**
- MiniC Compiler (C89/C99/C11)
- i8086 Backend (all integer + FPU ops)
- 8087 FPU Support (PR #11)
- Inline Assembly (commits d44ea80, c0ddbff)
- C11 Features: _Static_assert, _Generic, _Alignof/_Alignas, compound literals, designated initializers (PR #12)
- Far Pointers (PR #13)
- 32-bit long support
- Function pointers, struct bitfields
- ANSI C function definitions (PR #15)

**In Progress ⚠️:**
- DOS Runtime Library (~80% - crt0.asm exists, needs full printf/file I/O/malloc)
- DOS Integration (~30% - basic runtime, need full DOS API)

**Missing ❌:**
- Complete DOS runtime library (printf, file I/O, malloc/free)
- Memory models (tiny, medium, large, huge)
- DOS API wrappers (video, keyboard/mouse, interrupts)
- Comprehensive example programs (have 9, need 10-20)

---

## Key Documentation Files

- **[ROADMAP.md](./ROADMAP.md)** - Current status and implementation plan (UPDATED 2025-12-01)
- **[C11_8086_ARCHITECTURE.md](./C11_8086_ARCHITECTURE.md)** - Architectural analysis
- **[NEW_FEATURES_DOCUMENTATION.md](./NEW_FEATURES_DOCUMENTATION.md)** - MiniC feature reference
- **[I8086_TARGET.md](./I8086_TARGET.md)** - i8086 backend reference
- **[i8086/README.md](./i8086/README.md)** - i8086 backend documentation

---

## Recent Major Accomplishments

### PR #11 - 8087 FPU & Long Support (2025-11-26)
- ✅ Full hardware float/double operations
- ✅ All arithmetic: add, sub, mul, div, neg
- ✅ Comparisons with FPU status word
- ✅ Type conversions (int ↔ float/double)
- ✅ 32-bit long support with DX:AX pairs
- ✅ Function pointer support
- ✅ Struct bitfield support

### PR #12 - C11 Features (2025-11-26)
- ✅ _Static_assert, _Generic, _Alignof/_Alignas
- ✅ Compound literals, designated initializers
- ✅ Anonymous struct/union

### Inline Assembly Support (commits d44ea80, c0ddbff)
- ✅ GCC-style extended inline assembly
- ✅ Output/input operands
- ✅ Clobber lists

### PR #13 - Far Pointers (commit 6492370)
- ✅ Far pointer support for small memory model

### PR #15 - ANSI Functions (commit 03d0b81)
- ✅ ANSI C-style function definitions

---

## Next Priorities

1. **Complete DOS Runtime Library** (Phase 3)
   - Full printf implementation (all format specifiers)
   - File I/O functions (fopen, fread, fwrite, fclose)
   - Memory allocation (malloc, free, realloc)

2. **DOS API Wrappers**
   - Video functions (VGA mode 13h)
   - Keyboard/mouse support
   - DOS interrupt library (int86)

3. **Memory Models**
   - Tiny model (.COM files)
   - Medium, large, huge models

4. **Example Programs**
   - Expand from 9 to 10-20 examples
   - Demonstrate all features

---

## Repository Information

**Repository:** https://github.com/pauldevine/qbe
**Current Branch:** claude/add-clobbers-and-attributes
**Main Branch:** master

**Key Commits:**
- `c0ddbff` - Add clobber lists and GCC attribute support
- `d44ea80` - Add inline assembly support
- `d6bdd8c` - Fix FAR pointer flag conflict
- `e01104b` - Add floating point (8087 FPU) support
- `e52c1a7` - Add 32-bit long type support

---

## Project Contact

This project is developed by Paul Devine with assistance from Claude (Anthropic).

For detailed status, progress tracking, and implementation plans, always refer to **[ROADMAP.md](./ROADMAP.md)**.

---

*Last updated: 2025-12-01*
*See ROADMAP.md for current status*
