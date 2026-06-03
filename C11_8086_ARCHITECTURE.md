# QBE C11/C17 Compiler for 8086: Complete Architecture Analysis

**Date:** 2025-11-21
**Author:** Claude (Anthropic)
**Status:** Architectural Analysis Complete
**Version:** 1.0

---

## Executive Summary

This document provides a comprehensive architectural analysis of building a complete C11/C17 compiler targeting 8086 real-mode DOS binaries using the QBE compiler backend.  ("C11/C17" because C17 introduced no new language features over C11 — a C11-complete frontend is also C17-complete.  C23 language features such as `nullptr`/`constexpr`/`typeof`/`_BitInt`/`[[…]]` attributes are **not** implemented.)

**Critical Finding:** The c2qbe compiler mentioned in initial planning documents does not exist in this repository. The actual situation is:

- ✅ **MiniC compiler exists** - Highly sophisticated, C89/C99 compliant
- ✅ **i8086 backend exists** - Functional but lacks floating-point support
- ❌ **No separate c2qbe compiler** - No merge decision needed
- 🎯 **Actual task:** Connect MiniC → QBE → i8086 for complete DOS compilation pipeline

---

## Table of Contents

1. [Current Component Analysis](#1-current-component-analysis)
2. [Integration Analysis: The Floating-Point Problem](#2-integration-analysis-the-floating-point-problem)
3. [C11 Feature Gap Analysis](#3-c11-feature-gap-analysis)
4. [Complete Compilation Pipeline](#4-complete-compilation-pipeline)
5. [Recommended Architecture](#5-recommended-architecture)
6. [Testing Strategy](#6-testing-strategy)
7. [Risk Assessment](#7-risk-assessment)
8. [Success Metrics](#8-success-metrics)
9. [Conclusion](#9-conclusion)

---

## 1. Current Component Analysis

### 1.1 MiniC Compiler (minic/minic.y)

**Architecture:** Yacc-based LALR(1) parser with integrated code generation
**Size:** 2,276 lines (single-file design)
**Output:** QBE Intermediate Language (IL)

#### Feature Matrix

| Category | Features | Status | Notes |
|----------|----------|--------|-------|
| **Types** | int, char, short, long, long long | ✅ Complete | Full signed/unsigned support |
| | float, double | ✅ **WITH IEEE 754** | Hardware FP operations |
| | _Bool | ✅ Complete | C99 boolean type |
| | void, pointers (any level) | ✅ Complete | Full pointer arithmetic |
| | struct, union, enum, typedef | ✅ Complete | Max 16 members/struct |
| | Arrays (1-D with init) | ✅ Complete | Array initialization lists |
| | Function pointers | ✅ Complete | Full function pointer support |
| **Operators** | All arithmetic (+,-,*,/,%) | ✅ Complete | Int and float |
| | All bitwise (&,\|,^,~,<<,>>) | ✅ Complete | Integer only |
| | All comparisons (<,>,<=,>=,==,!=) | ✅ Complete | Int and float |
| | Logical (&&,\|\|,!) | ✅ Complete | Short-circuit evaluation |
| | Assignment (=, +=, -=, etc.) | ✅ Complete | 11 assignment operators |
| | Increment/decrement (++, --) | ✅ Complete | Prefix and postfix |
| | Ternary (?:), comma (,) | ✅ Complete | Full support |
| | sizeof, member access (.) | ✅ Complete | Compile-time sizeof |
| **Control Flow** | if/else, while, do-while, for | ✅ Complete | All C89 constructs |
| | switch/case/default | ✅ Complete | With fallthrough |
| | break, continue, return, goto | ✅ Complete | Labels supported |
| **Storage** | static, extern, inline | ✅ Complete | C99 storage classes |
| **Comments** | #, /* */, // | ✅ All 3 styles | C89 and C++ comments |
| **Escapes** | \x, \ooo, \a, \b, \f, \v | ✅ Complete | Extended escapes |

#### Test Coverage

- **Total tests:** 84 test files
- **C89 compliance tests:** 8 tests, 100% pass
- **C99 compliance tests:** 6 tests, 100% pass
- **Feature tests:** 70+ tests
- **Pass rate:** 100% on tested features

#### Key Strengths

1. **Direct QBE IL emission** - No AST serialization overhead
2. **Comprehensive type system** - Proper promotions and conversions
3. **Float/double support** - IEEE 754 with full conversions
4. **Mature codebase** - Battle-tested with extensive test suite
5. **Clean architecture** - Single-pass compilation with yacc

#### Known Limitations

1. **No preprocessor** - No #include, #define, #ifdef
2. **No multi-dimensional arrays** - Only 1-D arrays supported
3. **No variadic definitions** - Can call variadic functions via `...`
4. **Block-scope declarations** - Variables mostly at block start (C89 style)
5. **Limited string handling** - Basic string literals only

---

### 1.2 i8086 Backend (i8086/)

**Architecture:** QBE backend module (abi.c, isel.c, emit.c, targ.c)
**Size:** 1,206 lines total
**Output:** 16-bit x86 assembly (Intel syntax, NASM/MASM compatible)

#### Implementation Status

| Feature | Status | Implementation | Notes |
|---------|--------|----------------|-------|
| **16-bit integers** | ✅ Complete | Full ALU support | add, sub, mul, and, or, xor |
| **Division/modulo** | ✅ Complete | DX:AX handling | div, rem, udiv, urem |
| **Bit shifts** | ✅ Complete | Immediate & CL | shl, shr, sar |
| **Comparisons** | ✅ Complete | All types | Signed and unsigned |
| **Control flow** | ✅ Complete | All constructs | if, loops, switch |
| **Function calls** | ✅ Complete | Cdecl convention | Stack-based args |
| **Memory addressing** | ✅ Complete | All i8086 modes | [bx+si+disp], etc. |
| **Stack frames** | ✅ Complete | BP-based | Standard prologue/epilogue |
| **32-bit (long)** | ❌ **Incomplete** | Partial Kl ops | DX:AX pairs needed |
| **Floating-point** | ❌ **MISSING** | No 8087 support | **Critical blocker** |
| **Memory models** | ❌ **Small only** | No far pointers | Limits to 64KB segments |

#### Register Allocation

| Register | Usage | Saved By | Notes |
|----------|-------|----------|-------|
| AX | Accumulator, return | Caller | Function return value |
| BX | Base | Callee | General purpose |
| CX | Counter | Caller | Loops, shifts |
| DX | Data, high return | Caller | DX:AX for 32-bit |
| SI | Source index | Callee | String ops, general |
| DI | Dest index | Callee | String ops, general |
| BP | Base pointer | Callee | Frame pointer |
| SP | Stack pointer | - | Stack (globally live) |

#### Calling Convention: cdecl

1. Arguments pushed on stack **right-to-left**
2. Caller cleans up stack after call
3. Return values:
   - 8/16-bit: AL/AX
   - 32-bit: DX:AX (DX=high, AX=low)
4. Callee preserves: BX, SI, DI, BP
5. Caller preserves: AX, CX, DX

#### Assembly Output Format

- **Syntax:** Intel (`mov dst, src`)
- **Directives:** AT&T-style (`.text`, `.globl`, `.balign`)
- **Symbols:** Underscore prefix (`_main`, `_printf`)
- **Format:** NASM/MASM compatible

#### Supported Assemblers

- **NASM** (recommended): `nasm -f obj output.asm`
- **MASM/TASM**: May need minor syntax adjustments
- **GNU as**: With `--32` flag and preprocessing

---

## 2. Integration Analysis: The Floating-Point Problem

### 2.1 The Critical Mismatch

```
┌─────────────────────────┐         ┌─────────────────────────┐
│   MiniC Compiler        │         │   i8086 Backend         │
├─────────────────────────┤         ├─────────────────────────┤
│ float f;                │  QBE IL │                         │
│ f = 3.14 + 2.0;         │ ══════> │ ❌ Cannot handle        │
│                         │ with 's'│    float type 's'       │
│ Generates:              │ type    │    (QBE float)          │
│ %r =s add %a, %b        │         │                         │
└─────────────────────────┘         └─────────────────────────┘
         Working                              Broken
```

**Problem:** MiniC emits QBE floating-point operations (`=s` for float, `=d` for double), but the i8086 backend has no code to handle them.

**Impact:** Any C program using `float` or `double` will fail to compile to DOS, despite MiniC supporting these types perfectly.

---

### 2.2 Three Integration Strategies

#### Option 1: Disable Float in MiniC for DOS Target

**Approach:** Add a compilation mode that rejects floating-point types.

```c
// Add to MiniC
./minic -target i8086 < program.c > out.ssa
// Compiler error: "float not supported for i8086 target"
```

**Pros:**
- ✅ Immediate solution (1 day implementation)
- ✅ No backend changes needed
- ✅ Clear error messages for users
- ✅ Works with existing i8086 backend

**Cons:**
- ❌ Loses major MiniC feature (float/double)
- ❌ C89/C99 float tests would fail for DOS
- ❌ Users expect float support in C compiler
- ❌ Poor user experience (rejected valid C code)

**Recommendation:** ⚠️ **Temporary fallback only** - Use during Phase 1 development

---

#### Option 2: Software Floating-Point Emulation

**Approach:** Implement IEEE 754 operations in pure integer code.

```asm
; Software FP library (~5-10KB)
__fadd:  ; (AX:BX, CX:DX) -> AX:BX
    ; Parse mantissa, exponent
    ; Align exponents
    ; Add mantissas
    ; Normalize result
    ; Return in AX:BX
    ret

__fmul:  ; Multiply floats
__fdiv:  ; Divide floats
__fcmp:  ; Compare floats
; ... 20+ functions
```

**Pros:**
- ✅ Maintains C compatibility
- ✅ No hardware requirements (works on any 8086)
- ✅ Predictable behavior

**Cons:**
- ❌ **Very slow** (100-1000x slower than hardware)
- ❌ Large code size (5-10KB library)
- ❌ Complex to implement correctly (IEEE 754 edge cases)
- ❌ Poor DOS user experience
- ❌ Difficult to debug

**Recommendation:** ❌ **Not recommended** - Performance unacceptable for practical use

---

#### Option 3: 8087 FPU Support ⭐ **RECOMMENDED**

**Approach:** Implement hardware floating-point using 8087/80287/80387 coprocessor.

```asm
; 8087 FPU instructions (hardware acceleration)
fld  dword ptr [si]    ; Load float from memory
fadd dword ptr [di]    ; Add float (hardware FP)
fstp dword ptr [bx]    ; Store result to memory
; Fast, accurate, standard
```

**8087 Architecture:**
- **8 FP registers:** ST(0) through ST(7) (80-bit each)
- **Stack-based:** Push/pop semantics
- **IEEE 754 compliant:** Standard floating-point
- **Multiple formats:** 32-bit (float), 64-bit (double), 80-bit (long double)
- **Hardware instructions:** Fast arithmetic (1-10 cycles)

**Implementation Requirements:**

1. **FPU instruction encoding** (i8086/emit.c) - ~200 lines
2. **FP register allocator** (i8086/isel.c) - ~150 lines
3. **QBE IL mapping** (i8086/isel.c) - ~100 lines
4. **Type conversions** (int ↔ float) - ~50 lines
5. **Testing framework** - ~100 test cases

**Total effort:** ~500 lines of code, 2-3 weeks

**QBE IL to 8087 Mapping:**

| QBE Operation | 8087 Instruction | Notes |
|---------------|------------------|-------|
| `=s add` | `fadd` | Float addition |
| `=s sub` | `fsub` | Float subtraction |
| `=s mul` | `fmul` | Float multiplication |
| `=s div` | `fdiv` | Float division |
| `=s neg` | `fchs` | Change sign |
| `=w cslt` (float) | `fcom`, `fstsw` | Compare, get status |
| `=s loads` | `fld dword ptr` | Load float from memory |
| `stores` | `fstp dword ptr` | Store and pop |
| `=d load` | `fld qword ptr` | Load double |
| `stored` | `fstp qword ptr` | Store double |

**Type Conversions:**

| Conversion | 8087 Instruction | Notes |
|------------|------------------|-------|
| int → float | `fild` (integer load) | Auto-converts |
| float → int | `fistp` (integer store+pop) | Truncates |
| float → double | `fld` (promotes automatically) | 80-bit internal |
| double → float | `fstp dword` | Demotes automatically |

**Pros:**
- ✅ **Hardware speed** (1-10x faster than integer ops)
- ✅ Full IEEE 754 compliance
- ✅ Preserves all MiniC float features
- ✅ Authentic DOS experience
- ✅ Reasonable code size (~500 lines)
- ✅ Standard approach (Turbo C, Microsoft C used 8087)

**Cons:**
- ⚠️ Requires 8087/80287/80387 coprocessor hardware
- ⚠️ Stack-based FPU is complex to manage
- ⚠️ 2-3 weeks implementation time

**Hardware Compatibility:**
- 8086 + 8087 (1978-1980) - Original
- 80286 + 80287 (1982+) - ← **Common DOS target**
- 80386 + 80387 (1985+) - Common
- 80486+ (1989+) - Built-in FPU (no separate chip)
- DOSBox/86Box - Full 8087 emulation

**Recommendation:** ⭐ **Strongly recommended** - Best balance of performance, compatibility, and implementation effort

---

## 3. C11 Feature Gap Analysis

### 3.1 Current Compliance Status

**MiniC Current Coverage:**
- **C89/ANSI C:** ~95% (missing only preprocessor)
- **C99:** ~70% (missing VLAs, some library features)
- **C11:** ~30% (missing most C11-specific features)

### 3.2 C11 Feature Detailed Analysis

| C11 Feature | Status | Priority | Effort | Rationale |
|-------------|--------|----------|--------|-----------|
| **_Static_assert** | ❌ Missing | 🔥 High | Low (1 day) | Compile-time checks, very useful |
| **Compound literals** | ❌ Missing | 🔥 High | Low (2 days) | Temporary objects, common pattern |
| **Designated initializers** | ❌ Missing | 🔥 High | Medium (3 days) | Struct init by name, readability |
| **Anonymous struct/union** | ❌ Missing | 🟡 Medium | Medium (2 days) | Nested structs without names |
| **_Generic** | ❌ Missing | 🟡 Medium | High (5 days) | Type-generic macros |
| **_Alignof/_Alignas** | ❌ Missing | 🟡 Medium | Medium (3 days) | Memory alignment control |
| **Complex types** | ❌ Missing | 🟡 Medium | High (5 days) | Complex numbers (with 8087) |
| **VLAs (C99)** | ❌ Missing | 🟡 Medium | Medium (4 days) | Variable-length arrays |
| **Unicode (u"", U"")** | ❌ Missing | 🔵 Low | Medium (3 days) | Unicode string literals |
| **_Atomic** | ❌ Missing | 🔵 Low | Very High | No multithreading in DOS |
| **_Thread_local** | ❌ Missing | 🔵 Low | High | No threads in DOS |
| **Bounds-checking (Annex K)** | ❌ Missing | 🔵 Low | High | Rarely used, optional |
| **restrict keyword** | ❌ Missing | 🔵 Low | Low (1 day) | Optimization hint only |
| **inline (full C99)** | ⚠️ Partial | 🟡 Medium | Low (1 day) | Currently parsed but not enforced |

### 3.3 Realistic C11 Compliance Target

**For an 8086 DOS compiler, ~60% C11 compliance is achievable and appropriate.**

#### Phase 1: DOS-Relevant Features (High Priority)

```c
// _Static_assert - Compile-time checks
_Static_assert(sizeof(int) == 2, "int must be 2 bytes on 8086");
_Static_assert(BUFFER_SIZE > 0, "Buffer size must be positive");

// Compound literals - Temporary objects
draw_point((Point){.x=10, .y=20});
process((int[]){1, 2, 3, 4, 5}, 5);

// Designated initializers - Named struct init
struct Config cfg = {
    .width = 640,
    .height = 480,
    .color_depth = 8
};

// Anonymous struct/union - Cleaner nested types
struct Packet {
    int type;
    union {  // No name needed
        int int_value;
        float float_value;
    };
};
packet.int_value = 42;  // Direct access
```

**Estimated effort:** 8 days (1-2 weeks)

#### Phase 2: Advanced Features (Medium Priority)

```c
// _Generic - Type-generic macros
#define abs(x) _Generic((x), \
    int: abs_int, \
    float: abs_float, \
    double: abs_double)(x)

// _Alignof/_Alignas - Memory alignment
_Alignas(16) char buffer[256];
size_t align = _Alignof(double);

// Complex types (requires 8087)
double complex z = 3.0 + 4.0*I;
double real_part = creal(z);
```

**Estimated effort:** 10 days (2 weeks)

#### Excluded: Inappropriate for DOS

```c
// _Atomic - No multithreading in DOS
_Atomic int counter;  // ❌ DOS is single-threaded

// _Thread_local - No thread support
_Thread_local int tls_var;  // ❌ No threads

// Bounds-checking (Annex K) - Rarely used
strcpy_s(dst, sizeof(dst), src);  // ❌ Optional, rarely used
```

---

## 4. Complete Compilation Pipeline

### 4.1 Pipeline Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     SOURCE CODE (program.c)                             │
│  #include <stdio.h>  // Note: No preprocessor yet                       │
│  int main() {                                                           │
│      float x = 3.14;                                                    │
│      printf("Hello, DOS! x=%f\n", x);                                   │
│      return 0;                                                          │
│  }                                                                      │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  PHASE 1: C COMPILATION - MiniC Compiler                                │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Lexer (yylex)                                                    │  │
│  │  - Tokenize: keywords, identifiers, literals, operators          │  │
│  │  - Handle comments: #, /* */, //                                 │  │
│  │  - Parse escapes: \x, \ooo, \a, \b, etc.                         │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Parser (yacc LALR(1))                                            │  │
│  │  - Build AST from tokens                                          │  │
│  │  - Type checking and promotions                                   │  │
│  │  - Symbol table management                                        │  │
│  │  - Error reporting                                                │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Code Generator                                                   │  │
│  │  - Direct QBE IL emission (no separate AST walk)                 │  │
│  │  - Register allocation via temporaries (%t0, %t1, ...)           │  │
│  │  - Type conversions (int/float/pointer)                          │  │
│  │  - Control flow (labels, branches, phi nodes)                    │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
└────────────────────────────┼─────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  QBE INTERMEDIATE LANGUAGE (program.ssa)                                │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  data $str = { b "Hello, DOS! x=%f\n", b 0 }                     │  │
│  │  data $float_pi = { s 3.14 }                                     │  │
│  │                                                                   │  │
│  │  export function w $main() {                                     │  │
│  │  @start                                                          │  │
│  │      %x =s loads $float_pi       # Load float constant          │  │
│  │      %r =w call $printf(l $str, s %x, ...)                      │  │
│  │      ret 0                                                       │  │
│  │  }                                                               │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  PHASE 2: QBE OPTIMIZATION & CODE GENERATION                            │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  SSA Optimizations                                                │  │
│  │  - Copy propagation                                               │  │
│  │  - Dead code elimination (DCE)                                    │  │
│  │  - Global value numbering (GVN)                                   │  │
│  │  - Common subexpression elimination (CSE)                         │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  i8086 Target Selection                                           │  │
│  │  - Choose i8086 backend                                           │  │
│  │  - Load target-specific ABI (cdecl)                              │  │
│  │  - Initialize register allocator (6 GPRs + 8 FPRs)               │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Instruction Selection (isel.c)                                   │  │
│  │  - Map QBE IL to 8086 instructions                                │  │
│  │  - Integer ops: add→ADD, mul→MUL, div→IDIV                       │  │
│  │  - Float ops: =s add→FADD, =s mul→FMUL (8087)                    │  │
│  │  - Memory: loads→MOV, stores→MOV                                  │  │
│  │  - Control: jnz→JNZ, jmp→JMP                                     │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Register Allocation (rega.c)                                     │  │
│  │  - Assign virtual registers to physical registers                 │  │
│  │  - Spill to stack if needed                                       │  │
│  │  - Handle calling convention (caller/callee save)                 │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Assembly Emission (emit.c)                                       │  │
│  │  - Generate Intel syntax assembly                                 │  │
│  │  - Function prologue/epilogue (PUSH BP, MOV BP,SP)               │  │
│  │  - cdecl calling convention                                       │  │
│  │  - Data section emission                                          │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
└────────────────────────────┼─────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  16-BIT x86 ASSEMBLY (program.asm)                                      │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  .data                                                            │  │
│  │  str: db "Hello, DOS! x=%f", 10, 0                               │  │
│  │  float_pi: dd 3.14                                               │  │
│  │                                                                   │  │
│  │  .text                                                            │  │
│  │  .globl _main                                                     │  │
│  │  _main:                                                           │  │
│  │      push bp              ; Prologue                             │  │
│  │      mov bp, sp                                                   │  │
│  │      fld dword [float_pi] ; Load float (8087)                    │  │
│  │      fsub sp, 4           ; Make space for float arg             │  │
│  │      fstp dword [sp]      ; Push float to stack                  │  │
│  │      push offset str      ; Push format string                   │  │
│  │      call _printf         ; Call printf                          │  │
│  │      add sp, 6            ; Clean up (4+2 bytes)                 │  │
│  │      xor ax, ax           ; Return 0                             │  │
│  │      mov sp, bp           ; Epilogue                             │  │
│  │      pop bp                                                       │  │
│  │      ret                                                          │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  PHASE 3: ASSEMBLY & LINKING                                            │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  NASM Assembler                                                   │  │
│  │  $ nasm -f obj program.asm -o program.obj                        │  │
│  │  - Parse assembly source                                          │  │
│  │  - Generate OMF (Object Module Format) object file               │  │
│  │  - Create symbol table                                            │  │
│  │  - Generate relocation entries                                    │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  OMF Object File (program.obj)                                    │  │
│  │  - Relocatable machine code                                       │  │
│  │  - Symbol table (functions, variables)                            │  │
│  │  - External references (_printf)                                  │  │
│  │  - Public symbols (_main)                                         │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
│                          ▼                                               │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  OpenWatcom Linker (wlink) or Turbo Link                         │  │
│  │  $ wlink system dos file program.obj,crt0.obj,libc.lib \         │  │
│  │          name program.exe                                         │  │
│  │  - Link multiple object files                                     │  │
│  │  - Resolve external symbols                                       │  │
│  │  - Apply relocations                                              │  │
│  │  - Create DOS executable (MZ format)                              │  │
│  └───────────────────────┬───────────────────────────────────────────┘  │
└────────────────────────────┼─────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  DOS EXECUTABLE (program.exe)                                           │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  MZ Header (DOS EXE format)                                       │  │
│  │  - Magic number: 0x5A4D ("MZ")                                    │  │
│  │  - File size, header size                                         │  │
│  │  - Initial CS:IP, SS:SP                                           │  │
│  │  - Relocation table                                               │  │
│  │                                                                   │  │
│  │  Code Segment                                                     │  │
│  │  - 16-bit machine code (8086 instructions)                       │  │
│  │  - _main, _printf, other functions                               │  │
│  │                                                                   │  │
│  │  Data Segment                                                     │  │
│  │  - Initialized data (strings, constants)                         │  │
│  │  - Uninitialized data (BSS)                                      │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  PHASE 4: EXECUTION                                                     │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  DOS Environment                                                  │  │
│  │  (DOSBox, 86Box, or real DOS 6.22)                               │  │
│  │                                                                   │  │
│  │  C:\> program.exe                                                 │  │
│  │  Hello, DOS! x=3.140000                                          │  │
│  │  C:\>                                                             │  │
│  │                                                                   │  │
│  │  Hardware:                                                        │  │
│  │  - 8086/80286/80386 CPU (16-bit mode)                            │  │
│  │  - 8087/80287/80387 FPU (for float support)                      │  │
│  │  - 640KB conventional memory                                      │  │
│  │  - DOS INT 21h services                                           │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Data Flow Summary

```
C Source (program.c)
    ↓ [MiniC Compiler]
QBE IL (program.ssa)
    ↓ [QBE + i8086 Backend]
x86 Assembly (program.asm)
    ↓ [NASM Assembler]
OMF Object (program.obj)
    ↓ [OpenWatcom Linker + CRT + LibC]
DOS Executable (program.exe)
    ↓ [DOSBox/Real DOS]
Running Program
```

---

## 5. Recommended Architecture

### 5.1 File Organization

```
qbe/
├── minic/                       # MiniC compiler frontend
│   ├── minic.y                  # Main compiler (yacc + codegen)
│   ├── Makefile                 # Build MiniC
│   ├── test/                    # Test suite (84 existing files)
│   │   ├── simple_float.c
│   │   ├── struct_test.c
│   │   ├── compliance/          # C89/C99/C11 tests
│   │   └── ...
│   └── dos/                     # NEW: DOS-specific additions
│       ├── dos_runtime.c        # DOS C runtime (printf, file I/O)
│       ├── dos_runtime.h
│       ├── dos_int.h            # DOS interrupt interface
│       ├── crt0.asm             # DOS startup code (_start)
│       ├── libc.c               # Basic C library functions
│       └── examples/            # DOS example programs
│           ├── hello.c          # Hello world
│           ├── mandelbrot.c     # Mandelbrot set renderer
│           ├── life.c           # Conway's Game of Life
│           ├── snake.c          # Snake game
│           └── tsr_example.c    # TSR demonstration
│
├── i8086/                       # i8086 backend
│   ├── all.h                    # Register definitions, forward decls
│   ├── targ.c                   # Target registration (53 lines)
│   ├── abi.c                    # Cdecl calling convention (306 lines)
│   ├── isel.c                   # Instruction selection (217 lines)
│   ├── emit.c                   # Assembly emission (578 lines)
│   └── fpu.c                    # NEW: 8087 FPU support (~500 lines)
│       # Functions:
│       # - i8086_selfpu() - FPU instruction selection
│       # - emitfpu() - FPU instruction emission
│       # - fpualloc() - FP register allocation
│       # - fpuconv() - Float/int conversions
│
├── docs/                        # Documentation
│   ├── C11_8086_ARCHITECTURE.md # This document
│   ├── ROADMAP.md               # Implementation roadmap
│   ├── I8086_TARGET.md          # i8086 backend reference
│   ├── NEW_FEATURES_DOCUMENTATION.md  # MiniC features
│   └── DOS_RUNTIME_API.md       # NEW: DOS API documentation
│
├── tools/                       # Build and test scripts
│   ├── build-dos.sh             # NEW: Complete build script
│   ├── test-dos.sh              # NEW: DOS test runner (DOSBox)
│   ├── dosbox-test.conf         # NEW: DOSBox automation config
│   └── setup-toolchain.sh       # NEW: Install NASM, OpenWatcom
│
├── Makefile                     # Build QBE with i8086
├── README.md                    # Main project README
└── claude.md                    # Claude session status
```

---

## 6. Testing Strategy

### 6.1 Test Pyramid

```
             ┌─────────────────────────┐
             │  Integration Tests      │
             │  (Full DOS Programs)    │  10 tests
             │  - DOSBox execution     │
             │  - Real hardware        │
             └───────────┬─────────────┘
                         │
             ┌───────────▼─────────────┐
             │  Functional Tests       │
             │  (Language Features)    │  50 tests
             │  - QBE IL validation    │
             │  - Assembly check       │
             └───────────┬─────────────┘
                         │
             ┌───────────▼─────────────┐
             │  Unit Tests             │
             │  (Operators, Types)     │  200+ tests
             │  - MiniC output check   │
             │  - Type system          │
             └─────────────────────────┘
```

### 6.2 Test Categories

#### Unit Tests (200+ tests)
- **Type system:** int, char, short, long, float, double, pointers
- **Operators:** All arithmetic, bitwise, logical, assignment
- **Control flow:** if, while, for, switch, goto
- **Data structures:** struct, union, enum, arrays
- **Storage classes:** static, extern
- **Edge cases:** Overflow, underflow, type conversions

#### Functional Tests (50 tests)
- **C89 compliance:** 8 tests (existing, all pass)
- **C99 compliance:** 6 tests (existing, all pass)
- **C11 compliance:** 10 tests (new, to be added)
- **DOS-specific:** 10 tests (interrupts, video, etc.)
- **Floating-point:** 16 tests (8087 operations)

#### Integration Tests (10 tests)
- **Hello World:** Basic printf
- **Mandelbrot:** Float arithmetic + VGA graphics
- **Conway's Life:** 2D arrays + video memory
- **Snake Game:** Keyboard input + timer interrupts
- **File I/O:** DOS file operations
- **TSR Example:** Terminate-Stay-Resident program
- **Memory Demo:** Malloc/free implementation
- **Math Library:** sin, cos, sqrt with 8087
- **Command Args:** argc/argv parsing
- **Multi-file:** Linking multiple objects

### 6.3 DOS Testing Environment

#### Option 1: DOSBox (Recommended for CI/CD)

```bash
#!/bin/bash
# test-dos.sh - Automated DOSBox testing

DOSBOX="dosbox"
TEST_DIR="minic/dos/examples"

for test in $TEST_DIR/*.c; do
    name=$(basename "$test" .c)
    echo "Testing $name..."

    # Compile
    ./minic < "$test" > test.ssa
    ./qbe -t i8086 test.ssa > test.asm
    nasm -f obj test.asm -o test.obj
    wlink system dos file test.obj name test.exe

    # Run in DOSBox
    $DOSBOX -c "test.exe" -c "exit" > output.txt 2>&1

    # Check result
    if grep -q "PASS" output.txt; then
        echo "✓ $name passed"
    else
        echo "✗ $name failed"
        cat output.txt
    fi
done
```

**Pros:**
- ✅ Free and open-source
- ✅ Available on all platforms (Linux, Mac, Windows)
- ✅ Easy to automate (command-line options)
- ✅ Good 8086/8087 emulation
- ✅ Fast execution

**Cons:**
- ⚠️ Not cycle-accurate
- ⚠️ Some hardware quirks missing

#### Option 2: 86Box (Most Accurate)

**Pros:**
- ✅ Cycle-accurate 8086/80286/80386 emulation
- ✅ Accurate 8087/80287/80387 FPU emulation
- ✅ Accurate video/sound hardware
- ✅ Boot real DOS (DOS 6.22, FreeDOS)

**Cons:**
- ⚠️ Slower than DOSBox
- ⚠️ Harder to automate (GUI-based)
- ⚠️ More complex setup

**Use case:** Final validation, debugging hardware-specific issues

#### Option 3: Real Hardware (Optional)

**Target systems:**
- 80286 + 80287 FPU (IBM AT-compatible)
- 80386 + 80387 FPU
- DOS 6.22 or FreeDOS

**Use case:** Final certification, demonstration

### 6.4 Continuous Integration

```yaml
# .github/workflows/qbe-dos.yml
name: QBE DOS Build

on: [push, pull_request]

jobs:
  test-dos:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y yacc nasm dosbox
          # Install OpenWatcom
          wget https://github.com/open-watcom/open-watcom-v2/releases/...

      - name: Build QBE
        run: make

      - name: Build MiniC
        run: cd minic && make

      - name: Run DOS tests
        run: ./tools/test-dos.sh

      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: dos-executables
          path: '*.exe'
```

---

## 7. Risk Assessment

| Risk | Impact | Likelihood | Mitigation Strategy |
|------|--------|------------|---------------------|
| **8087 FPU complexity** | High | Medium | Incremental implementation: start with simple ops (add/sub), test each op individually, use 86Box for cycle-accurate testing |
| **QBE IL incompatibility** | High | Low | QBE IL is stable and well-documented; unlikely to change; MiniC already generates valid QBE IL |
| **DOS toolchain issues** | Medium | Medium | Use maintained tools (OpenWatcom still updated, NASM actively maintained); provide fallback instructions for Turbo Link |
| **Memory model bugs** | Medium | High | Start with small model only (simplest); document limitations clearly; add other models later if needed |
| **Testing difficulty** | Medium | High | Automate with DOSBox; provide clear test scripts; document manual testing procedures |
| **32-bit long support** | Low | Medium | Implement DX:AX register pairs carefully; test division/modulo extensively; document calling convention |
| **Register allocation** | Low | Low | QBE's register allocator is proven; i8086 has 6 usable GPRs (adequate) |
| **Calling convention bugs** | Medium | Low | cdecl is well-documented; follow DOS conventions exactly; test with multiple compilers |
| **8087 not available** | Low | Low | DOSBox/86Box support 8087 emulation; real hardware from 80286+ era had coprocessors; provide software fallback option |

### Risk Mitigation Timeline

**Week 1-2 (Phase 0-1):**
- Low risk - integer-only pipeline
- Mitigate toolchain issues early

**Week 3-5 (Phase 2):**
- High risk - 8087 implementation
- Mitigation: Incremental testing, one instruction at a time

**Week 6-8 (Phase 3):**
- Medium risk - DOS integration
- Mitigation: Use existing libraries as reference (Turbo C source)

**Week 9-12 (Phase 4):**
- Low risk - C11 features
- Mitigation: Each feature is independent

---

## 8. Success Metrics

### 8.1 Phase 1 Success Criteria (Integer-Only)

**Quantitative:**
- [ ] 50+ MiniC integer tests compile and run on DOS
- [ ] Build scripts work end-to-end (0 manual steps)
- [ ] All C89 integer features work (int, char, short, long, pointers)

**Qualitative:**
- [ ] Hello World compiles in <5 seconds
- [ ] Clear error messages on compilation failure
- [ ] Documentation complete (build instructions)

**Demonstration:**
- [ ] Mandelbrot renderer (integer-only version) runs
- [ ] Conway's Game of Life runs in DOSBox
- [ ] File I/O example reads/writes files

### 8.2 Phase 2 Success Criteria (Floating-Point)

**Quantitative:**
- [ ] All 84 MiniC tests pass (including float/double)
- [ ] 16 FPU-specific tests pass
- [ ] 8087 operations within 10% of expected performance

**Qualitative:**
- [ ] FP arithmetic produces correct results (IEEE 754)
- [ ] FP comparisons work correctly
- [ ] No FP stack overflows/underflows

**Demonstration:**
- [ ] Mandelbrot renderer (float version) produces correct image
- [ ] Ray tracer demo renders 3D scene
- [ ] Scientific calculator with transcendental functions

### 8.3 Phase 3 Success Criteria (DOS Integration)

**Quantitative:**
- [ ] Complete DOS API library (20+ functions)
- [ ] 10+ example programs work
- [ ] All memory models implemented (tiny, small, medium)

**Qualitative:**
- [ ] DOS programs feel native (fast, responsive)
- [ ] No memory leaks or corruption
- [ ] Professional documentation

**Demonstration:**
- [ ] TSR (Terminate-Stay-Resident) program works
- [ ] Graphics demo (VGA mode 13h) works
- [ ] Mouse input works (INT 33h)
- [ ] Multi-file project compiles and links

### 8.4 Phase 4 Success Criteria (C11 Compliance)

**Quantitative:**
- [ ] C11 compliance: 60%+ (measured by test suite)
- [ ] All target C11 features implemented (8 features)
- [ ] 20+ C11-specific tests pass

**Qualitative:**
- [ ] Code readability improved (designated initializers, compound literals)
- [ ] Compile-time safety improved (_Static_assert)
- [ ] Type-generic macros work (_Generic)

**Demonstration:**
- [ ] Feature parity with Turbo C 2.0 on relevant features
- [ ] Complex number library works
- [ ] All example programs use modern C11 idioms

### 8.5 Overall Success Metrics

**Final Goal:** Production-quality C89/C99/C11-subset compiler for DOS

**Measured by:**
1. **Feature completeness:** 95% C89, 70% C99, 60% C11
2. **Test coverage:** 200+ tests, 95%+ pass rate
3. **Performance:** Within 2x of Turbo C 2.0
4. **Usability:** Complete documentation, clear error messages
5. **Reliability:** No crashes on valid input
6. **Community adoption:** Used by retro computing enthusiasts

---

## 9. Conclusion

### 9.1 Key Findings

1. ✅ **MiniC is excellent** - Far more advanced than initially expected
   - 2,276 lines of well-structured yacc code
   - Complete C89/C99 feature set
   - Float/double with IEEE 754
   - 84 tests, 100% pass rate

2. ✅ **i8086 backend is solid** - Just missing FPU support
   - 1,206 lines of clean, modular code
   - Full integer support with proper cdecl
   - Good register allocation
   - Works with standard DOS toolchain

3. ❌ **No c2qbe exists** - Resume prompt was based on incorrect information
   - No separate DOS-focused compiler
   - No merge decision needed
   - No conflicting architectures

4. 🎯 **Real task is simpler** - Connect existing components + add FPU
   - MiniC → QBE → i8086 pipeline mostly works
   - Only blocker: 8087 FPU support (~500 lines)
   - C11 features: Independent additions (2-3 weeks)

### 9.2 Recommended Implementation Path

```
┌─────────────────────────────────────────────────────────┐
│ TIMELINE: 10-12 Weeks to Production Release            │
└─────────────────────────────────────────────────────────┘

Week 1-2:  ✅ Phase 0-1: Integer-only pipeline
           - Validate MiniC → QBE → i8086 → DOS
           - Create DOS runtime (printf, file I/O)
           - Build and test scripts
           - Documentation

Week 3-5:  🔧 Phase 2: 8087 FPU Support
           - Implement FPU instruction selection (~200 lines)
           - Add FPU register allocation (~150 lines)
           - Map QBE IL to 8087 ops (~100 lines)
           - Type conversions (int ↔ float) (~50 lines)
           - Test each operation incrementally

Week 6:    🧪 Testing and Bug Fixes
           - Port all 84 MiniC tests to DOS
           - Fix FPU edge cases
           - Performance tuning
           - Documentation updates

Week 7-8:  🖥️  Phase 3: DOS Integration
           - Multiple memory models (tiny, small, medium)
           - Complete DOS API library (interrupts, video, etc.)
           - 10+ example programs
           - Professional documentation

Week 9-12: 🚀 Phase 4: C11 Features
           - _Static_assert (1 day)
           - Compound literals (2 days)
           - Designated initializers (3 days)
           - Anonymous struct/union (2 days)
           - _Generic (5 days)
           - _Alignof/_Alignas (3 days)
           - Polish and documentation
```

### 9.3 Expected Outcome

**A production-quality C89/C99/C11-subset compiler for DOS** with:

✅ **Complete language support:**
- All C89 features (95% coverage)
- Most C99 features (70% coverage)
- Key C11 features (60% coverage)
- Float/double with 8087 FPU
- Function pointers
- Struct/union/enum/typedef
- Arrays with initialization

✅ **Professional DOS integration:**
- Complete C runtime library
- DOS interrupt interface (INT 21h, INT 10h, INT 33h, etc.)
- Video memory access (VGA)
- File I/O
- Memory allocation
- Command-line argument parsing

✅ **Excellent developer experience:**
- Fast compilation (<5 seconds for typical programs)
- Clear error messages
- Complete documentation
- 20+ example programs
- Automated testing with DOSBox

✅ **High-quality codebase:**
- 200+ tests, 95%+ pass rate
- Clean, modular architecture
- Well-documented code
- Easy to extend

**This will be one of the most advanced open-source C compilers for DOS/8086,** comparable to or exceeding Turbo C 2.0 in features while using modern compiler technology (SSA, proper optimization passes).

### 9.4 Next Steps

**Immediate actions:**

1. **Build QBE and validate integer pipeline** (1-2 days)
2. **Create DOS runtime library** (2-3 days)
3. **Write build and test scripts** (1 day)
4. **Begin 8087 FPU implementation** (Week 3)

**Decision points:**

- **FPU strategy:** Hardware 8087 (recommended) vs. software emulation
- **Memory models:** Small only (MVP) vs. all models (complete)
- **C11 priority:** Which features first? (Recommend: _Static_assert, compound literals)

**Resources needed:**

- NASM assembler (free)
- OpenWatcom linker (free, open-source)
- DOSBox (free, for testing)
- 86Box (optional, for accurate testing)
- Time: 10-12 weeks for complete implementation

---

## Appendix A: Quick Reference

### A.1 Build Commands

```bash
# Build QBE with i8086 backend
make

# Build MiniC compiler
cd minic && make

# Compile C program to DOS executable
./minic < program.c > program.ssa
./qbe -t i8086 program.ssa > program.asm
nasm -f obj program.asm -o program.obj
wlink system dos file program.obj,crt0.obj,libc.lib name program.exe

# Test in DOSBox
dosbox -c "program.exe"
```

### A.2 File Extensions

- `.c` - C source code
- `.ssa` - QBE Intermediate Language
- `.asm` - 16-bit x86 assembly
- `.obj` - OMF object file
- `.exe` - DOS executable (MZ format)
- `.com` - DOS executable (tiny model)

### A.3 Key Files

| File | Purpose | Lines |
|------|---------|-------|
| `minic/minic.y` | MiniC compiler | 2,276 |
| `i8086/abi.c` | Calling convention | 306 |
| `i8086/isel.c` | Instruction selection | 217 |
| `i8086/emit.c` | Assembly emission | 578 |
| `i8086/fpu.c` | 8087 FPU support | ~500 (NEW) |
| `minic/dos/dos_runtime.c` | DOS C runtime | ~300 (NEW) |
| `minic/dos/crt0.asm` | DOS startup code | ~100 (NEW) |

### A.4 Test Counts

- Unit tests: 200+ (existing + new)
- Functional tests: 50 (existing + new)
- Integration tests: 10 (new)
- Total: 260+ tests

---

**Document Version:** 1.0
**Last Updated:** 2025-11-21
**Status:** Ready for Implementation
**Next Review:** After Phase 1 completion
