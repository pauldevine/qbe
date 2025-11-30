# chibicc Preprocessor Adaptation Plan
**MiniC Integration Strategy - Week 1 Analysis**

## Executive Summary

After studying chibicc's source code (tokenize.c: 805 lines, preprocess.c: 1,208 lines, chibicc.h: 457 lines), I've identified a clear adaptation strategy. chibicc's preprocessor is exceptionally well-designed and can be adapted to MiniC with minimal changes.

**Key Insight:** chibicc's tokenizer and preprocessor are already well-separated from the parser, making adaptation straightforward.

---

## Architecture Overview

### chibicc's Preprocessing Pipeline
```
File (source) → tokenize() → Token stream → preprocess() → Token stream → parse()
```

### MiniC's New Pipeline
```
File (source) → tokenize() → Token stream → preprocess() → yylex_adapter() → yacc parser
                (adapted)                     (adapted)      (NEW bridge)     (unchanged)
```

---

## Critical Data Structures

### 1. Token Structure (chibicc.h:73-92)

**chibicc version:**
```c
typedef enum {
  TK_IDENT,   // Identifiers
  TK_PUNCT,   // Punctuators
  TK_KEYWORD, // Keywords
  TK_STR,     // String literals
  TK_NUM,     // Numeric literals
  TK_PP_NUM,  // Preprocessing numbers
  TK_EOF,     // End-of-file
} TokenKind;

typedef struct Token Token;
struct Token {
  TokenKind kind;
  Token *next;
  int64_t val;         // Numeric value
  long double fval;    // Float value
  char *loc;           // Source location
  int len;             // Token length
  Type *ty;            // Type (we'll omit)
  char *str;           // String content

  File *file;          // Source file
  char *filename;      // Filename
  int line_no;         // Line number
  int line_delta;      // For #line
  bool at_bol;         // At beginning of line
  bool has_space;      // Has leading space
  Hideset *hideset;    // For macro expansion
  Token *origin;       // Original token if expanded
};
```

