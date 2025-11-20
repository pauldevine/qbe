# Porting C Code to MiniC

This guide helps you convert existing C programs to work with MiniC, a subset of C designed for the QBE intermediate language compiler.

## Quick Checklist

Before porting, make sure your C code:
- [ ] Uses only supported types (int, long, char, unsigned variants, void)
- [ ] Doesn't use the preprocessor (#include, #define, etc.)
- [ ] Uses only `#` comments (not `/* */`)
- [ ] Declares all local variables at function start
- [ ] Doesn't use unsupported features (see below)

## Step-by-Step Porting Process

### Step 1: Remove Preprocessor Directives

**Original C:**
```c
#include <stdio.h>
#include <stdlib.h>
#define MAX 100
#define MIN(a, b) ((a) < (b) ? (a) : (b))

int main() {
	int x = MIN(10, 20);
	return 0;
}
```

**MiniC Version:**
```c
# Forward declare functions you need
int printf(char *fmt, ...);

main() {
	int x;
	int a;
	int b;

	a = 10;
	b = 20;
	x = a < b ? a : b;  # Inline the macro
}
```

**Tips:**
- Remove all `#include` statements
- Manually declare external functions like `printf`
- Replace `#define` constants with enum or variables
- Expand macros manually

### Step 2: Convert Comments

**Original C:**
```c
/* This is a C comment */
int x;  /* inline comment */

// C++ style comment
int y;
```

**MiniC Version:**
```c
# This is a MiniC comment
int x;  # inline comment

# MiniC only supports # comments
int y;
```

### Step 3: Move Variable Declarations

**Original C:**
```c
int main() {
	int x = 10;
	printf("%d\n", x);

	int y = 20;  /* Can declare anywhere in C99+ */
	printf("%d\n", y);

	for (int i = 0; i < 10; i++) {  /* Loop variable */
		printf("%d\n", i);
	}
}
```

**MiniC Version:**
```c
main() {
	int x;
	int y;
	int i;  # All declarations at the top

	x = 10;
	printf("%d\n", x);

	y = 20;
	printf("%d\n", y);

	for (i = 0; i < 10; i = i + 1) {
		printf("%d\n", i);
	}
}
```

### Step 4: Split Multiple Declarations

**Original C:**
```c
int x, y, z;
int *p, **pp;
int arr[10], brr[20];
```

**MiniC Version:**
```c
int x;
int y;
int z;
int *p;
int **pp;
int arr[10];
int brr[20];
```

### Step 5: Handle Unsupported Types

**Original C:**
```c
#include <stdint.h>
#include <stdbool.h>

int main() {
	short s = 100;
	uint32_t x = 42;
	int64_t y = 1000;
	bool flag = true;
	float f = 3.14;
	double d = 2.718;
}
```

**MiniC Version:**
```c
main() {
	int s;  # short -> int
	unsigned int x;  # uint32_t -> unsigned int
	long y;  # int64_t -> long
	int flag;  # bool -> int (use 0/1)
	# float and double are not supported
	# Use fixed-point arithmetic or omit

	s = 100;
	x = 42;
	y = 1000;
	flag = 1;  # true
}
```

**Type Conversion Table:**
| C Type | MiniC Equivalent | Notes |
|--------|------------------|-------|
| `short` | `int` | May waste space |
| `uint8_t` | `unsigned char` | ✓ Direct match |
| `uint16_t` | `unsigned int` | May be larger |
| `uint32_t` | `unsigned int` | ✓ Direct match (32-bit) |
| `uint64_t` | `unsigned long` | ✓ Direct match (64-bit) |
| `size_t` | `unsigned long` | Platform-dependent |
| `bool` | `int` | Use 0 (false) and 1 (true) |
| `float`/`double` | N/A | Not supported |

### Step 6: Replace Compound Literals

**Original C:**
```c
struct Point {
	int x, y;
};

int main() {
	struct Point p = {10, 20};
	struct Point q = (struct Point){30, 40};
}
```

**MiniC Version:**
```c
struct Point {
	int x;
	int y;
};

main() {
	struct Point p;
	struct Point q;

	# Initialize separately
	p.x = 10;
	p.y = 20;

	q.x = 30;
	q.y = 40;
}
```

