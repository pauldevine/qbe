# MiniC Tutorial

This tutorial will guide you through using the MiniC compiler, from writing your first program to understanding advanced features.

## Getting Started

### Your First Program

Create a file called `hello.c`:

```c
main() {
	printf("Hello, World!\n");
}
```

Compile and run it:

```bash
./mcc hello.c
./a.out
```

Output:
```
Hello, World!
```

### Variables and Types

MiniC supports several data types:

```c
main() {
	int x;
	long y;
	char c;
	unsigned int u;
	unsigned long ul;
	unsigned char uc;

	x = 42;
	y = 1000000000;
	c = 'A';
	u = 4294967295;  # Maximum unsigned int
	ul = 18446744073709551615;  # Maximum unsigned long (won't fit, wraps)
	uc = 255;  # Maximum unsigned char

	printf("int: %d\n", x);
	printf("long: %ld\n", y);
	printf("char: %c (value: %d)\n", c, c);
	printf("unsigned: %u\n", u);
}
```

**Important Notes:**
- All local variables must be declared at the beginning of the function
- Cannot declare multiple variables in one line (e.g., `int x, y;`)
- Must declare each variable separately:
  ```c
  int x;
  int y;
  int z;
  ```

### Number Literals

MiniC supports decimal, hexadecimal, and octal literals:

```c
main() {
	int dec;
	int hex;
	int oct;
	char ch;

	dec = 255;      # Decimal
	hex = 0xFF;     # Hexadecimal (same as 255)
	oct = 0377;     # Octal (same as 255)
	ch = 'A';       # Character literal (65)

	printf("All same: %d %d %d\n", dec, hex, oct);
	printf("Character 'A' = %d\n", ch);
}
```

### Escape Sequences

Character literals support escape sequences:

```c
main() {
	char newline;
	char tab;
	char backslash;
	char quote;

	newline = '\n';
	tab = '\t';
	backslash = '\\';
	quote = '\'';

	printf("Line 1%cLine 2\n", newline);
	printf("Column1%cColumn2\n", tab);
}
```

## Control Flow

### If-Else Statements

```c
main() {
	int x;
	x = 10;

	if (x > 5) {
		printf("x is greater than 5\n");
	} else {
		printf("x is not greater than 5\n");
	}

	# Nested if-else
	if (x > 15) {
		printf("Large\n");
	} else if (x > 5) {
		printf("Medium\n");
	} else {
		printf("Small\n");
	}
}
```

### Loops

**While Loop:**
```c
main() {
	int i;
	i = 0;

	while (i < 5) {
		printf("%d\n", i);
		i = i + 1;
	}
}
```

**Do-While Loop:**
```c
main() {
	int i;
	i = 0;

	do {
		printf("%d\n", i);
		i = i + 1;
	} while (i < 5);
}
```

**For Loop:**
```c
main() {
	int i;

	for (i = 0; i < 5; i = i + 1) {
		printf("%d\n", i);
	}
}
```

### Break and Continue

```c
main() {
	int i;

	# Break example
	for (i = 0; i < 10; i = i + 1) {
		if (i == 5)
			break;
		printf("%d ", i);
	}
	printf("\n");

	# Continue example
	for (i = 0; i < 10; i = i + 1) {
		if (i % 2 == 0)
			continue;
		printf("%d ", i);  # Prints odd numbers only
	}
	printf("\n");
}
```

### Switch Statements

```c
main() {
	int day;
	day = 3;

	switch (day) {
		case 1:
			printf("Monday\n");
			break;
		case 2:
			printf("Tuesday\n");
			break;
		case 3:
			printf("Wednesday\n");
			break;
		default:
			printf("Other day\n");
			break;
	}
}
```

**Fall-through behavior:**
```c
main() {
	int x;
	x = 2;

	switch (x) {
		case 1:
		case 2:
		case 3:
			printf("1, 2, or 3\n");
			break;
		case 4:
			printf("4\n");
			# No break - falls through to case 5
		case 5:
			printf("4 or 5\n");
			break;
	}
}
```

