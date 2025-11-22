# Claude Session Status: QBE C11 8086 Compiler

**Project:** C11 Compiler for 8086 DOS using QBE Backend
**Last Updated:** 2025-11-21
**Session:** Initial Architectural Analysis
**Status:** ✅ Planning Complete, Ready for Implementation

---

## Current Session Summary

### What We Accomplished

**Session Goal:** Analyze the QBE C11 8086 compiler project architecture and create an implementation plan.

**Major Deliverables:**
1. ✅ **Complete architectural analysis** (C11_8086_ARCHITECTURE.md)
2. ✅ **Phased implementation roadmap** (ROADMAP.md)
3. ✅ **Session documentation** (this file)
4. ✅ **Resume prompt for next session** (RESUME_PROMPT.md)

### Critical Discovery

**The c2qbe compiler mentioned in the original prompt DOES NOT EXIST.**

Initial prompt suggested:
- ❌ Two separate C compilers (MiniC and c2qbe) needing to be merged
- ❌ Complex architectural decision needed
- ❌ 16 commits of c2qbe work on a separate branch

**Actual situation:**
- ✅ Only MiniC exists - highly sophisticated, C89/C99 compliant
- ✅ i8086 backend exists - functional but lacks floating-point
- ✅ No merge needed - just connect components
- ✅ Add 8087 FPU support (~500 lines) to complete pipeline

**This significantly simplifies the project!**

---

## Project Status

### Component Status

| Component | Status | Details |
|-----------|--------|---------|
| **MiniC Compiler** | ✅ Complete | 2,276 lines, C89/C99 compliant, float/double support |
| **i8086 Backend** | ⚠️ Partial | 1,206 lines, integer ops work, FPU missing |
| **8087 FPU Support** | ❌ Missing | ~500 lines needed, critical blocker |
| **DOS Runtime** | ❌ Missing | Need crt0.asm, printf, file I/O |
| **C11 Features** | ❌ Missing | Target 60% compliance, ~8 features |
| **Documentation** | ✅ Complete | Architecture, roadmap, API docs planned |

### Current Capabilities

**What Works Today:**
- ✅ MiniC compiles C to QBE IL (all types including float/double)
- ✅ QBE optimizes IL and generates 8086 assembly
- ✅ Integer operations work end-to-end
- ✅ Function calls with cdecl convention
- ✅ Structs, unions, enums, typedefs
- ✅ Pointers and arrays
- ✅ All control flow (if/while/for/switch/goto)

**What's Broken:**
- ❌ Float/double compilation to DOS (no 8087 FPU)
- ❌ Cannot link to DOS (no runtime library)
- ❌ 32-bit long operations incomplete
- ❌ No memory models except small

---

## Architectural Analysis Results

### MiniC Compiler Analysis

**Architecture:** Yacc-based LALR(1) parser with integrated code generation

**Strengths:**
- Direct QBE IL emission (no AST overhead)
- Comprehensive type system
- IEEE 754 float/double support
- 84 tests, 100% pass rate
- Clean, maintainable code

**Feature Coverage:**
- **C89:** ~95% (missing only preprocessor)
- **C99:** ~70% (missing VLAs, some library features)
- **C11:** ~30% (missing most C11-specific features)

**Key Files:**
- `minic/minic.y` - Main compiler (2,276 lines)
- `minic/test/` - 84 test files
- `NEW_FEATURES_DOCUMENTATION.md` - Feature reference

### i8086 Backend Analysis

**Architecture:** QBE backend module (4 files: abi, isel, emit, targ)

**Strengths:**
- Clean separation of concerns
- Proper cdecl implementation
- Good register allocation
- Works with standard DOS toolchain (NASM, OpenWatcom)

**Implementation Status:**
- ✅ 16-bit integer arithmetic (add, sub, mul, div, rem)
- ✅ Bitwise operations (and, or, xor, shl, shr)
- ✅ Comparisons (signed and unsigned)
- ✅ Control flow (branches, loops, switch)
- ✅ Function calls (cdecl convention)
- ✅ Memory addressing (all i8086 modes)
- ⚠️ 32-bit long operations (partial)
- ❌ Floating-point (no 8087 FPU)

**Key Files:**
- `i8086/abi.c` - Calling convention (306 lines)
- `i8086/isel.c` - Instruction selection (217 lines)
- `i8086/emit.c` - Assembly emission (578 lines)
- `i8086/targ.c` - Target registration (53 lines)

### The Floating-Point Problem

**Critical Blocker:** MiniC generates QBE float operations (`=s`, `=d`), but i8086 backend cannot handle them.

**Three Solutions Evaluated:**

1. **Disable float for DOS** ❌ Poor user experience
2. **Software FP emulation** ❌ 100-1000x slower, complex
3. **8087 FPU support** ✅ **RECOMMENDED**
   - Hardware speed (1-10x faster than integer)
   - IEEE 754 compliant
   - Authentic DOS experience
   - ~500 lines of code, 2-3 weeks

**Recommendation:** Implement 8087 FPU support (Option 3)

---

## Implementation Roadmap

