# QBE i8086 Backend Examples

These examples demonstrate the fixed functionality in the i8086 backend for QBE.

## Fixed Issues

1. **Return values now work** - Functions can return values in the AX register
2. **Function calls now work** - Functions can call other functions without crashing
3. **Parameters work** - Functions can accept parameters via the stack (cdecl convention)

## Building the Examples

### Prerequisites

- QBE compiler built with i8086 support
- minic compiler (in the minic directory)
- NASM assembler (for assembling to DOS executables)
- DOSBox or similar DOS emulator (for testing)

### Compilation Steps

For each example (e.g., `01_return.c`):

```bash
# 1. Compile C to QBE IL
./minic/minic < i8086/examples/01_return.c > example.ssa

# 2. Compile QBE IL to i8086 assembly
./qbe -t i8086 example.ssa > example.asm

# 3. Assemble to DOS executable (if you have NASM)
nasm -f bin example.asm -o example.com

# 4. Run in DOS emulator
dosbox example.com
```

## Example Descriptions

### 01_return.c
Tests basic return value functionality. The main function returns 42, which should be placed in the AX register before the ret instruction.

**Generated assembly shows:**
```asm
mov ax, 42
ret
```

### 02_function_call.c
Tests function calls with return values. Calls `get_value()` which returns 42, then returns that value from main.

**Key features:**
- Function call instruction
- Return value captured from AX register
- Nested return values

### 03_parameters.c
Tests parameter passing via cdecl convention. Calls `add(10, 32)` which should return 42.

**Key features:**
- Parameters pushed onto stack
- Parameters accessed via [bp+4], [bp+6], etc. in callee
- Stack cleanup after call

### 04_global_var.c
Tests global variable storage and function calls that modify globals.

**Key features:**
- Global variable declaration
- Function modifies global
- Main reads global for return value

### 05_arithmetic.c
Tests various arithmetic operations: addition, subtraction, multiplication.

**Key features:**
- Local variables
- Multiple arithmetic operations
- Register allocation for temporaries

### 10_memory_models.ssa
Tests all six 8086 memory models (tiny, small, medium, compact, large, huge).

**Key features:**
- Demonstrates near vs far procedure calls
- Shows parameter offset differences between near and far calls
- Works correctly with all memory model flags

**Usage:**
```bash
# Near code models (tiny/small/compact) - use RET
./qbe -t i8086 -m tiny i8086/examples/10_memory_models.ssa
./qbe -t i8086 -m small i8086/examples/10_memory_models.ssa
./qbe -t i8086 -m compact i8086/examples/10_memory_models.ssa

# Far code models (medium/large/huge) - use RETF
./qbe -t i8086 -m medium i8086/examples/10_memory_models.ssa
./qbe -t i8086 -m large i8086/examples/10_memory_models.ssa
./qbe -t i8086 -m huge i8086/examples/10_memory_models.ssa
```

## Assembly Output Format

The i8086 backend generates MASM/NASM compatible assembly with:

- `.text` section for code
- `.data` section for global variables
- cdecl calling convention:
  - Arguments pushed right-to-left
  - Caller cleans up stack
  - Return value in AX (16-bit) or DX:AX (32-bit)
  - Callee-save registers: BX, SI, DI, BP
  - Caller-save registers: AX, CX, DX

## Known Limitations

The i8086 backend currently has these limitations:

1. **No floating point** - FPU operations not yet implemented
2. **Limited long (32-bit) support** - DX:AX pair handling incomplete
3. **No variadic functions** - printf-style functions limited
4. **No inline assembly** - Must use assembly files
5. **Simple optimization only** - QBE's optimizations may not all work on i8086

## Testing Return Codes

To test if the programs work, check the DOS return code:

```bat
REM In DOS
example.com
ECHO %ERRORLEVEL%
```

Should print 42 for most examples.

## Next Steps

To create a full DOS application:

1. Link with the DOS runtime (minic/dos/crt0.asm)
2. Link with DOS library (minic/dos/doslib.asm)
3. Link with C library (minic/dos/libc.c compiled to .asm)
4. Use a DOS linker to create final .EXE file

See minic/dos/ directory for the complete DOS runtime library.