## Operators

### Arithmetic Operators

```c
main() {
	int a;
	int b;
	int result;

	a = 10;
	b = 3;

	result = a + b;   # Addition: 13
	result = a - b;   # Subtraction: 7
	result = a * b;   # Multiplication: 30
	result = a / b;   # Division: 3
	result = a % b;   # Modulo: 1

	printf("Results: %d %d %d %d %d\n",
		a + b, a - b, a * b, a / b, a % b);
}
```

### Compound Assignments

```c
main() {
	int x;

	x = 10;
	x += 5;   # x = x + 5;  (15)
	x -= 3;   # x = x - 3;  (12)
	x *= 2;   # x = x * 2;  (24)
	x /= 4;   # x = x / 4;  (6)
	x %= 5;   # x = x % 5;  (1)

	printf("Result: %d\n", x);
}
```

### Bitwise Operators

```c
main() {
	int a;
	int b;

	a = 12;  # 1100 in binary
	b = 10;  # 1010 in binary

	printf("AND: %d\n", a & b);   # 8  (1000)
	printf("OR:  %d\n", a | b);   # 14 (1110)
	printf("XOR: %d\n", a ^ b);   # 6  (0110)
	printf("NOT: %d\n", ~a);      # -13 (bitwise NOT)
	printf("SHL: %d\n", a << 1);  # 24 (shift left)
	printf("SHR: %d\n", a >> 1);  # 6  (shift right)
}
```

**Bitwise Compound Assignments:**
```c
main() {
	int x;

	x = 12;
	x &= 10;  # x = x & 10;
	x |= 3;   # x = x | 3;
	x ^= 5;   # x = x ^ 5;
	x <<= 1;  # x = x << 1;
	x >>= 1;  # x = x >> 1;
}
```

### Increment and Decrement

```c
main() {
	int x;
	int y;

	x = 5;
	y = x++;  # Post-increment: y=5, x=6
	y = ++x;  # Pre-increment:  y=7, x=7

	x = 5;
	y = x--;  # Post-decrement: y=5, x=4
	y = --x;  # Pre-decrement:  y=3, x=3
}
```

### Ternary Operator

```c
main() {
	int x;
	int y;
	int max;

	x = 10;
	y = 20;

	max = x > y ? x : y;  # max = 20
	printf("Max: %d\n", max);

	# Nested ternary
	max = x > y ? x : (y > 15 ? y : 15);
}
```

## Advanced Features

### Structures

```c
struct Point {
	int x;
	int y;
};

main() {
	struct Point p;

	p.x = 10;
	p.y = 20;

	printf("Point: (%d, %d)\n", p.x, p.y);
}
```

### Unions

```c
union Data {
	int i;
	long l;
	char c;
};

main() {
	union Data d;

	d.i = 42;
	printf("As int: %d\n", d.i);

	d.l = 1000;
	printf("As long: %ld\n", d.l);
}
```

### Enumerations

```c
enum Color {
	RED,
	GREEN,
	BLUE
};

enum Status {
	SUCCESS = 0,
	ERROR = -1,
	PENDING = 100
};

main() {
	int color;
	int status;

	color = RED;  # 0
	status = SUCCESS;  # 0

	if (color == GREEN) {
		printf("Green\n");
	}
}
```

### Type Definitions

```c
typedef int myint;
typedef long* longptr;

struct Point {
	int x;
	int y;
};

typedef struct Point Point;

main() {
	myint x;
	longptr p;
	Point pt;

	x = 42;
	pt.x = 10;
	pt.y = 20;
}
```

### Arrays

```c
main() {
	int arr[5];
	int primes[5] = {2, 3, 5, 7, 11};
	int i;

	# Initialize array elements
	arr[0] = 10;
	arr[1] = 20;
	arr[2] = 30;
	arr[3] = 40;
	arr[4] = 50;

	# Access array elements
	for (i = 0; i < 5; i = i + 1) {
		printf("arr[%d] = %d, primes[%d] = %d\n",
			i, arr[i], i, primes[i]);
	}
}
```