### Step 7: Convert String Functions

Most string functions from `<string.h>` are not available. You'll need to implement them or work around them.

**Original C:**
```c
#include <string.h>

int main() {
	char str[100];
	strcpy(str, "hello");
	int len = strlen(str);
	if (strcmp(str, "hello") == 0) {
		/* ... */
	}
}
```

**MiniC Version:**
```c
# Implement needed string functions
int mystrcmp(char *s1, char *s2) {
	int i;
	i = 0;
	while (s1[i] != 0 && s2[i] != 0) {
		if (s1[i] != s2[i])
			return s1[i] - s2[i];
		i = i + 1;
	}
	return s1[i] - s2[i];
}

main() {
	char *str;  # Use string literal
	str = "hello";

	# Use printf for output instead
	printf("%s\n", str);
}
```

### Step 8: Handle Unsupported Control Flow

**Goto statements are not supported:**

**Original C:**
```c
int main() {
	int i = 0;
loop:
	printf("%d\n", i);
	i++;
	if (i < 10)
		goto loop;
	return 0;
}
```

**MiniC Version:**
```c
main() {
	int i;
	i = 0;

	while (i < 10) {
		printf("%d\n", i);
		i = i + 1;
	}
}
```

### Step 9: Replace Function Pointers

**Original C:**
```c
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int main() {
	int (*op)(int, int);
	op = add;
	int result = op(5, 3);
}
```

**MiniC Version:**
```c
int add(int a, int b) {
	return a + b;
}

int sub(int a, int b) {
	return a - b;
}

main() {
	int op;  # Use integer to select operation
	int result;
	int a;
	int b;

	a = 5;
	b = 3;
	op = 0;  # 0 for add, 1 for sub

	if (op == 0)
		result = add(a, b);
	else
		result = sub(a, b);
}
```

### Step 10: Static and Global Variables

**Original C:**
```c
static int counter = 0;

int increment() {
	static int internal = 0;
	internal++;
	counter++;
	return counter;
}
```

**MiniC Version:**
```c
int counter;  # Static is not supported, use global
int internal;

int increment() {
	internal = internal + 1;
	counter = counter + 1;
	return counter;
}

main() {
	counter = 0;
	internal = 0;
	# ... use increment()
}
```

## Common Patterns

### Pattern 1: Min/Max Functions

**Original:**
```c
#define MAX(a, b) ((a) > (b) ? (a) : (b))
int x = MAX(10, 20);
```

**MiniC:**
```c
int max(int a, int b) {
	return a > b ? a : b;
}

main() {
	int x;
	x = max(10, 20);
}
```

### Pattern 2: Boolean Flags

**Original:**
```c
#include <stdbool.h>
bool found = false;
if (x == target)
	found = true;
```

**MiniC:**
```c
main() {
	int found;
	int x;
	int target;

	found = 0;  # false
	target = 42;
	x = 42;

	if (x == target)
		found = 1;  # true
}
```

### Pattern 3: Const Variables

**Original:**
```c
const int MAX_SIZE = 100;
const char *name = "John";
```

**MiniC:**
```c
enum {
	MAX_SIZE = 100
};

main() {
	char *name;
	name = "John";  # String literals are read-only anyway
}
```

### Pattern 4: Void Functions

**Original:**
```c
void print_header() {
	printf("=====\n");
}
```

**MiniC:**
```c
void print_header() {
	printf("=====\n");
	return;  # Void functions still need return
}
```

## Feature Compatibility Matrix

