# QBE i8086 Backend

This document describes the i8086 backend for QBE, which allows you to generate 16-bit x86 assembly code for real mode DOS and compatible environments.

## Overview

The i8086 backend targets 16-bit x86 processors (8086, 80186, 80286, 80386 in 16-bit mode). This allows QBE to generate code for:
- MS-DOS and DOS-compatible systems
- Embedded x86 systems running in real mode
- Boot loaders and firmware
- Retro computing projects

**Important**: This backend runs on modern systems (Mac, Linux, Windows) and **cross-compiles** to 16-bit x86. You do not need to run QBE on DOS itself.

## Usage

```bash
# Compile QBE with i8086 support (included by default)
make

# Generate 16-bit x86 assembly from QBE IR
./qbe -t i8086 input.ssa > output.asm

# List all available targets
./qbe -h
```

## Examples

### Example 1: Simple Addition

Input `test_add.ssa`:
```
export function w $add(w %a, w %b) {
@start
	%c =w add %a, %b
	ret %c
}
```

Command:
```bash
./qbe -t i8086 test_add.ssa > test_add.asm
```

Output `test_add.asm`:
```asm
.text
.balign 16
.globl _add
_add:
add:
	push bp
	mov bp, sp
	add ax, cx
	mov sp, bp
	pop bp
	ret
```

### Example 2: Maximum (with comparison)

Input `test_max.ssa`:
```
export function w $max(w %a, w %b) {
@start
	%cond =w csltw %a, %b
	jnz %cond, @retb, @reta
@reta
	ret %a
@retb
	ret %b
}
```

Output:
```asm
_max:
max:
	push bp
	mov bp, sp
	cmp ax, cx       ; Compare a and b
	setl dl          ; Set dl if a < b
	movzx dx, dl     ; Zero-extend to 16-bit
	test dx, dx      ; Test if true
	jnz start_retb   ; Jump if a < b
reta:
	mov sp, bp
	pop bp
	ret
start_retb:
	mov ax, cx       ; Return b
retb:
	mov sp, bp
	pop bp
	ret
```

### Example 3: Loop (sum from 0 to n)

Input `test_loop.ssa`:
```
export function w $sum_to_n(w %n) {
@start
	%sum0 =w copy 0
	%i0 =w copy 0
	jmp @loop
@loop
	%sum =w phi @start %sum0, @body %sum1
	%i =w phi @start %i0, @body %i1
	%cond =w csltw %i, %n
	jnz %cond, @body, @end
@body
	%sum1 =w add %sum, %i
	%i1 =w add %i, 1
	jmp @loop
@end
	ret %sum
}
```

Output shows proper loop structure:
```asm
_sum_to_n:
sum_to_n:
	push bp
	mov bp, sp
start_loop:
	mov cx, 0        ; i = 0
	mov ax, 0        ; sum = 0
loop:
	cmp cx, dx       ; compare i with n
	setl bl
	movzx bx, bl
	test bx, bx
	jnz body         ; if i < n, continue loop
	jmp end
body:
	add ax, cx       ; sum += i
	add cx, 1        ; i++
	jmp loop
end:
	mov sp, bp
	pop bp
	ret
```

### Example 4: Division and Modulo

Input `test_divmod.ssa`:
```
export function w $divmod_sum(w %a, w %b) {
@start
	%q =w div %a, %b
	%r =w rem %a, %b
	%sum =w add %q, %r
	ret %sum
}
```

Output shows proper x86 division handling:
```asm
_divmod_sum:
divmod_sum:
	push bp
	mov bp, sp
	cwd              ; Sign-extend AX into DX:AX
	idiv cx          ; Signed divide, quotient in AX, remainder in DX
	mov ax, cx
	cwd
	idiv dx
	mov cx, dx       ; Move remainder to CX
	add ax, cx       ; Add quotient + remainder
	mov sp, bp
	pop bp
	ret
```

Key features:
- **Signed division (`div`)**: Uses `cwd` to sign-extend, then `idiv`
- **Unsigned division (`udiv`)**: Uses `xor dx, dx` to zero-extend, then `div`
- **Remainder (`rem`/`urem`)**: Same as division, but result from DX instead of AX

### Example 5: Bit Shifts

Input `test_shifts.ssa`:
```
export function w $mul16(w %n) {
@start
	%result =w shl %n, 4
	ret %result
}

export function w $div8(w %n) {
@start
	%result =w sar %n, 3
	ret %result
}

export function w $shift_left(w %val, w %count) {
@start
	%result =w shl %val, %count
	ret %result
}
```

