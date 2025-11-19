# MiniC Compiler Reference

MiniC is a small C compiler frontend for the QBE intermediate language. This document describes the supported C language features and known limitations.

## Overview

MiniC compiles a subset of C to QBE intermediate language (IL), which can then be compiled to native assembly for various architectures (AMD64, ARM64, RISC-V 64, i8086).

## Supported Features

### Data Types

- **int** - 32-bit signed integer (maps to QBE 'w' type)
- **long** - 64-bit signed integer (maps to QBE 'l' type)
- **void** - used for function return types only
- **Pointers** - any level of indirection (e.g., `int*`, `int**`, `long*`)

### Operators

#### Arithmetic Operators
- `+` - Addition
- `-` - Subtraction and unary negation
- `*` - Multiplication
- `/` - Division
- `%` - Modulo (remainder)

#### Bitwise Operators
- `&` - Bitwise AND
- `|` - Bitwise OR
- `^` - Bitwise XOR
- `~` - Bitwise NOT (complement)
- `<<` - Left shift
- `>>` - Right shift

#### Comparison Operators
- `<` - Less than
- `>` - Greater than
- `<=` - Less than or equal
- `>=` - Greater than or equal
- `==` - Equal
- `!=` - Not equal

#### Logical Operators
- `&&` - Logical AND (short-circuit evaluation)
- `||` - Logical OR (short-circuit evaluation)
- `!` - Logical NOT

#### Assignment
- `=` - Assignment

#### Pointer Operators
- `&` - Address-of
- `*` - Dereference

#### Increment/Decrement
- `++` - Post-increment (e.g., `i++`)
- `--` - Post-decrement (e.g., `i--`)

#### Other
- `[]` - Array subscript (equivalent to pointer arithmetic)
- `sizeof()` - Size of type in bytes

### Control Flow

#### Conditional Statements
```c
if (condition)
    statement;

if (condition)
    statement;
else
    statement;
```

#### Loops
```c
while (condition)
    statement;

do
    statement;
while (condition);

for (init; condition; increment)
    statement;
```

#### Loop Control
- `break` - Exit innermost loop
- `continue` - Skip to next iteration of loop

#### Function Return
- `return expression;` - Return from function

### Functions

```c
# Function declaration
int function_name();

# Function definition
int function_name(int param1, long param2) {
    # function body
    return value;
}

# Function call
result = function_name(arg1, arg2);
```

Features:
- Variadic arguments supported via `...` in IL
- Recursive calls supported
- Parameters passed by value
- Return type required (use `void` for no return value, but must still use `return;`)

### Variables

#### Local Variables
```c
int local_var;
long *ptr;
```
- Must be declared at the beginning of a function
- Initialized to undefined values (use explicit assignment)

#### Global Variables
```c
int global_var;
```
- Declared outside functions
- Automatically initialized to zero

#### Arrays
```c
int array[10];
array[5] = 42;
int value = array[5];
```
- Implemented via pointer arithmetic
- No bounds checking
- Can be used with pointer arithmetic

### Pointers
```c
int x = 42;
int *ptr = &x;
*ptr = 84;
```
- Full pointer arithmetic supported
- Pointer to pointer supported
- Type checking for pointer assignments

### Comments
```c
# This is a comment (shell-style)
```
Note: C-style `/* */` comments are NOT supported.

### String Literals
```c
printf("Hello, world!\n");
```
- Strings are null-terminated
- Escape sequences supported: `\n`, `\"`, `\\`

## Type Promotion and Conversion

- Automatic promotion from `int` to `long` when mixing types
- Pointer arithmetic automatically scales by size of pointed-to type
- Integer to pointer and pointer to integer conversions allowed (via `void*`)

## Limitations

### Not Supported

1. **Types**:
   - `char`, `short`, `unsigned` types
   - `float`, `double` (no floating-point support)
   - `struct`, `union`, `enum`
   - `typedef`
   - Type qualifiers: `const`, `volatile`, `restrict`
   - Storage classes: `static`, `extern`, `auto`, `register`