| Feature | C Standard | MiniC Support | Alternative |
|---------|-----------|---------------|-------------|
| `int`, `long`, `char` | C89 | ✓ Yes | - |
| `unsigned` types | C89 | ✓ Yes | - |
| `short` | C89 | ✗ No | Use `int` |
| `float`, `double` | C89 | ✗ No | Fixed-point or integers |
| `struct`, `union` | C89 | ✓ Yes | - |
| `enum` | C89 | ✓ Yes | - |
| `typedef` | C89 | ✓ Yes | - |
| Arrays | C89 | ✓ Yes | - |
| Pointers | C89 | ✓ Yes | - |
| `if`/`else` | C89 | ✓ Yes | - |
| `while`/`do-while` | C89 | ✓ Yes | - |
| `for` | C89 | ✓ Yes | - |
| `switch`/`case` | C89 | ✓ Yes | - |
| `break`/`continue` | C89 | ✓ Yes | - |
| `goto` | C89 | ✗ No | Use loops |
| `? :` ternary | C89 | ✓ Yes | - |
| Compound assignments | C89 | ✓ Yes | - |
| Function pointers | C89 | ✗ No | Use switch/if |
| `const` | C89 | ✗ No | Use enum or comments |
| `static` | C89 | ✗ No | Use globals |
| `extern` | C89 | ✗ No | Forward declare |
| `#include` | C89 | ✗ No | Manual declarations |
| `#define` | C89 | ✗ No | Use enum or expand |
| `/* */` comments | C89 | ✗ No | Use `#` |
| Hex/octal literals | C89 | ✓ Yes | - |
| Character literals | C89 | ✓ Yes | - |
| Escape sequences | C89 | ✓ Yes (`\n`, `\t`, etc.) | - |

## Testing Your Port

After porting, test your code:

```bash
# Compile your ported code
./mcc your_program.c

# Run it
./a.out

# If it compiles but crashes, check:
# 1. Array bounds
# 2. Uninitialized variables
# 3. Pointer dereferencing
```

## Example: Complete Port

### Original C Program

```c
#include <stdio.h>
#include <stdlib.h>
#define SIZE 10

typedef struct {
	int x, y;
} Point;

static Point points[SIZE];
static int count = 0;

void addPoint(int x, int y) {
	if (count < SIZE) {
		points[count].x = x;
		points[count].y = y;
		count++;
	}
}

int main(void) {
	int i;
	addPoint(10, 20);
	addPoint(30, 40);

	for (i = 0; i < count; i++) {
		printf("Point %d: (%d, %d)\n", i, points[i].x, points[i].y);
	}

	return 0;
}
```

### Ported to MiniC

```c
# Forward declarations
int printf(char *fmt, ...);

# Type definitions
struct Point {
	int x;
	int y;
};

typedef struct Point Point;

# Globals (instead of static)
enum { SIZE = 10 };
Point points[SIZE];
int count;

void addPoint(int x, int y) {
	if (count < SIZE) {
		points[count].x = x;
		points[count].y = y;
		count = count + 1;
	}
	return;
}

main() {
	int i;

	# Initialize globals
	count = 0;

	addPoint(10, 20);
	addPoint(30, 40);

	for (i = 0; i < count; i = i + 1) {
		printf("Point %d: (%d, %d)\n", i, points[i].x, points[i].y);
	}
}
```

## Tips for Success

1. **Start Simple**: Port small functions first before tackling the whole program
2. **Test Incrementally**: Compile and test after each major change
3. **Keep a Compatibility Layer**: Create a header with common patterns you've converted
4. **Document Assumptions**: Note where you've made simplifications
5. **Watch for Integer Sizes**: Be aware that `int` is 32-bit and `long` is 64-bit in MiniC
6. **Initialize Everything**: MiniC doesn't zero-initialize local variables

## Further Reading

- [MINIC_REFERENCE.md](MINIC_REFERENCE.md) - Complete language reference
- [TUTORIAL.md](TUTORIAL.md) - Learn MiniC from scratch
- [QBE Documentation](http://c9x.me/compile/doc/il.html) - Understanding the backend
- `test/` directory - Examples of working MiniC code

## Getting Help

If you encounter issues during porting:

1. **Compilation Errors**: Check the error message line number and verify syntax
2. **Runtime Crashes**: Add `printf` debugging to track execution
3. **Wrong Output**: Verify type conversions and integer overflow behavior
4. **Performance Issues**: MiniC doesn't optimize much; QBE handles that

Remember: MiniC is designed for learning and experimentation. Not all C programs need to be ported - use it for programs that fit its capabilities!
