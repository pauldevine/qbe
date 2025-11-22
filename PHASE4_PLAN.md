# Phase 4: C11 Feature Implementation - PLAN

**Date:** 2025-11-22
**Status:** 🚀 **STARTING** - C11 compliance target: 60%
**Dependencies:** Phases 0-3 complete

## Overview

Phase 4 adds C11 language features to the MiniC compiler, targeting ~60% C11 compliance with features most relevant for DOS development. This phase focuses on compile-time improvements and developer experience rather than runtime features.

## Current Status

**Baseline (from C11_8086_ARCHITECTURE.md):**
- **C89/ANSI C:** ~95% (missing only preprocessor)
- **C99:** ~70% (missing VLAs, some library features)
- **C11:** ~30% (missing most C11-specific features)

**Target after Phase 4:**
- **C11:** ~60% (add 8 high/medium priority features)

## Priority Features (Recommended Order)

### High Priority (Week 1-2)

| Feature | Effort | Impact | Use Cases |
|---------|--------|--------|-----------|
| **_Static_assert** | 1 day | High | Compile-time validation, platform checks |
| **Compound literals** | 2 days | High | Temporary objects, cleaner function calls |
| **Designated initializers** | 3 days | High | Named struct initialization, readability |
| **Anonymous struct/union** | 2 days | Medium | Nested types without extra names |

**Total:** 8 days (1-2 weeks)

### Medium Priority (Week 3-4)

| Feature | Effort | Impact | Use Cases |
|---------|--------|--------|-----------|
| **_Generic** | 5 days | Medium | Type-generic macros (like C++ templates lite) |
| **_Alignof/_Alignas** | 3 days | Medium | Memory alignment control for hardware |

**Total:** 8 days (1-2 weeks)

### Low Priority (Optional/Future)

| Feature | Effort | Why Low Priority |
|---------|--------|------------------|
| **restrict keyword** | 1 day | Optimization hint only, no DOS benefit |
| **Unicode (u"", U"")** | 3 days | Not useful for DOS (ASCII-based) |
| **Complex types** | 5 days | Requires 8087 library, niche use |
| **VLAs (C99)** | 4 days | Stack allocation complexity |
| **_Atomic** | Very High | No multithreading in DOS |
| **_Thread_local** | High | No threads in DOS |
| **Bounds-checking (Annex K)** | High | Rarely used, optional feature |

## Feature Specifications

### 1. _Static_assert (Compile-Time Assertions)

**Syntax:**
```c
_Static_assert(constant-expression, string-literal);
```

**Examples:**
```c
_Static_assert(sizeof(int) == 2, "int must be 2 bytes on 8086");
_Static_assert(sizeof(void*) == 2, "pointers must be 2 bytes");
_Static_assert(BUFFER_SIZE > 0, "Buffer size must be positive");
_Static_assert(MAX_USERS <= 256, "Too many users for 8-bit counter");
```

**Implementation:**
- Add keyword recognition in lexer
- Parse in declaration context
- Evaluate constant expression at compile time
- Emit error with message if false
- No runtime code generated

**Files to modify:**
- `minic/minic.y`: Add `TSTATIC_ASSERT` token, parsing rules

**Effort:** 1 day

---

### 2. Compound Literals (Temporary Objects)

**Syntax:**
```c
(type-name){ initializer-list }
```

**Examples:**
```c
// Function calls with temporary structs
draw_point((Point){.x=10, .y=20});
draw_rect((Rect){{0, 0}, {100, 100}});

// Temporary arrays
process_data((int[]){1, 2, 3, 4, 5}, 5);
sort((double[]){3.14, 2.71, 1.41}, 3);

// Assignment
Point *p = &(Point){100, 200};  // Lifetime of enclosing scope
```

**Implementation:**
- Parse compound literal syntax after cast
- Create anonymous temporary variable
- Generate initialization code
- Return address of temporary
- Lifetime: enclosing block scope

**Files to modify:**
- `minic/minic.y`: Add compound literal parsing in cast/unary context
- Temporary variable allocation

**Effort:** 2 days

---

### 3. Designated Initializers (Named Initialization)

**Syntax:**
```c
struct-initializer: .member-name = value
array-initializer: [index] = value
```

**Examples:**
```c
// Struct designated initializers
struct Config cfg = {
    .width = 640,
    .height = 480,
    .color_depth = 8,
    .vsync = 1
};

// Can skip members (zero-initialized)
struct Point p = { .x = 10 };  // .y is 0

// Out of order allowed
struct RGB color = {
    .b = 255,
    .r = 128,
    .g = 0
};

// Array designated initializers
int sparse[100] = {
    [0] = 1,
    [10] = 2,
    [99] = 3
};

// Mixed with positional
int data[] = {
    1, 2, 3,
    [10] = 10,
    11, 12  // Continue from index 11, 12
};
```