2. **Operators**:
   - Prefix increment/decrement (`++i`, `--i`)
   - Compound assignments (`+=`, `-=`, `*=`, etc.)
   - Ternary operator (`? :`)
   - Comma operator (`,`)

3. **Control Flow**:
   - `switch`/`case` statements
   - `goto` and labels

4. **Functions**:
   - Function pointers
   - Returning structures
   - K&R style function declarations

5. **Preprocessor**:
   - No `#include`, `#define`, `#ifdef`, etc.
   - Must manually include function declarations

6. **Advanced Features**:
   - Bitfields
   - Variable-length arrays
   - Flexible array members
   - Designated initializers
   - Compound literals

### Known Issues

1. **Variable Declarations**:
   - All local variables must be declared at the function start
   - Cannot declare variables in the middle of a block
   - Cannot declare variables in `for` loop initializers

2. **Comments**:
   - Only `#` (shell-style) comments supported
   - C-style `/* */` comments cause parse errors

3. **Literals**:
   - Only decimal integer literals
   - No hexadecimal (`0x`), octal (`0`), or binary literals
   - No character literals (`'a'`)
   - No floating-point literals

4. **Array Initialization**:
   - Cannot initialize arrays at declaration
   - Must use assignment statements

5. **Type System**:
   - Limited type checking
   - Pointer comparisons may not work correctly (uses signed comparison)

## Example Programs

### Fibonacci Numbers
```c
int fibonacci(int n) {
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

main() {
    int i;
    for (i = 0; i < 10; i++)
        printf("fib(%d) = %d\n", i, fibonacci(i));
}
```

### Bitwise Operations
```c
main() {
    int a;
    int b;

    a = 12;  # 1100 binary
    b = 10;  # 1010 binary

    printf("AND: %d\n", a & b);   # 8
    printf("OR:  %d\n", a | b);   # 14
    printf("XOR: %d\n", a ^ b);   # 6
    printf("NOT: %d\n", ~a);      # -13
    printf("SHL: %d\n", a << 2);  # 48
    printf("SHR: %d\n", a >> 2);  # 3
}
```

### Loop Control
```c
main() {
    int i;
    int sum;

    # Using break
    sum = 0;
    i = 0;
    while (1) {
        if (i >= 10)
            break;
        sum = sum + i;
        i++;
    }

    # Using continue
    sum = 0;
    for (i = 0; i < 20; i++) {
        if (i % 2 == 0)
            continue;
        sum = sum + i;
    }

    # Do-while (executes at least once)
    i = 0;
    do {
        printf("%d\n", i);
        i++;
    } while (i < 5);
}
```

## Compilation

### Compile to QBE IL
```bash
./minic < program.c > program.ssa
```

### Compile IL to Assembly
```bash
qbe -t <target> program.ssa > program.s
# Targets: amd64_sysv, arm64, rv64, i8086
```

### Assemble and Link
```bash
# For AMD64 Linux
cc -o program program.s
./program
```

## QBE IL Output

MiniC generates QBE intermediate language, which uses:
- `w` type for 32-bit integers
- `l` type for 64-bit integers and pointers
- SSA (Static Single Assignment) form
- Explicit `alloc`, `load`, `store` operations
- Block-based control flow with labels and jumps

## Implementation Details

- **Line tracking**: Line numbers maintained for error messages
- **Type inference**: Limited type inference based on operations
- **Optimization**: Minimal optimization at frontend level (QBE handles optimization)
- **Code size**: ~950 lines of yacc grammar and C code

## Version History

### Latest (November 2024)
- Added do-while loops
- Added continue statement
- Added bitwise OR (`|`), XOR (`^`), NOT (`~`) operators
- Added shift operators (`<<`, `>>`)
- Added logical NOT (`!`) operator
- Improved control flow for loops with continue support

### Original
- Basic C features: if/else, while, for, break, return
- Arithmetic and comparison operators
- Functions, pointers, arrays
- Global and local variables

## See Also

- [QBE Documentation](http://c9x.me/compile/doc/il.html)
- Test programs in `minic/test/` directory
- Main QBE repository for backend details
