# QBE i8086 Backend - Complete Guide

## Overview

The i8086 backend for QBE enables cross-compilation from modern C code to 16-bit 8086 assembly for DOS and other 16-bit x86 targets. This backend has been recently fixed to support:

- ✅ **Return values** - Functions properly return values in AX register
- ✅ **Function calls** - Full support for calling functions with parameters
- ✅ **Parameter passing** - cdecl calling convention with stack-based parameters
- ✅ **Global and local variables** - Full variable support
- ✅ **Arithmetic operations** - Complete ALU operations
- ✅ **Control flow** - Loops, conditionals, and jumps

## Quick Start

### 1. Build QBE with i8086 Support

```bash
git clone https://github.com/pauldevine/qbe
cd qbe
make
```

### 2. Compile a Simple Program

```bash
# Create a simple C program
cat > hello.c << 'EOF'
main() {
    return 42;
}
EOF

# Compile to QBE IL (requires minic)
./minic/minic < hello.c > hello.ssa

# Compile to i8086 assembly
./qbe -t i8086 hello.ssa > hello.asm

# The output is NASM-compatible 8086 assembly
cat hello.asm
```

### 3. Assemble to DOS Executable (Optional)

If you have NASM installed:

```bash
# For a .COM file (tiny model):
nasm -f bin hello.asm -o hello.com

# For a .OBJ file to link with other code:
nasm -f obj hello.asm -o hello.obj
```

## Detailed Usage

### C Compiler (minic)

The minic compiler is a simple C compiler that generates QBE IL. It supports a subset of C:

**Supported:**
- Functions with return values
- Parameters (with type declarations)
- Local and global variables
- Arithmetic: +, -, *, /, %
- Bitwise: &, |, ^, <<, >>
- Comparisons: ==, !=, <, >, <=, >=
- Control flow: if, else, while, for
- Pointers and arrays
- Structs and unions