**Implementation:**
- Parse `.member` and `[index]` in initializer lists
- Track which members/indices have been initialized
- Allow out-of-order initialization
- Zero-fill uninitialized members/elements
- Update current position after each designated init

**Files to modify:**
- `minic/minic.y`: Extend initializer parsing
- Member/index tracking during initialization

**Effort:** 3 days

---

### 4. Anonymous Struct/Union (Nested Without Names)

**Syntax:**
```c
struct outer {
    int common;
    union {          // Anonymous union - no name
        int int_val;
        float float_val;
    };
};
```

**Examples:**
```c
// Anonymous union for variant types
struct Variant {
    enum { INT, FLOAT, STRING } type;
    union {  // No name needed
        int i;
        float f;
        char *s;
    };
};

// Access directly (no .u.i)
struct Variant v;
v.type = INT;
v.i = 42;  // Direct access, not v.u.i

// Anonymous struct
struct Message {
    int msg_type;
    struct {  // Anonymous struct
        int x, y;
    };
};

struct Message msg;
msg.x = 10;  // Direct access
msg.y = 20;
```

**Implementation:**
- Detect anonymous struct/union (no identifier after `}`)
- Hoist members into parent scope
- Generate direct member access (no intermediate name)
- Track member offsets correctly

**Files to modify:**
- `minic/minic.y`: Struct/union member hoisting
- Symbol table for direct member access

**Effort:** 2 days

---

### 5. _Generic (Type-Generic Macros)

**Syntax:**
```c
_Generic(expression, type1: expr1, type2: expr2, default: expr-default)
```

**Examples:**
```c
// Type-generic absolute value
#define abs(x) _Generic((x), \
    int: abs_int, \
    long: abs_long, \
    float: abs_float, \
    double: abs_double \
)(x)

int abs_int(int x) { return x < 0 ? -x : x; }
float abs_float(float x) { return x < 0.0 ? -x : x; }

int n = abs(-5);      // Calls abs_int
float f = abs(-3.14); // Calls abs_float

// Type-generic printf format
#define print_num(x) _Generic((x), \
    int: printf("%d\n", x), \
    float: printf("%f\n", x), \
    char*: printf("%s\n", x) \
)

print_num(42);       // Prints integer
print_num(3.14f);    // Prints float
print_num("hello");  // Prints string
```

**Implementation:**
- Parse `_Generic` keyword
- Evaluate controlling expression type (not value)
- Match against type associations
- Select matching expression
- Type check at compile time

**Files to modify:**
- `minic/minic.y`: Add `_Generic` expression parsing
- Type matching logic

**Effort:** 5 days (most complex)

**Note:** Requires preprocessor support for macro definitions. May need workarounds if preprocessor not available.

---

### 6. _Alignof/_Alignas (Alignment Control)

**Syntax:**
```c
_Alignof(type-name)
_Alignas(type-name or constant)
```

**Examples:**
```c
// Query alignment requirements
size_t int_align = _Alignof(int);        // Usually 2 on 8086
size_t ptr_align = _Alignof(void*);      // Usually 2 on 8086
size_t struct_align = _Alignof(struct MyStruct);

// Align variable
_Alignas(16) char buffer[256];  // 16-byte aligned buffer

// Align to type
_Alignas(double) char double_storage[8];

// Struct member alignment
struct Packet {
    char type;
    _Alignas(4) int data;  // Force 4-byte alignment
};

// For DMA, video memory access
_Alignas(4096) char dma_buffer[8192];  // Page-aligned
```

**Implementation:**
- `_Alignof`: Return compile-time alignment requirement
- `_Alignas`: Add padding to achieve specified alignment
- Track alignment in symbol table
- Emit assembly alignment directives

**Files to modify:**
- `minic/minic.y`: Add alignment keywords and expressions
- Code generation for aligned variables

**Effort:** 3 days

---

## Implementation Strategy

### Week 1: Foundation Features

**Days 1-2: _Static_assert**
- Add keyword to lexer
- Parse assertion statements
- Implement constant expression evaluator
- Error reporting with custom message
- Test with platform checks

**Days 3-4: Compound Literals**
- Parse compound literal syntax
- Anonymous temporary generation
- Lifetime management (block scope)
- Test with function calls

**Days 5-7: Designated Initializers**
- Parse `.member` and `[index]` syntax
- Out-of-order initialization
- Mixed positional/designated
- Zero-fill tracking
- Test with structs and arrays