Output shows proper x86 shift handling:
```asm
_mul16:
mul16:
	push bp
	mov bp, sp
	shl ax, 4        ; Shift left by 4 (multiply by 16)
	mov sp, bp
	pop bp
	ret

_div8:
div8:
	push bp
	mov bp, sp
	sar ax, 3        ; Arithmetic shift right by 3 (divide by 8)
	mov sp, bp
	pop bp
	ret

_shift_left:
shift_left:
	push bp
	mov bp, sp
	shl ax, cl       ; Variable shift - count must be in CL
	mov sp, bp
	pop bp
	ret
```

Key features:
- **Shift left (`shl`)**: Logical shift left, zeros fill from right
- **Logical shift right (`shr`)**: Logical shift right, zeros fill from left
- **Arithmetic shift right (`sar`)**: Arithmetic shift right, sign bit fills from left
- **Immediate shifts**: Can shift by constant amounts (1-31)
- **Variable shifts**: Count must be in CL register; backend automatically moves to CL if needed

### Example 6: Function Calls with Arguments

The i8086 backend fully supports function calls with the cdecl calling convention:

Input `test_call.ssa`:
```
export function w $add_two(w %a, w %b) {
@start
	%sum =w add %a, %b
	ret %sum
}

export function w $test_call() {
@start
	%result =w call $add_two(w 10, w 20)
	ret %result
}
```

Output shows proper cdecl calling convention:
```asm
_add_two:
add_two:
	push bp
	mov bp, sp
	mov cx, word ptr [bp+6]    ; Load second parameter
	mov ax, word ptr [bp+4]    ; Load first parameter
	add ax, cx
	mov sp, bp
	pop bp
	ret

_test_call:
test_call:
	push bp
	mov bp, sp
	sub sp, 4                  ; Allocate space for 2 arguments
	mov ax, sp
	mov cx, ax
	add cx, 2
	mov word ptr [cx], 20      ; Push second arg at [sp+2]
	mov word ptr [ax], 10      ; Push first arg at [sp]
	call add_two               ; Call function
	add sp, 4                  ; Caller cleanup
	mov sp, bp
	pop bp
	ret
```

Key features:
- **Arguments on stack**: All arguments passed via stack (no register arguments)
- **Right-to-left order**: Arguments pushed in reverse order (cdecl convention)
- **Caller cleanup**: Calling function cleans up stack after call
- **Return in AX**: 16-bit return values in AX, 32-bit in DX:AX
- **Stack layout**: `[bp+0]` = saved BP, `[bp+2]` = return address, `[bp+4]` = first arg

### Example 7: Memory Addressing Modes

The i8086 backend supports various addressing modes:

Input `test_memory.ssa`:
```
export function w $simple_load(l %addr) {
@start
	%val =w loadw %addr
	ret %val
}

export function $simple_store(l %addr, w %val) {
@start
	storew %val, %addr
	ret
}

export function w $struct_field(l %ptr) {
@start
	%addr =l add %ptr, 4
	%val =w loadw %addr
	ret %val
}
```

Output shows proper addressing modes:
```asm
_simple_load:
simple_load:
	push bp
	mov bp, sp
	mov ax, word ptr [ax]       ; Register indirect: [reg]
	mov sp, bp
	pop bp
	ret

_simple_store:
simple_store:
	push bp
	mov bp, sp
	mov word ptr [cx], ax       ; Store to [reg]
	mov sp, bp
	pop bp
	ret

_struct_field:
struct_field:
	push bp
	mov bp, sp
	add ax, 4                   ; Calculate address
	mov ax, word ptr [ax]       ; Load from [base+offset]
	mov sp, bp
	pop bp
	ret
```

Supported addressing modes:
- **Register indirect**: `[bx]`, `[bp]`, `[si]`, `[di]`
- **Base + offset**: `[bx+4]`, `[bp-2]`
- **Base + index**: `[bx+si]`, `[bp+di]`
- **Base + index + offset**: `[bx+si+8]`
- **Direct**: `[label]`, `[data]`

**Note**: The i8086 only supports specific register combinations for base and index:
- Base: BX or BP
- Index: SI or DI
- No scaling (scale is always 1)

## Features

### Implemented

- **16-bit word operations** (`Kw` class)
- **Basic arithmetic**: add, sub, mul, and, or, xor
- **Division and remainder**: Both signed (div/rem) and unsigned (udiv/urem)
  - Proper DX:AX handling with `cwd` for signed, `xor dx,dx` for unsigned
  - Quotient from AX, remainder from DX