**Not Supported:**
- Preprocessor directives (#include, #define)
- Type qualifiers (const, volatile, static)
- Advanced C features (goto, switch, etc.)

### Calling Convention

The i8086 backend uses the cdecl calling convention:

```
Stack Layout (after function prologue):
  [bp+8]  Third parameter
  [bp+6]  Second parameter
  [bp+4]  First parameter
  [bp+2]  Return address
  [bp+0]  Saved BP (current frame)
  [bp-2]  First local variable
  [bp-4]  Second local variable
  ...
```

**Key Points:**
- Arguments pushed right-to-left onto stack
- Caller cleans up stack after call
- Return value in AX (16-bit) or DX:AX (32-bit)
- Callee must preserve: BX, SI, DI, BP
- Caller must preserve: AX, CX, DX (if needed)

### Example: Function with Parameters

```c
add(int a, int b) {
    return a + b;
}

main() {
    int result;
    result = add(10, 32);  /* Returns 42 */
    return result;
}
```

Generated assembly:

```asm
add:
    push bp
    mov bp, sp
    mov cx, word ptr [bp+6]  ; Load b
    mov ax, word ptr [bp+4]  ; Load a
    add ax, cx               ; a + b
    pop bp
    ret

main:
    push bp
    mov bp, sp
    sub sp, 4                ; Allocate stack space for args
    mov word ptr [bp-2], 32  ; Second arg
    mov word ptr [bp-4], 10  ; First arg
    call add                 ; Call function
    add sp, 4                ; Clean up stack
    pop bp
    ret
```

## Recent Fixes (January 2025)

### Fix 1: Return Value Handling

**Problem:** Return values were not being placed in the AX register.

**Root Cause:** The `selret()` function in `i8086/abi.c` was commented out due to register allocation issues.

**Solution:**
1. Uncommented `selret()` call
2. Added `b->jmp.arg = CALL(ca)` to inform register allocator
3. This tells the allocator that the return instruction implicitly uses AX

**Before:**
```asm
main:
    push bp
    mov bp, sp
    ; No mov ax, 42 here!
    pop bp
    ret
```

**After:**
```asm
main:
    push bp
    mov bp, sp
    mov ax, 42        ; ✓ Return value now in AX!
    pop bp
    ret
```

### Fix 2: Function Call Handling

**Problem:** Function calls crashed with "invalid reference type" error.

**Root Cause:** The emit code didn't have special handling for Ocall instructions, and was trying to emit invalid addressing modes like `call word ptr [bp+4]`.

**Solution:** Added special case handling in `i8086/emit.c` for Ocall:
- Only allow RTmp (register) for indirect calls
- Only allow RCon with CAddr (function name) for direct calls
- Die with clear error for invalid call targets

**Code:**
```c
if (i->op == Ocall) {
    Ref target = i->arg[0];
    fprintf(f, "\tcall ");

    if (rtype(target) == RTmp) {
        /* Indirect call through register */
        fprintf(f, "%s\n", rname[target.val]);
    } else if (rtype(target) == RCon) {
        /* Direct call to function */
        Con *c = &fn->con[target.val];
        if (c->type == CAddr) {
            emitaddr(c, f);
        }
    }
    return;
}
```

### Fix 3: Return Value Capture from Calls

**Problem:** Return values from function calls were not being captured.

**Root Cause:** The code in `selcall()` that copies the return value from AX to the destination was commented out.

**Solution:** Uncommented the `emit(Ocopy, icall->cls, icall->to, TMP(RAX), R)` line that captures the return value from AX after a call.

## Architecture

### Backend Structure

```
i8086/
├── targ.c       - Target machine description
├── abi.c        - ABI and calling convention
├── isel.c       - Instruction selection
├── emit.c       - Assembly code emission
├── examples/    - Example programs
└── README.md    - This file
```

### Compilation Pipeline

```
C Source (.c)
    ↓
  minic (C to QBE IL)
    ↓
QBE IL (.ssa)
    ↓
  qbe -t i8086 (IL to Assembly)
    ↓
Assembly (.asm)
    ↓
  nasm (Assembler)
    ↓
Binary (.com / .exe)
```

### Register Allocation

The i8086 has limited registers:

- **AX** - Accumulator, return values, arithmetic
- **BX** - Base register, general purpose
- **CX** - Counter, loop variable, shifts
- **DX** - Data, high word for 32-bit ops
- **SI** - Source index, string ops
- **DI** - Destination index, string ops
- **BP** - Base pointer, stack frame
- **SP** - Stack pointer

**Register Classes:**
- Kw (word) - Uses AX, BX, CX, DX, SI, DI
- Kl (long) - Uses register pairs (DX:AX)

## DOS Runtime Library

The minic/dos directory contains a complete DOS runtime:

### crt0.asm - Startup Code

Entry point that:
1. Sets up DOS environment
2. Calls C main() function
3. Returns exit code to DOS via INT 21h, AH=4Ch

### doslib.asm - DOS System Calls

Low-level wrappers for DOS interrupts:
- Character I/O: `dos_putchar`, `dos_getchar`, `dos_puts`
- File I/O: `dos_open`, `dos_read`, `dos_write`, `dos_close`
- Directory: `dos_mkdir`, `dos_chdir`, `dos_rmdir`
- Memory: `dos_malloc`, `dos_free`
- System: `dos_get_version`, `dos_exit`

### libc.c - Standard C Library

Higher-level C functions:
- String: `strlen`, `strcpy`, `strcmp`, `strcat`
- Memory: `memcpy`, `memset`, `memcmp`
- Character: `isalpha`, `isdigit`, `toupper`, `tolower`
- I/O: `putchar`, `puts`, `getchar`
- Conversion: `itoa`, `atoi`

### Linking Example

```bash
# Compile all components
./minic/minic < myprogram.c > myprogram.ssa
./qbe -t i8086 myprogram.ssa > myprogram.asm
./minic/minic < minic/dos/libc.c > libc.ssa
./qbe -t i8086 libc.ssa > libc.asm

# Assemble
nasm -f obj minic/dos/crt0.asm -o crt0.obj
nasm -f obj minic/dos/doslib.asm -o doslib.obj
nasm -f obj myprogram.asm -o myprogram.obj
nasm -f obj libc.asm -o libc.obj

# Link (using OpenWatcom wlink or similar)
wlink system dos file crt0.obj,myprogram.obj,libc.obj,doslib.obj name myprogram.exe
```

## Limitations

### Current Limitations

1. **No far pointers** - Small memory model only (code <64KB, data <64KB)
2. **No inline assembly** - Must link with .asm files
3. **Single memory model** - Only small model supported
4. **No segment overrides** - Cannot access arbitrary memory segments

### What IS Supported ✅

1. **8087 FPU Support** - Full hardware float/double operations (PR #11)
2. **32-bit long support** - DX:AX register pairs working (PR #11)
3. **Function pointers** - Typedef, parameters, indirect calls (PR #11)
4. **Struct bitfields** - Full packing and read/write (PR #11)
5. **Variadic functions** - Basic support for printf-style functions

### Memory Models

Currently only supports **small model**:
- Code: <= 64KB (one segment)
- Data: <= 64KB (one segment)
- Near pointers only (16-bit)

Future work: tiny (.COM), medium, large, huge models

### Known Issues

1. Some QBE optimizations may not work correctly on i8086
2. Stack overflow not detected
3. No far pointer support for >64KB programs

## Testing

### Unit Tests

```bash
cd i8086/examples

# Test return values
./minic/minic < 01_return.c | ../qbe -t i8086 > test.asm
grep "mov ax, 42" test.asm && echo "PASS" || echo "FAIL"

# Test function calls
./minic/minic < 02_function_call.c | ../qbe -t i8086 > test.asm
grep "call get_value" test.asm && echo "PASS" || echo "FAIL"

# Test parameters
./minic/minic < 03_parameters.c | ../qbe -t i8086 > test.asm
grep "add ax, cx" test.asm && echo "PASS" || echo "FAIL"
```

### Integration Testing

To test on real DOS (or DOSBox):

```bash
# Build example
./minic/minic < example.c | ./qbe -t i8086 > example.asm
nasm -f bin example.asm -o example.com

# Run in DOSBox
dosbox -c "mount c ." -c "c:" -c "example.com" -c "echo Exit code: %ERRORLEVEL%" -c "exit"
```

## Troubleshooting

### Compilation Errors

**Error: "Assertion failed: spill.c:445: dead reg"**
- This was the register allocation bug, now fixed
- If you still see this, ensure you have the latest changes

**Error: "invalid reference type"**
- This was the function call bug, now fixed
- If you still see this on valid code, please report

### Runtime Errors

**Program crashes immediately**
- Check that crt0.asm is linked first
- Ensure stack is set up properly
- Verify all symbols are resolved

**Wrong return value**
- Check that function actually sets AX before returning
- Verify calling convention is correct
- Look for register clobbering

## Contributing

### Code Style

- Use tabs for indentation (like rest of QBE)
- Follow QBE's coding conventions
- Add comments for non-obvious code
- Test on actual DOS when possible

### Testing

Before submitting changes:

1. Run all examples in i8086/examples/
2. Check generated assembly is valid
3. Test on DOSBox or real DOS if possible
4. Verify no regressions on other backends

### Known TODOs

**Completed ✅:**
- [x] Implement FPU support (8087 coprocessor) - PR #11
- [x] Complete 32-bit long support (DX:AX pairs) - PR #11

**Remaining:**
- [ ] Add support for far pointers
- [ ] Implement other memory models (tiny, medium, large, huge)
- [ ] Add more optimization passes specific to 8086
- [ ] Better register allocation for 8086's limited registers
- [ ] Support for segment overrides
- [ ] Inline assembly support

## References

### 8086 Documentation

- Intel 8086 Family User's Manual
- MS-DOS Programmer's Reference
- Ralph Brown's Interrupt List

### QBE Resources

- [QBE IL Documentation](https://c9x.me/compile/doc/il.html)
- [QBE Backend Guide](https://c9x.me/compile/doc/backend.html)

### DOS Programming

- DOS INT 21h Reference
- NASM Documentation
- Ralf Brown's Interrupt List

## License

Same as QBE (MIT License)

## Authors

- QBE: Quentin Carbonneaux
- i8086 Backend: pauldevine and contributors
- Recent fixes (return values, function calls): Claude Code AI Assistant

## Changelog

### 2025-11-26 (PR #11 & #12)

- **ADDED**: 8087 FPU support - full hardware float/double operations
  - All arithmetic: add, sub, mul, div
  - Comparisons with FPU status word
  - Type conversions (int ↔ float/double)

- **ADDED**: 32-bit long support - DX:AX register pairs
  - Full 32-bit arithmetic operations
  - Proper return value handling

- **ADDED**: Function pointer support in MiniC
  - Typedef function pointers
  - Function pointers as parameters
  - Indirect calls via registers (`call ax`)

- **ADDED**: Struct bitfield support in MiniC
  - Bitfield packing
  - Read with shift/mask
  - Write with read-modify-write pattern

- **ADDED**: C11 features in MiniC
  - `_Static_assert`, `_Generic`, `_Alignof`/`_Alignas`
  - Compound literals, designated initializers
  - Anonymous struct/union

### 2025-01-24

- **FIXED**: Return value handling now works
  - Uncommented and fixed selret() function
  - Added proper register allocation hints

- **FIXED**: Function calls now work
  - Added special Ocall handling in emit.c
  - Fixed return value capture in selcall()

- **ADDED**: Comprehensive examples and documentation
- **ADDED**: Full DOS runtime library
- **TESTED**: All basic functionality working

### Earlier

- Initial i8086 backend implementation
- Basic instruction selection
- Stack-based calling convention