### Pointers

```c
main() {
	int x;
	int *p;

	x = 42;
	p = &x;  # Get address of x

	printf("Value: %d\n", x);
	printf("Address: %p\n", p);
	printf("Deref: %d\n", *p);

	*p = 100;  # Modify x through pointer
	printf("New value: %d\n", x);
}
```

**Pointer Arithmetic:**
```c
main() {
	int arr[5] = {10, 20, 30, 40, 50};
	int *p;
	int i;

	p = arr;  # arr decays to pointer

	for (i = 0; i < 5; i = i + 1) {
		printf("%d ", *(p + i));
	}
	printf("\n");
}
```

## Functions

### Function Definition

```c
int add(int a, int b) {
	return a + b;
}

int factorial(int n) {
	if (n <= 1)
		return 1;
	return n * factorial(n - 1);
}

main() {
	int sum;
	int fact;

	sum = add(5, 3);
	fact = factorial(5);

	printf("Sum: %d\n", sum);
	printf("Factorial: %d\n", fact);
}
```

### Forward Declarations

```c
int helper(int x);

main() {
	int result;
	result = helper(10);
	printf("Result: %d\n", result);
}

int helper(int x) {
	return x * 2;
}
```

## Best Practices

### 1. Variable Declaration
Always declare all local variables at the beginning of a function:

```c
# Good
main() {
	int x;
	int y;
	int z;

	x = 10;
	y = 20;
	z = x + y;
}

# Bad - Won't compile
main() {
	int x;
	x = 10;

	int y;  # Error: must be at beginning
	y = 20;
}
```

### 2. Comments

Use `#` for comments (shell-style), not C-style `/* */`:

```c
main() {
	# This is a valid comment
	int x;  # Comments can be at end of line

	/* This is NOT supported and will cause errors */
}
```

### 3. Return Statements

Always return a value from non-void functions:

```c
int calculate(int x) {
	if (x > 0)
		return x * 2;
	return 0;  # Always have a return
}
```

### 4. Type Safety

Be careful with type conversions:

```c
main() {
	int i;
	long l;
	char c;

	i = 300;
	c = i;  # Truncates to 44 (300 % 256)

	c = 100;
	i = c;  # Sign-extends to 100

	i = 100;
	l = i;  # Promotes to long
}
```

## Common Pitfalls

### 1. Forgetting Break in Switch

```c
main() {
	int x;
	x = 1;

	switch (x) {
		case 1:
			printf("One\n");
			# Missing break! Falls through to case 2
		case 2:
			printf("Two\n");
			break;
	}
	# Output: One\nTwo\n
}
```

### 2. Off-by-One Errors in Loops

```c
main() {
	int arr[5] = {1, 2, 3, 4, 5};
	int i;

	# Wrong: accesses arr[5] which is out of bounds
	for (i = 0; i <= 5; i = i + 1) {
		printf("%d\n", arr[i]);
	}

	# Correct
	for (i = 0; i < 5; i = i + 1) {
		printf("%d\n", arr[i]);
	}
}
```

### 3. Uninitialized Variables

```c
main() {
	int x;  # Contains garbage value
	int y;

	y = x + 10;  # Undefined behavior

	# Always initialize
	x = 0;
	y = x + 10;  # Now safe
}
```

## Next Steps

- Read [MINIC_REFERENCE.md](MINIC_REFERENCE.md) for complete language reference
- Read [PORTING_GUIDE.md](PORTING_GUIDE.md) for tips on converting C code to MiniC
- Explore examples in the `test/` directory
- Learn about [QBE intermediate language](http://c9x.me/compile/doc/il.html)

## Getting Help

If you encounter issues:
1. Check that all variables are declared at function start
2. Verify you're using `#` comments, not `/* */`
3. Make sure each variable declaration is on its own line
4. Review the [limitations section](MINIC_REFERENCE.md#limitations) in the reference

Happy coding with MiniC!