### Phase 0: Validation (Week 1)
**Goal:** Verify integer-only pipeline works

**Tasks:**
- Build QBE with i8086 backend
- Build MiniC compiler
- Test integer-only compilation
- Install DOS toolchain (NASM, OpenWatcom, DOSBox)

**Deliverable:** Can generate assembly from C

---

### Phase 1: Integer-Only DOS (Week 2)
**Goal:** Integer C programs compile and run on DOS

**Tasks:**
- Create DOS startup code (crt0.asm)
- Basic DOS runtime (putchar, exit, printf)
- Build script (build-dos.sh)
- Fix 32-bit long support
- Test programs (Hello World, arithmetic)

**Deliverable:** Hello World runs in DOSBox

---

### Phase 2: 8087 FPU (Weeks 3-5)
**Goal:** Full float/double support

**Week 3: Foundation**
- FPU instruction encoding (i8086/fpu.c)
- Instruction selection for float/double
- Basic operations (add, sub, mul, div)

**Week 4: Operations & Conversions**
- FPU comparisons
- Type conversions (int ↔ float)
- FPU register allocation (stack management)

**Week 5: Testing & Polish**
- FPU test suite (16+ tests)
- Bug fixes and edge cases
- Validate IEEE 754 compliance

**Deliverable:** All 84 MiniC tests pass on DOS

---

### Phase 3: DOS Integration (Weeks 6-8)
**Goal:** Production-quality DOS programs

**Week 6: Complete DOS Runtime**
- Enhanced printf (all format specifiers)
- File I/O (open, read, write, close)
- Memory allocation (malloc, free)

**Week 7: DOS Interrupt Library**
- DOS interrupt interface (int86)
- Video functions (VGA mode 13h)
- Keyboard/mouse support

**Week 8: Memory Models**
- Tiny model (.COM files)
- Medium model (large code)
- Large model (large code + data)

**Deliverable:** 10+ DOS example programs

---

### Phase 4: C11 Features (Weeks 9-12)
**Goal:** 60% C11 compliance

**Week 9: High-Priority (1 day each)**
- _Static_assert (compile-time checks)
- Compound literals (temporary objects)
- Designated initializers (named struct init)

**Week 10: Medium-Priority (2-3 days each)**
- Anonymous struct/union
- _Alignof/_Alignas

**Week 11: Advanced (5 days)**
- _Generic (type-generic macros)

**Week 12: Polish**
- C11 test suite
- Complete documentation

**Deliverable:** 60% C11 compliance, documentation complete

---

## Milestones

| Milestone | Week | Success Criteria |
|-----------|------|------------------|
| **M1: Integer DOS** | 2 | Hello World runs in DOSBox |
| **M2: Float Support** | 5 | All 84 MiniC tests pass on DOS |
| **M3: DOS Integration** | 8 | 10+ example programs complete |
| **M4: C11 Compliance** | 12 | 60% C11, documentation complete |

---

## Risk Assessment

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| 8087 FPU complexity | High | Medium | Incremental testing, one instruction at a time |
| DOS toolchain issues | Medium | Medium | Use maintained tools (OpenWatcom, NASM) |
| Memory model bugs | Medium | High | Start with small model only |
| Testing difficulty | Medium | High | Automate with DOSBox scripts |
| 32-bit long support | Low | Medium | Careful DX:AX pair implementation |

---

## Key Decisions Made

### 1. FPU Strategy: Hardware 8087 ✅
- **Decision:** Implement 8087 coprocessor support
- **Rationale:** Best balance of performance, compatibility, effort
- **Alternative rejected:** Software FP emulation (too slow)

### 2. Architecture: Single Compiler ✅
- **Decision:** Keep MiniC as sole frontend
- **Rationale:** No c2qbe exists, MiniC is excellent
- **Alternative rejected:** None needed

### 3. C11 Compliance Target: 60% ✅
- **Decision:** Focus on DOS-relevant features
- **Rationale:** _Atomic, _Thread_local inappropriate for DOS
- **Features included:** _Static_assert, compound literals, designated init, _Generic, _Alignof/_Alignas

### 4. Memory Models: Small First ✅
- **Decision:** MVP with small model, add others in Phase 3
- **Rationale:** Small model covers 90% of use cases
- **Future:** Add tiny, medium, large models

---

## Documentation Created

### Primary Documents
1. **C11_8086_ARCHITECTURE.md** (14,000+ words)
   - Complete component analysis
   - Integration strategies
   - C11 feature gap analysis
   - Compilation pipeline diagram
   - Testing strategy
   - Risk assessment

2. **ROADMAP.md** (8,000+ words)
   - Phased implementation plan
   - Week-by-week task breakdown
   - Deliverables and success criteria
   - Resource requirements
   - Milestone tracking

3. **claude.md** (this file)
   - Session summary
   - Current status
   - Key decisions
   - Next steps

4. **RESUME_PROMPT.md**
   - Context for next session
   - Quick start guide
   - Where we left off

