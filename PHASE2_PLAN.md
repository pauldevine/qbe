# Phase 2: 8087 FPU Support Implementation Plan

**Date:** 2025-11-22
**Status:** 🚀 Starting Implementation
**Goal:** Add hardware floating-point support using 8087/80287/80387 coprocessor

## Overview

Phase 1 (integer-only pipeline) is complete. Phase 2 adds floating-point support through hardware FPU instructions, enabling MiniC to compile float/double operations to 8087 coprocessor instructions.

## Implementation Strategy

### Incremental Approach
Start with simple operations and test each step:
1. ✅ Basic FP loads/stores (fld, fstp)
2. ✅ Simple arithmetic (fadd, fsub, fmul, fdiv)
3. ✅ Comparisons (fcom, fstsw)
4. ✅ Type conversions (fild, fistp)
5. ✅ Advanced ops (fsqrt, transcendentals - optional)

### 8087 FPU Architecture

**Register Stack:**
- 8 registers: ST(0) through ST(7)
- 80-bit extended precision internally
- Stack-based operations (push/pop semantics)
- ST(0) is top of stack

**Instruction Format:**
```asm
fld  dword [mem]    ; Load float, push to ST(0)
fadd st(0), st(1)   ; ST(0) = ST(0) + ST(1)
fstp dword [mem]    ; Store ST(0) to memory, pop
```

**Memory Formats:**
- `dword` - 32-bit float (4 bytes)
- `qword` - 64-bit double (8 bytes)
- `tword` - 80-bit long double (10 bytes)

## Implementation Tasks

### Task 1: Add FPU Instruction Encoding (emit.c)
**File:** `i8086/emit.c`
**Lines:** ~200
**Priority:** High

Add instruction templates for:
```c
// Basic operations
{ Ofadd,  Ks, "fadd" },
{ Ofsub,  Ks, "fsub" },
{ Ofmul,  Ks, "fmul" },
{ Ofdiv,  Ks, "fdiv" },
{ Ofneg,  Ks, "fchs" },

// Load/Store
{ Ofloads, Ks, "fld dword %M0" },
{ Ofstores, Ks, "fstp dword %M0" },
{ Ofloadd, Kd, "fld qword %M0" },
{ Ofstored, Kd, "fstp qword %M0" },

// Conversions
{ Ofstoi, Kw, "fistp word %M0" },   // float to int
{ Ofitos, Ks, "fild word %M0" },    // int to float

// Comparisons
{ Ofcmp, Ks, "fcom\n\tfstsw ax\n\tsahf" },
```

### Task 2: FP Register Allocation (isel.c)
**File:** `i8086/isel.c`
**Lines:** ~150
**Priority:** High

Implement FP register stack management:
- Track FP stack depth
- Emit fld/fstp to manage stack
- Handle register pressure (8 register limit)
- Spill to memory when necessary

**Key Functions:**
```c
static int fpstack = 0;  // Current FP stack depth

static void fpush(Ref r) {
    // Load value to FP stack
    fpstack++;
    assert(fpstack <= 8);
}

static void fpop(Ref r) {
    // Store and pop from FP stack
    fpstack--;
    assert(fpstack >= 0);
}
```

### Task 3: QBE IL Mapping (isel.c)
**File:** `i8086/isel.c`
**Lines:** ~100
**Priority:** High

Map QBE operations to 8087 instructions:

| QBE Op | Class | 8087 Instruction | Notes |
|--------|-------|------------------|-------|
| Oadd | Ks/Kd | fadd | ST(0) += ST(1), pop ST(1) |
| Osub | Ks/Kd | fsub | ST(0) -= ST(1), pop ST(1) |
| Omul | Ks/Kd | fmul | ST(0) *= ST(1), pop ST(1) |
| Odiv | Ks/Kd | fdiv | ST(0) /= ST(1), pop ST(1) |
| Oneg | Ks/Kd | fchs | ST(0) = -ST(0) |
| Oloads | Ks | fld dword | Load 32-bit float |
| Oloadd | Kd | fld qword | Load 64-bit double |
| Ostores | Ks | fstp dword | Store 32-bit float |
| Ostored | Kd | fstp qword | Store 64-bit double |

### Task 4: Type Conversions
**File:** `i8086/isel.c`
**Lines:** ~50
**Priority:** Medium

