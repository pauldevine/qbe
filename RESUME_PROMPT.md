# Resume Prompt: QBE C11 8086 Compiler Implementation

**Session Type:** Implementation (Resume from Architectural Analysis)
**Date:** 2025-11-21
**Phase:** Ready to Begin Phase 0 (Validation)

---

## Quick Context

You are continuing work on a **C11 compiler for 8086 real-mode DOS** using the QBE compiler backend. The architectural analysis is complete, and we're ready to begin implementation.

---

## What Was Accomplished (Previous Session)

### Critical Discovery
**The c2qbe compiler does NOT exist.** The project is simpler than initially thought:
- ✅ MiniC compiler exists (excellent C89/C99 support)
- ✅ i8086 backend exists (integer ops work)
- ❌ 8087 FPU support missing (critical blocker)
- 🎯 Task: Connect components + add FPU

### Documentation Created
1. **C11_8086_ARCHITECTURE.md** - Complete architectural analysis (14,000+ words)
2. **ROADMAP.md** - Phased implementation plan (8,000+ words)
3. **claude.md** - Session status and decisions
4. **RESUME_PROMPT.md** - This file

---

## Current Project Status

### What Works Today
- ✅ **MiniC compiler:** C89/C99 compliant, 84 tests, float/double support
- ✅ **i8086 backend:** Integer arithmetic, function calls, cdecl convention
- ✅ **Pipeline:** C → QBE IL → 8086 assembly (integers only)

### What's Blocked
- ❌ **Float/double to DOS:** No 8087 FPU support
- ❌ **Linking to DOS:** No runtime library (crt0, printf, etc.)
- ❌ **32-bit long:** Incomplete in i8086 backend
- ❌ **C11 features:** ~60% target compliance missing

---

## System Architecture (Quick Reference)

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│    MiniC     │  QBE IL │      QBE     │   x86   │  DOS Binary  │
│  (minic.y)   ├────────>│   i8086      ├────────>│  (.exe)      │
│  2,276 lines │         │   Backend    │   asm   │              │
└──────────────┘         └──────────────┘         └──────────────┘
     Working                  Needs FPU             Need Runtime
```

**Current blocker:** MiniC generates QBE float ops (`=s`, `=d`), but i8086 backend can't handle them.

**Solution:** Implement 8087 FPU support (~500 lines, 2-3 weeks)

---

## Implementation Roadmap (10-12 Weeks)

### **Phase 0: Validation (Week 1)** ← **YOU ARE HERE**
**Goal:** Verify integer-only pipeline works

**Tasks:**
1. Build QBE with i8086 backend
2. Build MiniC compiler
3. Test integer-only compilation (C → assembly)
4. Install DOS toolchain (NASM, OpenWatcom, DOSBox)
5. Document any build issues

**Deliverable:** Can generate 8086 assembly from C code

---

### **Phase 1: Integer-Only DOS (Week 2)**
**Goal:** Integer C programs run on DOS

**Tasks:**
1. Create DOS startup code (`minic/dos/crt0.asm`)
2. Basic DOS runtime (`dos_runtime.c` - putchar, exit, printf)
3. Build script (`tools/build-dos.sh`)
4. Fix 32-bit long support (DX:AX pairs)
5. Test programs (Hello World, arithmetic)

**Deliverable:** Hello World runs in DOSBox, returns correct exit code

---

### **Phase 2: 8087 Floating-Point (Weeks 3-5)**
**Goal:** Full float/double support

**Week 3: Foundation**
- FPU instruction encoding (`i8086/fpu.c` - new file)
- Instruction selection for float/double
- Basic operations (fadd, fsub, fmul, fdiv)

**Week 4: Operations & Conversions**
- FPU comparisons (fcom, fstsw)
- Type conversions (fild, fistp for int ↔ float)
- FPU register allocation (stack management)

**Week 5: Testing & Polish**
- FPU test suite (16+ tests)
- Bug fixes and edge cases
- IEEE 754 compliance validation

**Deliverable:** All 84 MiniC tests pass on DOS (including float/double)

---

### **Phase 3: DOS Integration (Weeks 6-8)**
**Goal:** Production-quality DOS programs

- Complete DOS runtime (full printf, file I/O, malloc/free)
- DOS interrupt library (int86, video, keyboard, mouse)
- Multiple memory models (tiny, small, medium, large)
- 10+ example programs

**Deliverable:** Professional DOS C development environment

---

### **Phase 4: C11 Features (Weeks 9-12)**
**Goal:** 60% C11 compliance

- _Static_assert, compound literals, designated initializers
- Anonymous struct/union
- _Alignof/_Alignas
- _Generic (type-generic macros)
- Complete documentation

**Deliverable:** C11-compliant compiler with documentation

---

## Key Technical Details

### MiniC Compiler
- **Location:** `minic/minic.y`
- **Size:** 2,276 lines (yacc + C)
- **Output:** QBE Intermediate Language (IL)
- **Features:** C89/C99 complete, float/double, structs, unions, enums, function pointers
- **Tests:** 84 files in `minic/test/`, 100% pass rate
- **Build:** `cd minic && make`

### i8086 Backend
- **Location:** `i8086/` (abi.c, isel.c, emit.c, targ.c)
- **Size:** 1,206 lines total
- **Output:** 16-bit x86 assembly (Intel syntax, NASM compatible)
- **Convention:** cdecl (args on stack, caller cleanup)
- **Registers:** AX, BX, CX, DX, SI, DI (BP for frame pointer)
- **Missing:** 8087 FPU support, complete 32-bit long support

### 8087 FPU (To Be Implemented)
- **Hardware:** 8087/80287/80387 coprocessor
- **Architecture:** 8 FP registers (ST(0)-ST(7)), stack-based
- **Operations:** fadd, fsub, fmul, fdiv, fcom, fld, fstp
- **Conversions:** fild (int→float), fistp (float→int)
- **Implementation:** New file `i8086/fpu.c` (~500 lines)

---

## File Structure

```
qbe/
├── minic/
│   ├── minic.y                  # MiniC compiler (2,276 lines)
│   ├── test/                    # 84 test files
│   └── dos/                     # NEW: DOS runtime (to be created)
│       ├── crt0.asm             # DOS startup
│       ├── dos_runtime.c        # C runtime
│       └── examples/            # Example programs
│
├── i8086/
│   ├── abi.c, isel.c, emit.c, targ.c  # Existing backend
│   └── fpu.c                    # NEW: 8087 support (Phase 2)
│
├── tools/
│   ├── build-dos.sh             # NEW: Build script
│   └── test-dos.sh              # NEW: Test runner
│
├── docs/                        # Documentation
│   ├── C11_8086_ARCHITECTURE.md # Complete analysis
│   ├── ROADMAP.md               # Implementation plan
│   ├── claude.md                # Session status
│   └── RESUME_PROMPT.md         # This file
│
└── Makefile                     # Build QBE
```

---

## Toolchain Requirements

### Required Tools
- **NASM** - Assembler for x86 (nasm.us)
  - Install: `sudo apt-get install nasm` (Linux)
  - Or download from nasm.us (Windows/Mac)

- **OpenWatcom v2** - Linker for DOS (github.com/open-watcom/open-watcom-v2)
  - Download from GitHub releases
  - Provides `wlink` (linker) and `libc.lib`

- **DOSBox** - DOS emulator for testing (dosbox.com)
  - Install: `sudo apt-get install dosbox` (Linux)
  - Or download from dosbox.com (Windows/Mac)

### Optional Tools
- **86Box** - Cycle-accurate emulator for thorough testing
- **Real hardware** - 80286/80386 with DOS 6.22 (final validation)

---

## Build Commands (Quick Reference)

### Build QBE
```bash
make clean && make
./qbe -h  # Verify i8086 target listed
```

### Build MiniC
```bash
cd minic && make
./minic < test/simple_test.c > test.ssa
```

### Test Compilation (Integer-Only)
```bash
# Create simple test
echo 'int main() { return 42; }' > test.c

