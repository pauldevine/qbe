# Phase 5 Quick Start Guide

## 🚀 Ready to Compile Real DOS Code!

Good news: MiniC has MORE features than documented, and we now have a preprocessor solution!

## What Already Works (No Changes Needed)

✅ **Modern C operators and literals:**
- Hexadecimal (`0xFF`), octal (`0777`), character (`'A'`, `'\n'`) literals
- Compound assignments (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`)
- Ternary operator (`x > y ? x : y`)
- Comma operator

✅ **Data types:**
- All integer types: `char`, `short`, `int`, `long` (signed and unsigned)
- `float`, `double` (with FPU support)
- Pointers, arrays, `struct`, `union`, `enum`, `typedef`
- Anonymous `struct`/`union` (Phase 4 addition for FreeDOS compatibility)

✅ **C11 features** (Phase 4):
- `_Static_assert` (compile-time assertions)
- `_Alignof` (alignment queries)
- `_Alignas` (alignment control)

## Preprocessor Support (NEW!)

### Using the `minic_cpp` Wrapper

The `minic_cpp` script enables full preprocessor support via system `cpp`:

```bash
# Compile with preprocessor support
./minic_cpp input.c output.ssa

# Pipe directly to QBE backend
./minic_cpp input.c | qbe -t i8086 > output.s
```

**Supported preprocessor features:**
- ✅ `#define` constants: `#define PI 3`
- ✅ `#define` macros: `#define MAX(a,b) ((a)>(b)?(a):(b))`
- ✅ `#include` directives (for custom headers)
- ✅ `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif`
- ✅ `#undef`

**Example:**

```c
#define BUFFER_SIZE 256
#define MAX(a,b) ((a) > (b) ? (a) : (b))

main() {
    char buffer[BUFFER_SIZE];
    int x = 10;
    int y = 20;
    int max_val = MAX(x, y);
    return max_val;
}
```

Compile with:
```bash
./minic_cpp program.c program.ssa
```

The macros will be expanded before MiniC sees them.

## Known Limitations (Workarounds Available)

### 1. Arrow Operator (`->`)

**Status:** ❌ Not supported (yet - easy to implement)

**Workaround:** Use `(*ptr).member` instead of `ptr->member`

```c
// Instead of:
ptr->field = 10;

// Use:
(*ptr).field = 10;
```

### 2. Function Pointers

**Status:** ❌ Not supported (complex to implement)

**Workaround:** Use `switch` dispatch or restructure code

```c
// Instead of function pointers, use enum + switch:
enum Operation { OP_ADD, OP_SUB };

int calculate(int a, int b, enum Operation op) {
    switch (op) {
        case OP_ADD: return a + b;
        case OP_SUB: return a - b;
    }
    return 0;
}
```

### 3. C-Style Comments (`/* */`)

**Status:** ❌ Not supported (only `#` comments)

**Workaround:** The `cpp` preprocessor automatically handles this! Just use `minic_cpp` and write normal C comments.

Alternatively, convert manually:
```bash
# Convert C comments to # comments
sed 's|/\*|#|g; s|\*/||g' input.c > output.c
```

## Testing Your Real DOS Code

### Step 1: Try the Preprocessor

```bash
cd /home/user/qbe/minic

# Test with a file that has #define and #include
./minic_cpp your_dos_code.c test.ssa
```

### Step 2: Check for Compatibility Issues

Common patterns that WON'T work:
- `ptr->member` (use `(*ptr).member`)
- Function pointers (refactor to dispatch pattern)
- System headers like `<stdio.h>` (use DOS-specific headers or built-in functions)

### Step 3: Complete Build Pipeline

```bash
# 1. Preprocess + compile to QBE IL
./minic_cpp program.c program.ssa

# 2. Compile to 8086 assembly
qbe -t i8086 program.ssa > program.s

# 3. Assemble and link (if you have 8086 assembler)
# ... your assembler commands ...
```

## Recommended Next Steps for Real Code

### For FreeDOS / DOS Kernel Code:

1. **Start small:** Find the simplest `.c` file in the codebase
   ```bash
   find . -name "*.c" -exec wc -l {} \; | sort -n | head -10
   ```

2. **Test compilation:**
   ```bash
   ./minic_cpp simple_file.c > /tmp/test.ssa 2>&1
   ```

3. **Fix issues one at a time:**
   - Replace `ptr->member` with `(*ptr).member`
   - Ensure headers are DOS-compatible (no system libc headers)
   - Refactor function pointers if present

4. **Measure progress:**
   - Track how many files compile successfully
   - Document what patterns cause failures
   - Report findings for further compiler improvements

### For New DOS Projects:

**MiniC is fully capable of compiling DOS programs now!** Just use:
- `minic_cpp` for preprocessing
- Avoid `->` and function pointers
- Use DOS-specific I/O (see `minic/dos/examples/` for patterns)

## Example: Real-World Compilation

Here's what a typical DOS program compilation looks like:

**Input: `dos_util.c`**
```c
#define BUFFER_SIZE 128
#define MIN(a,b) ((a)<(b)?(a):(b))

typedef unsigned char byte;
typedef unsigned int word;

struct FileInfo {
    char name[32];
    word size;
    byte flags;
};

main() {
    struct FileInfo info;
    struct FileInfo *ptr;
    byte buffer[BUFFER_SIZE];

    ptr = &info;
    (*ptr).size = 1024;     // Would prefer ptr->size
    (*ptr).flags = 0x01;

    return MIN((*ptr).size, BUFFER_SIZE);
}
```

**Compilation:**
```bash
./minic_cpp dos_util.c | qbe -t i8086 > dos_util.s
```

**Output:** Valid 8086 assembly! ✅

## Success Metrics

Track your progress:
```bash
#!/bin/bash
# test_compilation.sh
TOTAL=0
SUCCESS=0

for file in $(find /path/to/dos_code -name "*.c"); do
    TOTAL=$((TOTAL + 1))
    if ./minic_cpp "$file" > /tmp/test.ssa 2>/dev/null; then
        SUCCESS=$((SUCCESS + 1))
        echo "✅ $file"
    else
        FAILED=$((FAILED + 1))
        echo "❌ $file"
    fi
done

echo ""
echo "Results: $SUCCESS/$TOTAL files compiled ($((SUCCESS * 100 / TOTAL))%)"
```

## Implementation Priorities

If you want to improve compatibility further, implement in this order:

1. **Arrow operator `->` ** (Easy, 2-4 hours, huge UX improvement)
2. **C-style comments `/* */`** (Easy, 1 hour, nice-to-have if not using `cpp`)
3. **Function pointers** (Hard, 1-2 days, enables callbacks/dispatch)
4. **Struct bitfields** (Hard, 2-3 days, needed for hardware register access)

## Questions?

Check:
- `PHASE5_ANALYSIS.md` - Comprehensive feature analysis
- `MINIC_REFERENCE.md` - Language reference (NOTE: outdated, this is more accurate)
- `minic/test/` - Test files showing all supported features
- `minic/dos/examples/` - Working DOS example programs

Happy compiling! 🎉