**Day 8: Anonymous Struct/Union**
- Parse anonymous definitions
- Member hoisting logic
- Direct member access
- Test nested variants

### Week 2: Advanced Features

**Days 9-13: _Generic**
- Parse `_Generic` expressions
- Type matching without evaluation
- Expression selection
- Test type dispatch

**Days 14-16: _Alignof/_Alignas**
- Alignment query implementation
- Variable alignment control
- Assembly directive emission
- Test hardware access patterns

## Testing Strategy

### Unit Tests (Per Feature)

Each feature needs:
- **Basic tests:** Simple valid usage
- **Edge cases:** Corner cases, unusual combinations
- **Error tests:** Invalid syntax, should produce errors
- **Interaction tests:** Feature interactions

### Test File Naming

```
minic/test/c11/
├── static_assert_basic.c
├── static_assert_error.c
├── compound_literals.c
├── designated_init_struct.c
├── designated_init_array.c
├── designated_init_mixed.c
├── anonymous_union.c
├── anonymous_struct.c
├── generic_dispatch.c
├── alignof_query.c
└── alignas_variables.c
```

### Integration Tests

Create DOS programs using multiple C11 features:
- **config_demo.c** - Uses designated initializers for configuration
- **variant_types.c** - Uses anonymous unions for variant types
- **hardware_access.c** - Uses alignment for DMA/video memory
- **generic_math.c** - Uses _Generic for type-generic functions

## Success Criteria

### Quantitative

- [ ] All 8 features implemented and working
- [ ] 20+ C11-specific tests pass
- [ ] No regressions in existing MiniC tests (84 tests still pass)
- [ ] C11 compliance: 60%+ (measured by feature coverage)

### Qualitative

- [ ] Code readability improved with new features
- [ ] Compile-time safety improved (_Static_assert)
- [ ] DOS hardware access easier (_Alignof/_Alignas)
- [ ] Type-generic code possible (_Generic)
- [ ] Documentation complete

### Demonstration Programs

- [ ] Configuration system using designated initializers
- [ ] Variant type system using anonymous unions
- [ ] Hardware driver using alignment features
- [ ] Math library using _Generic dispatch
- [ ] All programs compile and run in DOSBox

## Known Limitations

### No Preprocessor

MiniC doesn't have a full preprocessor, which limits:
- **_Generic macros:** Can't define as macros without #define
- **Workaround:** Use _Generic in source directly
- **Alternative:** Implement minimal #define for _Generic patterns

### Features NOT Implementing (Out of Scope)

- ❌ **_Atomic** - No multithreading in DOS
- ❌ **_Thread_local** - No threads in DOS
- ❌ **Bounds-checking (Annex K)** - Rarely used, optional
- ❌ **VLAs** - Complex stack allocation
- ❌ **Complex types** - Requires math library
- ❌ **Unicode strings** - DOS is ASCII-based

These features are inappropriate for DOS or too complex for current scope.

## Documentation Deliverables

1. **PHASE4_PLAN.md** - This file
2. **PHASE4_STATUS.md** - Progress tracking
3. **PHASE4_COMPLETE.md** - Final documentation
4. **C11_FEATURES.md** - User guide for C11 features
5. **Updated README** - Mention C11 support

## Timeline

**Week 1 (Days 1-8):**
- _Static_assert ✅
- Compound literals ✅
- Designated initializers ✅
- Anonymous struct/union ✅

**Week 2 (Days 9-16):**
- _Generic ✅
- _Alignof/_Alignas ✅

**Week 3 (Optional):**
- Additional testing
- Documentation polish
- Integration examples
- Performance optimization

**Total:** 2-3 weeks for 60% C11 compliance

## Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **Parser complexity** | High | Medium | Incremental implementation, test each feature independently |
| **No preprocessor** | Medium | High | Implement workarounds, document limitations |
| **QBE IL compatibility** | Low | Low | Most features are compile-time only |
| **Testing coverage** | Medium | Medium | Create comprehensive test suite early |
| **Feature interactions** | Medium | Medium | Test combinations, document edge cases |

## Next Steps

1. Create `PHASE4_STATUS.md` for tracking
2. Start with `_Static_assert` (easiest, 1 day)
3. Add test cases incrementally
4. Document each feature as implemented
5. Create integration examples

---

**Ready to begin Phase 4 implementation!**

**Project Status:**
- **Phase 0:** ✅ 100% COMPLETE - Toolchain setup
- **Phase 1:** ✅ 100% COMPLETE - Integer pipeline
- **Phase 2:** ✅ 100% COMPLETE - 8087 FPU support
- **Phase 3:** ✅ 100% COMPLETE - DOS integration
- **Phase 4:** 🚀 **STARTING** - C11 features (target: 60% compliance)