# Compile to QBE IL
./minic/minic < test.c > test.ssa

# Generate assembly
./qbe -t i8086 test.ssa > test.asm

# View assembly
cat test.asm
```

### Full Build (Once Runtime Exists)
```bash
# Will be automated in build-dos.sh
./minic/minic < program.c > program.ssa
./qbe -t i8086 program.ssa > program.asm
nasm -f obj program.asm -o program.obj
wlink system dos file program.obj,crt0.obj,runtime.obj name program.exe
dosbox -c "program.exe"
```

---

## Key Decisions Made

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **FPU Strategy** | Hardware 8087 | Best performance/effort balance |
| **Compiler Base** | MiniC only | No c2qbe exists |
| **C11 Target** | 60% compliance | Focus on DOS-relevant features |
| **Memory Model** | Small first | Covers 90% of use cases |
| **Testing** | DOSBox automated | Easy to automate, cross-platform |

---

## Immediate Next Steps (Phase 0)

### Step 1: Validate Build (1 hour)
```bash
# Build QBE
make clean && make

# Build MiniC
cd minic && make

# Test compilation
echo 'int main() { return 42; }' > test.c
./minic < test.c > test.ssa
../qbe -t i8086 test.ssa > test.asm

# Check output
cat test.asm  # Should see 8086 assembly
```

**Success:** Assembly generated without errors

### Step 2: Install Toolchain (2 hours)
- Download and install NASM
- Download and install OpenWatcom v2
- Install DOSBox
- Test each tool individually

**Success:** All tools available and working

### Step 3: Document Issues (30 minutes)
- Note any build errors
- Document toolchain versions
- Create setup.md with installation steps

### Step 4: Plan Phase 1 (30 minutes)
- Review ROADMAP.md Phase 1 tasks
- Decide on task priority
- Allocate time estimates

---

## Success Criteria for This Session

### Phase 0 Complete When:
- [ ] QBE builds successfully with i8086 support
- [ ] MiniC compiles test programs
- [ ] Integer-only compilation works (C → assembly)
- [ ] Toolchain installed (NASM, OpenWatcom, DOSBox)
- [ ] Build process documented
- [ ] Ready to begin Phase 1 (DOS runtime)

### Phase 1 Goal (Next Session):
- [ ] Hello World compiles to DOS .exe
- [ ] Program runs in DOSBox
- [ ] Return code verified (ERRORLEVEL)
- [ ] Basic printf works

---

## Common Issues & Solutions

### Issue: QBE doesn't build
**Solution:** Check for yacc/bison, standard C compiler. Run `make clean` first.

### Issue: MiniC yacc conflicts
**Solution:** Expected (1 shift/reduce, 1 reduce/reduce). Doesn't affect functionality.

### Issue: Can't find i8086 target
**Solution:** Rebuild QBE completely. Check `i8086/` directory exists.

### Issue: OpenWatcom linker not found
**Solution:** Add OpenWatcom bin directory to PATH. Or use absolute path to wlink.

### Issue: DOSBox won't run .exe
**Solution:** Check .exe format (should be MZ header). Try different DOSBox version.

---

## Testing Strategy

### Phase 0-1: Manual Testing
- Build and run each test manually
- Verify output visually
- Use DOSBox for execution

### Phase 2+: Automated Testing
```bash
# Test script (to be created)
./tools/test-dos.sh
# Runs all tests in DOSBox
# Reports pass/fail
# Saves output logs
```

### Test Pyramid
- **Unit tests:** 200+ (type system, operators)
- **Functional tests:** 50 (language features)
- **Integration tests:** 10 (full DOS programs)

---

## Documentation References

### Primary Documents (Read First)
1. **C11_8086_ARCHITECTURE.md** - Complete technical analysis
   - Component status
   - Integration strategies
   - FPU implementation details
   - C11 feature gaps

2. **ROADMAP.md** - Week-by-week implementation plan
   - Task breakdowns
   - Time estimates
   - Deliverables
   - Success criteria

3. **claude.md** - Session status and decisions
   - What was accomplished
   - Key decisions made
   - Current status

### Existing Documentation
- **I8086_TARGET.md** - i8086 backend reference manual
- **NEW_FEATURES_DOCUMENTATION.md** - MiniC features guide
- **PHASE1-2_EXPANSION.md** - Feature expansion history

### External Resources
- [QBE IL Documentation](https://c9x.me/compile/doc/il.html)
- [Intel 8086 Manual](https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf)
- [8087 FPU Reference](http://www.electronics.dit.ie/staff/tscarff/8087_family/8087_intel_manuall.pdf)
- [DOS Interrupts](http://www.ctyme.com/intr/int.htm)

---

## Questions to Answer This Session

### Phase 0 Questions
- [ ] Does QBE build cleanly on this system?
- [ ] Does MiniC generate correct QBE IL?
- [ ] Does i8086 backend generate valid assembly?
- [ ] Are all required tools available?

### Phase 1 Questions
- [ ] What's the minimal crt0.asm needed?
- [ ] Which DOS functions should printf use?
- [ ] How to handle argc/argv parsing?
- [ ] What's the simplest test case?

---

## Git Branch & Workflow

**Current Branch:** `claude/c11-8086-qbe-compiler-01WY6Ga4YosUW79m3yL2pMvZ`

**Git Workflow:**
1. Create feature branches for each phase
2. Commit frequently with clear messages
3. Create PRs for major milestones
4. Merge to main branch after testing

**Recent Commits:**
- Architectural analysis documentation
- Roadmap creation
- Session status updates

---

## Contact & Collaboration

**Project Owner:** Paul Devine (pauldevine)
**Repository:** github.com/pauldevine/qbe
**Collaborator:** Claude (Anthropic)

**Communication:**
- Document all decisions in claude.md
- Create clear commit messages
- Use PR descriptions for major changes
- Update ROADMAP.md as plans evolve

---

## Final Checklist Before Starting Implementation

- [ ] Read C11_8086_ARCHITECTURE.md (understand the problem)
- [ ] Read ROADMAP.md (understand the solution)
- [ ] Read claude.md (understand the context)
- [ ] Review Phase 0 tasks
- [ ] Ensure tools are available
- [ ] Ready to build and test

---

**Status:** ✅ Ready for Implementation
**Next Action:** Begin Phase 0 (Validation)
**Timeline:** 10-12 weeks to production release
**Current Phase:** Week 1 of 12

---

## Quick Start Command Sequence

```bash
# 1. Verify repository
pwd  # Should be /home/user/qbe
git status
git branch  # Should be on claude/c11-8086-qbe-compiler-...

# 2. Build QBE
make clean && make

# 3. Build MiniC
cd minic && make && cd ..

# 4. Test compilation
echo 'int main() { int x = 42; return x; }' > test.c
./minic/minic < test.c > test.ssa
./qbe -t i8086 test.ssa > test.asm

# 5. View output
cat test.asm

# 6. If successful, proceed to Phase 1
# If errors, debug and document issues
```

---

**Good luck with the implementation!**

**Remember:** This is a 10-12 week project. Phase 0 is just validation. Take time to understand the architecture before coding.

---

*Last updated: 2025-11-21*
*Resume from: Phase 0 (Validation)*
*Estimated completion: 12 weeks from start*