Implement type conversion operations:

```c
// int → float
case Ocast:
    if (i.from == Kw && i.to == Ks) {
        // fild word [mem]  ; Load integer, convert to float
    }
    break;

// float → int
case Ocast:
    if (i.from == Ks && i.to == Kw) {
        // fistp word [mem]  ; Convert to integer, store
    }
    break;
```

### Task 5: Testing Framework
**Lines:** ~100 test cases
**Priority:** High

Create comprehensive tests:

**Test 1: Basic Arithmetic**
```c
float test_add() {
    float a = 2.0;
    float b = 3.0;
    return a + b;  // Expected: 5.0
}
```

**Test 2: Type Conversion**
```c
int test_ftoi() {
    float f = 3.7;
    return (int)f;  // Expected: 3
}
```

**Test 3: Comparisons**
```c
int test_cmp() {
    float a = 2.5;
    float b = 3.5;
    return a < b;  // Expected: 1 (true)
}
```

## Implementation Order

### Week 1: Foundation
1. ✅ Add FPU opcodes to ops.h
2. ✅ Add instruction templates to emit.c
3. ✅ Test simple fld/fstp generation
4. ✅ Verify NASM can assemble FPU instructions

### Week 2: Core Operations
1. ✅ Implement FP arithmetic (add, sub, mul, div)
2. ✅ Add FP register tracking in isel.c
3. ✅ Create simple test program (float add)
4. ✅ Verify in DOSBox with 8087 emulation

### Week 3: Advanced Features
1. ✅ Implement type conversions
2. ✅ Add FP comparisons
3. ✅ Handle edge cases (NaN, infinity, overflow)
4. ✅ Run MiniC float test suite

## Success Criteria

### Minimal Viable Product (MVP)
- [ ] Compile simple float arithmetic: `float f = 2.0 + 3.0;`
- [ ] Generate valid 8087 assembly
- [ ] Execute correctly in DOSBox
- [ ] Pass 5 basic FP tests

### Phase 2 Complete
- [ ] All MiniC float/double tests pass (84 tests)
- [ ] 16 FPU-specific tests pass
- [ ] No FP stack errors
- [ ] Mandelbrot set demo works

## Technical Notes

### 8087 Stack Management
The 8087 is stack-based, which differs from QBE's SSA model:

**QBE SSA:**
```
%a =s loads %mem1
%b =s loads %mem2
%c =s add %a, %b
stores %c, %mem3
```

**8087 Stack-based:**
```asm
fld dword [mem1]    ; ST(0) = mem1
fld dword [mem2]    ; ST(0) = mem2, ST(1) = mem1
fadd                ; ST(0) = ST(0) + ST(1), pop
fstp dword [mem3]   ; mem3 = ST(0), pop
```

**Challenge:** Need to track stack depth and emit appropriate loads/stores.

### DOSBox FPU Support
DOSBox emulates 8087/80287/80387:
- Full instruction set support
- Cycle-accurate emulation
- No special configuration needed
- Use `cycles=auto` for best performance

### Testing Strategy
1. **Unit tests:** Each FPU operation individually
2. **Integration tests:** Complex expressions
3. **Regression tests:** Existing MiniC test suite
4. **Real-world demos:** Mandelbrot, ray tracer

## Risks and Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| FP stack overflow | High | Implement stack depth tracking, emit fxch/fstp to manage |
| Incorrect comparisons | Medium | Test all comparison ops thoroughly, check flag handling |
| Type conversion bugs | Medium | Test boundary cases (NaN, ±∞, 0.0, -0.0) |
| Register allocation | High | Start with simple spill-everything approach, optimize later |

## Resources

**Documentation:**
- Intel 8087 Programmer's Reference Manual
- QBE IL documentation (qbe.c-lang.org)
- NASM FPU instruction reference

**Testing:**
- DOSBox 0.74-3 (8087 emulation)
- 86Box (optional, more accurate)
- NASM 2.15+ (FPU instruction support)

## Next Steps

After completing Phase 2:
1. **Phase 3:** DOS integration (file I/O, graphics, mouse)
2. **Phase 4:** C11 feature additions
3. **Phase 5:** Optimization and polish

---

**Let's begin with Task 1: Adding FPU instruction encoding to emit.c**