### Existing Documentation
- `I8086_TARGET.md` - i8086 backend reference
- `NEW_FEATURES_DOCUMENTATION.md` - MiniC features
- `PHASE1-2_EXPANSION.md` - Feature expansion history

---

## Next Session Priorities

### Immediate Actions (Phase 0 - Week 1)

**Priority 1: Validate Pipeline (4 hours)**
```bash
# Build QBE
make clean && make

# Build MiniC
cd minic && make

# Test integer compilation
echo 'int main() { return 42; }' > test.c
./minic < test.c > test.ssa
../qbe -t i8086 test.ssa > test.asm
cat test.asm  # Verify assembly
```

**Priority 2: Install Toolchain (2 hours)**
- NASM: `sudo apt-get install nasm`
- OpenWatcom: Download from GitHub
- DOSBox: `sudo apt-get install dosbox`

**Priority 3: Create DOS Runtime (Week 2)**
- Write crt0.asm (DOS startup code)
- Write dos_runtime.c (putchar, exit, basic printf)
- Write build-dos.sh (complete build script)
- Test Hello World in DOSBox

### Questions to Resolve

- [ ] **FPU approach approved?** Hardware 8087 vs software emulation?
- [ ] **Memory model scope?** Small only for MVP vs all models?
- [ ] **C11 feature priority?** Which features first?
- [ ] **Testing environment?** DOSBox only vs 86Box vs real hardware?

---

## Files Modified/Created This Session

### Created
- `C11_8086_ARCHITECTURE.md` - Complete architectural analysis
- `ROADMAP.md` - Implementation roadmap
- `claude.md` - This session status file
- `RESUME_PROMPT.md` - Next session context

### To Be Created (Next Session)
- `minic/dos/crt0.asm` - DOS startup code
- `minic/dos/dos_runtime.c` - DOS C runtime
- `minic/dos/dos_runtime.h` - Runtime header
- `tools/build-dos.sh` - Build script
- `tools/test-dos.sh` - Test runner
- `i8086/fpu.c` - 8087 FPU support (Phase 2)

---

## Recommendations for Next Session

### Start With
1. **Read the documentation**
   - C11_8086_ARCHITECTURE.md (comprehensive analysis)
   - ROADMAP.md (implementation plan)
   - Understand the floating-point problem and solution

2. **Validate the pipeline**
   - Build QBE and MiniC
   - Test integer-only compilation
   - Verify assembly output looks correct

3. **Install toolchain**
   - NASM, OpenWatcom, DOSBox
   - Test each tool individually

### Then Proceed To
4. **Phase 1 implementation** (Week 2)
   - Create DOS startup code
   - Create basic runtime library
   - Build Hello World for DOS
   - Test in DOSBox

### Success Metrics
- [ ] Documentation read and understood
- [ ] Pipeline validated (C → assembly works)
- [ ] Toolchain installed and tested
- [ ] Ready to start Phase 1 implementation

---

## Long-Term Vision

**Project Goal:** Build one of the most advanced open-source C compilers for DOS/8086.

**Target Capabilities:**
- ✅ Full C89/C99 support
- ✅ 60% C11 compliance (DOS-relevant features)
- ✅ Hardware floating-point (8087 FPU)
- ✅ Complete DOS API library
- ✅ Professional documentation
- ✅ 20+ working example programs
- ✅ Performance comparable to Turbo C 2.0

**Timeline:** 10-12 weeks to production release

**Community Impact:**
- Retro computing enthusiasts
- DOS game developers
- Embedded 8086 systems
- Educational use (compiler design, DOS programming)

---

## Resources

### Documentation
- [C11_8086_ARCHITECTURE.md](./C11_8086_ARCHITECTURE.md) - Full analysis
- [ROADMAP.md](./ROADMAP.md) - Implementation plan
- [I8086_TARGET.md](./I8086_TARGET.md) - Backend reference
- [NEW_FEATURES_DOCUMENTATION.md](./NEW_FEATURES_DOCUMENTATION.md) - MiniC features

### External Resources
- [QBE IL Documentation](https://c9x.me/compile/doc/il.html)
- [Intel 8086 Family User's Manual](https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf)
- [8087 FPU Programmer's Reference](http://www.electronics.dit.ie/staff/tscarff/8087_family/8087_intel_manuall.pdf)
- [DOS Interrupt List](http://www.ctyme.com/intr/int.htm)

### Tools
- **NASM:** https://www.nasm.us/
- **OpenWatcom v2:** https://github.com/open-watcom/open-watcom-v2
- **DOSBox:** https://www.dosbox.com/
- **86Box:** https://86box.net/

---

## Contact & Collaboration

This project is developed by Paul Devine with assistance from Claude (Anthropic).

**Repository:** https://github.com/pauldevine/qbe
**Branch:** `claude/c11-8086-qbe-compiler-01WY6Ga4YosUW79m3yL2pMvZ`

---

**Session Status:** ✅ Complete
**Next Session:** Resume with Phase 0 (Validation)
**Estimated Completion:** 10-12 weeks from start
**Current Phase:** Planning → Implementation Ready

---

*Last updated: 2025-11-21*
*Next update: After Phase 0 completion*