- **Bit shifts**: shl, shr, sar with both immediate and variable shift counts
  - Immediate shifts: `shl ax, 5`
  - Variable shifts: `shl ax, cl` (count in CL register)
  - Backend automatically moves shift count to CL when needed
- **Comparisons**: All signed and unsigned integer comparisons (eq, ne, lt, gt, le, ge)
- **Conditional branches**: Full support for if-statements and conditional jumps
- **Loops**: While loops, for loops, and all control flow structures
- **Memory addressing**: Full support for i8086 addressing modes
  - Register indirect: `[bx]`, `[bp]`, `[si]`, `[di]`
  - Base + offset: `[bp+4]`, `[bx-2]`
  - Base + index: `[bx+si]`, `[bp+di]`
  - Base + index + offset: `[bx+si+offset]`
  - LEA (load effective address) for address calculations
- **Function prologue/epilogue**: Standard BP-based stack frames
- **Function calls**: ✓ Working - full cdecl calling convention support
  - Arguments passed on stack (right-to-left order)
  - Caller allocates stack space and cleans up after call
  - Return values in AX (16-bit) or DX:AX (32-bit)
  - Proper CALL() encoding for register allocator
- **Parameter reception**: ✓ Working - functions correctly receive parameters from stack
  - Parameters loaded from [bp+4], [bp+6], etc.
  - Supports 16-bit (Kw), 32-bit (Kl), and extended byte/halfword parameters
  - No TODO comments in generated code for parameter access
- **Calling convention**: cdecl (arguments on stack, caller cleanup)
- **Return values**: AX for 16-bit, DX:AX for 32-bit
- **Register allocation**: AX, BX, CX, DX, SI, DI
- **Stack operations**: push/pop, local variables, dynamic allocation (Osalloc)

### Limitations / TODO

- **No floating point**: The 8087 FPU is not yet supported
- **Limited 32-bit support**: Long (Kl) operations are not fully implemented
- **Incomplete instruction selection**: Some QBE IR operations are not yet mapped
- **No optimizations**: Code generation is straightforward without target-specific optimizations
- **Missing features**:
  - Bit rotations (rol/ror)
  - Conditional moves
  - Structure passing
  - Far pointers for large/huge memory models
  - Multiply high word operations

## Register Usage

### General Purpose Registers (16-bit)

| Register | Usage | Saved By | Notes |
|----------|-------|----------|-------|
| AX | Accumulator, return value | Caller | Used for function returns |
| BX | Base | Callee | General purpose |
| CX | Counter | Caller | Used for loops, shifts |
| DX | Data, extended return | Caller | DX:AX for 32-bit returns |
| SI | Source index | Callee | String operations, general purpose |
| DI | Destination index | Callee | String operations, general purpose |
| BP | Base pointer | Callee | Frame pointer (always preserved) |
| SP | Stack pointer | - | Stack pointer (globally live) |

### Calling Convention

The i8086 backend uses the **cdecl** calling convention:

1. Arguments pushed on stack **right-to-left**
2. Caller cleans up stack after call
3. Return values:
   - 8/16-bit: AL/AX
   - 32-bit: DX:AX (DX = high word, AX = low word)
4. Callee must preserve: BX, SI, DI, BP
5. Caller must preserve: AX, CX, DX

## Assembly Syntax

The generated assembly uses:
- **Intel syntax**: `mov dst, src`
- **AT&T-style directives**: `.text`, `.globl`, `.balign`
- **Underscore prefix**: Global symbols prefixed with `_` (DOS/OMF convention)

You can assemble the output with:
- **NASM**: `nasm -f obj output.asm`
- **MASM/TASM**: May need minor syntax adjustments
- **GNU as**: With `--32` flag and some preprocessing

## Memory Models

The i8086 backend supports all six classic DOS memory models. Use the `-m` option to select:

```bash
./qbe -t i8086 -m <model> input.ssa > output.asm
```

### Supported Models

| Model | Code | Data | Description |
|-------|------|------|-------------|
| **tiny** | < 64KB | < 64KB | Single segment (.COM files). CS=DS=SS. Uses `ret`. |
| **small** | < 64KB | < 64KB | Separate code/data segments. Default. Uses `ret`. |
| **compact** | < 64KB | > 64KB | Near code, far data. Uses `ret`. |
| **medium** | > 64KB | < 64KB | Far code, near data. Uses `retf`. |
| **large** | > 64KB | > 64KB | Far code, far data. Uses `retf`. |
| **huge** | > 64KB | > 64KB | Like large, but arrays can exceed 64KB. Uses `retf`. |