**MiniC adaptation:**
- **Keep:** All fields except `Type *ty` (MiniC doesn't need type during tokenization)
- **Simplify:** Use `long` instead of `int64_t` for 32-bit compatibility
- **Location:** `minic/minic_token.h`

### 2. File Structure (chibicc.h:62-70)

```c
typedef struct {
  char *name;
  int file_no;
  char *contents;      // Entire file contents
  char *display_name;  // For #line directive
  int line_delta;
} File;
```

**MiniC adaptation:**
- **Keep:** All fields
- **Note:** chibicc reads entire file into memory (good for preprocessing)

### 3. Macro Structure (preprocess.c:43-51)

```c
typedef struct Macro Macro;
struct Macro {
  char *name;
  bool is_objlike;         // vs function-like
  MacroParam *params;      // Parameter list
  char *va_args_name;      // For variadic macros
  Token *body;             // Replacement tokens
  macro_handler_fn *handler; // For built-ins like __LINE__
};

typedef struct MacroParam MacroParam;
struct MacroParam {
  MacroParam *next;
  char *name;
};
```

**MiniC adaptation:**
- **Keep:** All fields
- **Note:** Tokenized body (not string) makes expansion easier

### 4. Hideset (preprocess.c:62-66)

**The Prossor Algorithm** - Prevents infinite recursion

```c
typedef struct Hideset Hideset;
struct Hideset {
  Hideset *next;
  char *name;
};
```

**How it works:**
1. Each token has a hideset (set of macro names)
2. When expanding macro M, add M to hideset of result tokens
3. Don't expand a macro if it's in the token's hideset
4. This guarantees termination even with recursive macros

**Example:**
```c
#define A B
#define B A

A → B (hideset: {A})
  → A (hideset: {A, B}) → stops (A is in hideset)
```

**MiniC adaptation:**
- **Keep:** Exactly as-is - this is the key algorithm

### 5. Conditional Stack (preprocess.c:54-60)

```c
typedef struct CondIncl CondIncl;
struct CondIncl {
  CondIncl *next;
  enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
  Token *tok;
  bool included;
};
```

**MiniC adaptation:**
- **Keep:** All fields
- **Purpose:** Track nested #if/#ifdef state

---

## Function Analysis

### tokenize.c (805 lines)

**Functions we NEED:**

| Function | Lines | Purpose | Adaptation |
|----------|-------|---------|------------|
| `tokenize()` | ~400 | Main tokenizer | Keep 95%, simplify UTF-8 |
| `new_token()` | ~20 | Create token | Keep verbatim |
| `read_ident()` | ~30 | Read identifier | Keep verbatim |
| `read_number()` | ~100 | Read numeric literal | Keep verbatim |
| `read_string()` | ~80 | Read string literal | Keep verbatim |
| `read_punct()` | ~50 | Read operators | Keep verbatim |
| `error_tok()` | ~20 | Error reporting | Adapt to MiniC style |

**Functions we can SKIP:**
- `convert_keywords()` - MiniC's yacc handles keywords
- `display_width()` - For Unicode width (skip for DOS)
- `encode_utf8()` - DOS is ASCII-only

**Adaptation Strategy:**
1. Extract ~600 lines from tokenize.c
2. Remove UTF-8/Unicode handling (~100 lines)
3. Adapt error functions to MiniC style (~50 lines)
4. Result: ~550 lines for `minic_tokenize.c`

### preprocess.c (1,208 lines)

**Core Functions:**

| Function | Lines | Purpose | Adaptation |
|----------|-------|---------|------------|
| `preprocess()` | ~50 | Main entry point | Keep verbatim |
| `preprocess2()` | ~100 | Handle directives | Keep verbatim |
| `expand_macro()` | ~150 | Macro expansion | Keep verbatim (THE KEY!) |
| `find_macro()` | ~20 | Lookup macro | Keep verbatim |
| `read_macro_definition()` | ~100 | Parse #define | Keep verbatim |
| `read_macro_params()` | ~80 | Parse param list | Keep verbatim |
| `read_macro_args()` | ~100 | Collect arguments | Keep verbatim |
| `subst()` | ~80 | Substitute params | Keep verbatim |
| `stringize()` | ~40 | # operator | Keep verbatim |
| `paste_tokens()` | ~60 | ## operator | Keep verbatim |
| `eval_const_expr()` | ~200 | #if evaluation | Keep verbatim |
| `include_file()` | ~80 | #include handler | Adapt paths |

**Directive Handlers:**
- `#include` - ~80 lines, adapt path search
- `#define` - ~150 lines, keep verbatim
- `#undef` - ~20 lines, keep verbatim
- `#if/#elif/#else/#endif` - ~200 lines, keep verbatim
- `#ifdef/#ifndef` - ~40 lines, keep verbatim
- `#error/#warning` - ~30 lines, keep verbatim
- `#pragma` - ~40 lines, adapt for DOS

**Adaptation Strategy:**
1. Extract ~1,000 lines from preprocess.c
2. Adapt include path logic (~50 lines changes)
3. Simplify #pragma handling (~20 lines removed)
4. Result: ~1,000 lines for `minic_preprocess.c`

---

## Dependency Analysis

### External Dependencies in chibicc

**From stdlib:**
- `malloc()`, `calloc()`, `realloc()` - ✅ Available
- `memcmp()`, `memcpy()`, `strlen()` - ✅ Available
- `strtol()`, `strtod()` - ✅ Available (for number parsing)
- `fprintf()`, `vfprintf()` - ✅ Available (error reporting)

**POSIX-specific:**
- `glob.h` - ❌ Not needed (only for shell expansion)
- `sys/stat.h` - ⚠️ Need file_exists() - easy to replace
- `sys/wait.h` - ❌ Not needed (only for calling assembler)

**Our additions needed:**
- Replace `glob()` with simple file search
- Replace `stat()` with `fopen()` test
- Use MiniC's `die()` instead of `error()`

### Internal Dependencies

**From hashmap.c (4,564 bytes):**
- HashMap structure and functions
- **Decision:** Copy hashmap.c verbatim (~150 lines we need)

**From strings.c (690 bytes):**
- `StringArray` and `format()` function
- **Decision:** Copy strings.c verbatim (~80 lines)

**Total external code needed:** ~230 lines (hashmap + strings)

---

## Integration Strategy

### Phase 1: Files to Create

**1. minic/minic_token.h (~200 lines)**
- Token structure
- TokenKind enum
- File structure
- Function prototypes

**2. minic/minic_tokenize.c (~550 lines)**
- Adapted from chibicc/tokenize.c (805 lines)
- Removed: UTF-8, Unicode, keyword conversion
- Added: MiniC error handling

**3. minic/minic_preprocess.c (~1,000 lines)**
- Adapted from chibicc/preprocess.c (1,208 lines)
- Removed: Complex pragma, GNU extensions
- Adapted: Include path logic for DOS

**4. minic/minic_support.c (~230 lines)**
- HashMap from chibicc/hashmap.c
- StringArray from chibicc/strings.c

**Total new code:** ~1,980 lines (mostly adapted from chibicc)

### Phase 2: Files to Modify

**1. minic/minic.y (modify ~150 lines)**

**A. Add includes (top of file):**
```c
#include "minic_token.h"
```

**B. Replace yylex() (lines 3018-3400):**
```c
// Old: Character-by-character reading
// New: Token stream adapter

static Token *g_tokens = NULL;
static Token *g_current = NULL;

int yylex(void) {
    if (!g_current || g_current->kind == TK_EOF)
        return 0;

    Token *tok = g_current;
    g_current = tok->next;

    // Map TokenKind to yacc tokens
    switch (tok->kind) {
        case TK_IDENT:
            // Check keywords
            for (int i = 0; kwds[i].s; i++) {
                if (equal(tok, kwds[i].s))
                    return kwds[i].t;
            }
            yylval.str = strndup(tok->loc, tok->len);
            return IDENT;

        case TK_NUM:
            if (tok->fval != 0.0) {
                yylval.f = tok->fval;
                return FLNUM;
            }
            yylval.n = tok->val;
            return NUM;

        case TK_STR:
            yylval.str = tok->str;
            return STR;

        case TK_PUNCT:
            // Single-char or multi-char operators
            if (tok->len == 1)
                return tok->loc[0];
            // Map multi-char: "==", "!=", "<=", etc.
            if (equal(tok, "==")) return EQ;
            if (equal(tok, "!=")) return NE;
            // ... rest of operators

        default:
            return 0;
    }
}
```

**C. Update main() (around line 3421):**
```c
int main(int argc, char **argv) {
    // Parse arguments (-I, -D flags)
    char **include_paths = NULL;
    char *input_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-I", 2) == 0) {
            // Add include path
        } else if (strncmp(argv[i], "-D", 2) == 0) {
            // Define macro
        } else {
            input_file = argv[i];
        }
    }

    // Read input (file or stdin)
    char *input;
    if (input_file) {
        File *file = tokenize_file(input_file);
        input = file->contents;
    } else {
        // Read from stdin
        input = read_stdin();
    }

    // Tokenize
    g_tokens = tokenize_string(input);

    // Preprocess
    init_macros(); // Define __LINE__, __FILE__, etc.
    g_tokens = preprocess(g_tokens);

    // Start parsing
    g_current = g_tokens;

    // Rest of existing main()
    of = stdout;
    nglo = 1;
    if (yyparse() != 0)
        die("parse error");

    for (int i = 1; i < nglo; i++)
        fprintf(of, "data $glo%d = %s\n", i, ini[i]);

    return 0;
}
```

**2. minic/Makefile (modify ~20 lines)**

```makefile
BIN = minic
OBJS = y.tab.o minic_tokenize.o minic_preprocess.o minic_support.o

CFLAGS += -g -Wall -Wno-switch

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

y.tab.c: yacc minic.y
	./yacc minic.y

y.tab.o: y.tab.c minic_token.h
	$(CC) $(CFLAGS) -c y.tab.c

minic_tokenize.o: minic_tokenize.c minic_token.h
	$(CC) $(CFLAGS) -c minic_tokenize.c

minic_preprocess.o: minic_preprocess.c minic_token.h
	$(CC) $(CFLAGS) -c minic_preprocess.c

minic_support.o: minic_support.c minic_token.h
	$(CC) $(CFLAGS) -c minic_support.c

clean:
	rm -f yacc minic y.* $(OBJS)
```

---

## Week-by-Week Implementation Plan

### Week 2: Tokenizer Foundation
**Goal:** Can tokenize C source into Token stream

**Tasks:**
1. Create `minic_token.h` with structures
2. Create `minic_support.c` (hashmap, strings)
3. Adapt `tokenize.c` → `minic_tokenize.c`
4. Write 20 unit tests for tokenizer
5. Test: `echo 'int x = 42;' | tokenize_test` produces correct tokens

**Deliverable:** Working tokenizer (standalone, not yet integrated)

### Week 3: File Inclusion
**Goal:** #include works

**Tasks:**
1. Create `minic_preprocess.c` with basic structure
2. Implement `include_file()` function
3. Implement include path searching
4. Implement include guard detection
5. Test with existing headers (stdio.h, stdlib.h)

**Deliverable:** Can include headers

### Week 4: Simple Macros
**Goal:** Object-like #define works

**Tasks:**
1. Implement `read_macro_definition()`
2. Implement `find_macro()`
3. Implement simple `expand_macro()` (no args)
4. Implement `#undef`
5. Implement predefined macros (__LINE__, __FILE__)

**Deliverable:** Can use #define constants

**MILESTONE:** 80% of preprocessing works!

### Week 5: Conditionals
**Goal:** #ifdef, #if work

**Tasks:**
1. Implement conditional stack
2. Implement `#ifdef`, `#ifndef`, `#endif`
3. Implement `eval_const_expr()` (full expression evaluator)
4. Implement `#if`, `#elif`, `#else`
5. Implement `defined()` operator

**Deliverable:** Conditionals work

### Week 6: Function Macros
**Goal:** Macros with arguments work

**Tasks:**
1. Implement `read_macro_params()`
2. Implement `read_macro_args()`
3. Implement `subst()` (parameter substitution)
4. Implement hideset logic (prevent recursion)
5. Test with complex macros (MIN, MAX, etc.)

**Deliverable:** Function-like macros work

### Week 7: Operators
**Goal:** # and ## work

**Tasks:**
1. Implement `stringize()` (# operator)
2. Implement `paste_tokens()` (## operator)
3. Handle interaction with hideset
4. Test edge cases
5. Implement variadic macros (__VA_ARGS__)

**Deliverable:** Full macro system

### Week 8: Integration
**Goal:** Integrated into MiniC

**Tasks:**
1. Modify minic.y (yylex adapter, main)
2. Update Makefile
3. Test all 84 MiniC tests
4. Test all preprocessor tests
5. Fix bugs, polish

**Deliverable:** Production-ready preprocessor!

---

## Testing Strategy

### Unit Tests (Week 2-7)

**Tokenizer (20 tests):**
- Identifiers, keywords
- Numbers (int, long, float, double, hex, octal)
- Strings (escape sequences, multi-line)
- Operators (single-char, multi-char)
- Comments (C-style, C++-style)

**Includes (15 tests):**
- System includes (<stdio.h>)
- Local includes ("local.h")
- Nested includes (3 levels)
- Circular include detection
- Include guards

**Macros (40 tests):**
- Object-like macros
- Function-like macros
- Recursive expansion prevention
- Variadic macros
- Predefined macros

**Conditionals (30 tests):**
- #ifdef, #ifndef
- #if with expressions
- Nested conditionals (5 levels)
- defined() operator

**Operators (15 tests):**
- Stringification (#)
- Token pasting (##)
- Interaction with arguments

**Total:** 120 unit tests

### Integration Tests (Week 8)

- All 84 existing MiniC tests
- All 16 DOS examples
- 50+ new preprocessor tests

**Total:** 150+ integration tests

---

## Key Algorithms to Preserve

### 1. Prossor Algorithm (Hideset)

**From preprocess.c lines 17-23:**
```
Informally speaking, a macro is applied only once for each token.
If a macro token T appears in a result of direct or indirect macro
expansion of T, T won't be expanded any further.
```

**Implementation:** Keep hideset logic EXACTLY as chibicc has it!

### 2. Include Guard Optimization

**From preprocess.c (pragma_once HashMap):**
- Detect pattern: `#ifndef FOO_H ... #define FOO_H ... #endif`
- Skip re-reading file on subsequent includes
- 10x speedup for headers with many includes

### 3. Expression Evaluator

**Recursive descent parser for #if:**
- Full C precedence table
- Handles: `defined()`, arithmetic, logical, bitwise
- ~200 lines, keep verbatim from chibicc

---

## Risk Assessment

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| chibicc code doesn't adapt cleanly | High | Low | Already reviewed - it's very clean |
| Integration with yacc breaks | High | Medium | Build adapter incrementally, test each step |
| Hideset algorithm bugs | High | Low | Keep chibicc logic verbatim, test thoroughly |
| Include path issues on DOS | Medium | Medium | Test early, provide DOS-specific helpers |
| Performance on large files | Low | Low | chibicc already optimized (include guards) |

---

## Success Criteria

### Week 4 (Essential Features)
- ✅ #include works with existing headers
- ✅ #define works for constants
- ✅ All 84 MiniC tests still pass
- ✅ Can compile simple programs with headers

### Week 8 (Full C99 Preprocessor)
- ✅ All preprocessor directives work
- ✅ Function-like macros work
- ✅ # and ## operators work
- ✅ Variadic macros work
- ✅ 120 unit tests + 150 integration tests pass
- ✅ No memory leaks (valgrind clean)

---

## Next Steps

1. ✅ **Week 1 Complete** - This analysis document
2. **Week 2 Start** - Create minic_token.h and begin tokenizer extraction
3. **Daily progress** - Commit after each major function works

---

## References

- chibicc source: /tmp/chibicc/
- Prossor algorithm: https://github.com/rui314/chibicc/wiki/cpp.algo.pdf
- C99 preprocessor spec: ISO/IEC 9899:1999 Section 6.10

---

**Analysis Complete: 2024-11-29**
**Ready to proceed with Week 2: Tokenizer Foundation**
