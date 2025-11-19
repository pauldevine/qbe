# Comprehensive test of C features

# Test global variables
int glob1;
int glob2;

# Test function declarations
int fibonacci(int n);
int factorial(int n);

main() {
	int i;
	int j;
	int *ptr;
	int arr[10];
	long lval;

	printf("=== Comprehensive C Feature Test ===\n\n");

	# Test 1: Arithmetic operators
	printf("-- Arithmetic Operators --\n");
	i = 10 + 5;
	printf("10 + 5 = %d\n", i);
	i = 10 - 5;
	printf("10 - 5 = %d\n", i);
	i = 10 * 5;
	printf("10 * 5 = %d\n", i);
	i = 10 / 5;
	printf("10 / 5 = %d\n", i);
	i = 10 % 3;
	printf("10 %% 3 = %d\n", i);

	# Test 2: Comparison operators
	printf("\n-- Comparison Operators --\n");
	printf("10 < 5 = %d\n", 10 < 5);
	printf("10 > 5 = %d\n", 10 > 5);
	printf("10 <= 10 = %d\n", 10 <= 10);
	printf("10 >= 5 = %d\n", 10 >= 5);
	printf("10 == 10 = %d\n", 10 == 10);
	printf("10 != 5 = %d\n", 10 != 5);

	# Test 3: Logical operators
	printf("\n-- Logical Operators --\n");
	printf("1 && 1 = %d\n", 1 && 1);
	printf("1 && 0 = %d\n", 1 && 0);
	printf("1 || 0 = %d\n", 1 || 0);
	printf("0 || 0 = %d\n", 0 || 0);
	printf("!0 = %d\n", !0);
	printf("!1 = %d\n", !1);

	# Test 4: Bitwise operators
	printf("\n-- Bitwise Operators --\n");
	printf("12 & 10 = %d\n", 12 & 10);
	printf("12 | 10 = %d\n", 12 | 10);
	printf("12 ^ 10 = %d\n", 12 ^ 10);
	printf("~0 = %d\n", ~0);
	printf("3 << 2 = %d\n", 3 << 2);
	printf("12 >> 2 = %d\n", 12 >> 2);

	# Test 5: Pointer operations
	printf("\n-- Pointer Operations --\n");
	i = 42;
	ptr = &i;
	printf("i = %d, *ptr = %d\n", i, *ptr);
	*ptr = 84;
	printf("After *ptr = 84: i = %d\n", i);

	# Test 6: Array operations
	printf("\n-- Array Operations --\n");
	for (i = 0; i < 10; i++)
		arr[i] = i * i;
	for (i = 0; i < 10; i++)
		printf("arr[%d] = %d\n", i, arr[i]);

	# Test 7: Control flow - if/else
	printf("\n-- If/Else Statements --\n");
	i = 10;
	if (i > 5)
		printf("i > 5\n");
	else
		printf("i <= 5\n");

	# Test 8: Control flow - while loop
	printf("\n-- While Loop --\n");
	i = 0;
	j = 0;
	while (i < 5) {
		j = j + i;
		i++;
	}
	printf("Sum 0-4 using while: %d\n", j);

	# Test 9: Control flow - do-while loop
	printf("\n-- Do-While Loop --\n");
	i = 0;
	j = 0;
	do {
		j = j + i;
		i++;
	} while (i < 5);
	printf("Sum 0-4 using do-while: %d\n", j);

	# Test 10: Control flow - for loop
	printf("\n-- For Loop --\n");
	j = 0;
	for (i = 0; i < 5; i++)
		j = j + i;
	printf("Sum 0-4 using for: %d\n", j);

	# Test 11: Control flow - break
	printf("\n-- Break Statement --\n");
	i = 0;
	j = 0;
	while (1) {
		if (i >= 5)
			break;
		j = j + i;
		i++;
	}
	printf("Sum 0-4 using break: %d\n", j);

	# Test 12: Control flow - continue
	printf("\n-- Continue Statement --\n");
	j = 0;
	for (i = 0; i < 10; i++) {
		if (i % 2 == 0)
			continue;
		j = j + i;
	}
	printf("Sum of odd numbers 1-9: %d\n", j);

	# Test 13: Increment/decrement operators
	printf("\n-- Increment/Decrement --\n");
	i = 5;
	printf("i = %d\n", i);
	printf("i++ = %d\n", i++);
	printf("i = %d\n", i);
	printf("i-- = %d\n", i--);
	printf("i = %d\n", i);

	# Test 14: Function calls
	printf("\n-- Function Calls --\n");
	printf("fibonacci(10) = %d\n", fibonacci(10));
	printf("factorial(5) = %d\n", factorial(5));

	# Test 15: Long integers
	printf("\n-- Long Integers --\n");
	lval = 1000000;
	lval = lval * lval;
	printf("1000000 * 1000000 = %ld\n", lval);

	# Test 16: Global variables
	printf("\n-- Global Variables --\n");
	glob1 = 123;
	glob2 = 456;
	printf("glob1 = %d, glob2 = %d\n", glob1, glob2);

	# Test 17: sizeof operator
	printf("\n-- Sizeof Operator --\n");
	printf("sizeof(int) = %d\n", sizeof(int));
	printf("sizeof(long) = %d\n", sizeof(long));
	printf("sizeof(int*) = %d\n", sizeof(int*));

	printf("\n=== All tests completed! ===\n");
}

# Fibonacci function
int fibonacci(int n) {
	if (n <= 1)
		return n;
	return fibonacci(n - 1) + fibonacci(n - 2);
}

# Factorial function
int factorial(int n) {
	int result;
	result = 1;
	while (n > 1) {
		result = result * n;
		n--;
	}
	return result;
}