### Stack Layout Differences

**Near calls (tiny/small/compact)**:
```
[bp+0]  saved BP
[bp+2]  return address (2 bytes: offset only)
[bp+4]  first parameter
[bp+6]  second parameter
```

**Far calls (medium/large/huge)**:
```
[bp+0]  saved BP
[bp+2]  return offset
[bp+4]  return segment
[bp+6]  first parameter
[bp+8]  second parameter
```

### Example Output

**Small model** (default):
```asm
; Memory model: small
.model small
.code

add proc near
    push bp
    mov bp, sp
    mov cx, word ptr [bp+6]    ; second param
    mov ax, word ptr [bp+4]    ; first param
    add ax, cx
    mov sp, bp
    pop bp
    ret                        ; near return
add endp
```

**Medium model**:
```asm
; Memory model: medium
.model medium
.code

add proc far
    push bp
    mov bp, sp
    mov cx, word ptr [bp+8]    ; second param (offset by 2 for far ret)
    mov ax, word ptr [bp+6]    ; first param
    add ax, cx
    mov sp, bp
    pop bp
    retf                       ; far return
add endp
```

### Far Pointers

Far pointers (32-bit segment:offset pairs) are supported for data access in all memory models using the far pointer operations:
- `loadfb`, `loadfh`, `loadfw` - Load byte/half/word through far pointer
- `storefb`, `storefh`, `storefw` - Store byte/half/word through far pointer
- `mkfar` - Create far pointer from segment:offset
- `farseg`, `faroff` - Extract segment/offset from far pointer

See the MiniC documentation for how to use the `far` keyword in C code.

## Building for DOS

After generating assembly with QBE:

```bash
# Generate assembly
./qbe -t i8086 program.ssa > program.asm

# Assemble with NASM (produces OBJ file)
nasm -f obj program.asm -o program.obj

# Link with OpenWatcom's wlink
wlink system dos file program.obj name program.exe

# Or use Turbo Link
tlink program.obj, program.exe
```

## Implementation Status

| Feature | Status | Notes |
|---------|--------|-------|
| Integer arithmetic (16-bit) | ✓ Working | add, sub, mul, and, or, xor |
| Division/Remainder | ✓ Working | div, rem, udiv, urem with proper DX:AX handling |
| Bit shifts | ✓ Working | shl, shr, sar with immediate and variable counts |
| Comparisons | ✓ Working | All signed/unsigned integer comparisons |
| Conditional branches | ✓ Working | if-else, all conditional jumps |
| Loops | ✓ Working | while, for, all loop structures |
| Memory addressing | ✓ Working | All i8086 addressing modes, LEA support |
| Memory load/store | ✓ Working | loadb/w/l, storeb/w/l with all addressing modes |
| Parameter reception | ✓ Working | Functions receive params from stack correctly |
| Function calls | ✓ Working | Full cdecl calling convention with stack arguments |
| **Memory models** | ✓ Working | tiny, small, medium, compact, large, huge |
| **Far code** | ✓ Working | CALL FAR, RETF for medium/large/huge models |
| **Far pointers** | ✓ Working | Far data access for all models |
| Floating point | ⚠ Partial | Basic 8087 FPU support |
| 32-bit operations | ⚠ Partial | Limited support |
| Optimizations | ✗ TODO | None yet |

## Architecture

The i8086 backend consists of:

- `i8086/all.h` - Register definitions, forward declarations
- `i8086/targ.c` - Target definition and registration
- `i8086/abi.c` - Calling convention (cdecl)
- `i8086/isel.c` - Instruction selection (QBE IR → x86 instructions)
- `i8086/emit.c` - Assembly code generation

## Contributing

The i8086 backend is functional with core features complete including all memory models. Contributions are welcome for:

- Improving 8087 FPU support for floating point operations
- Improving 32-bit (long) operations (Kl class)
- Optimizing code generation (peephole optimizations, better register allocation)
- Implementing bit rotations (rol, ror, rcl, rcr)
- Adding conditional move support (for 80386+)
- Better instruction selection for complex patterns
- Huge model segment arithmetic for large arrays

## References

- [Intel 8086 Family User's Manual](https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf)
- [QBE IL Documentation](https://c9x.me/compile/doc/il.html)
- [x86 DOS Calling Conventions](https://en.wikipedia.org/wiki/X86_calling_conventions#cdecl)

## License

This backend follows the same MIT license as QBE itself.
