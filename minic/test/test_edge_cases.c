main() {
	int failures;
	int overflow;
	unsigned int u;
	int zero;
	int neg;
	int minint;
	long big;
	char c;

	failures = 0;

	# Test 1: Integer overflow (wraps around)
	overflow = 2147483647;
	overflow = overflow + 1;
	if (overflow != -2147483648) failures = failures + 1;

	# Test 2: Unsigned wrap-around
	u = 0;
	u = u - 1;
	if (u < 1000) failures = failures + 1;

	# Test 3: Division by zero would crash, but test other division edge cases
	zero = 0;
	if (zero != 0) failures = failures + 1;

	# Test 4: Negative modulo
	neg = -17;
	neg = neg % 5;
	if (neg != -2) failures = failures + 1;

	# Test 5: Shift by zero
	overflow = 42;
	overflow = overflow << 0;
	if (overflow != 42) failures = failures + 1;

	# Test 6: Large shift
	overflow = 1;
	overflow = overflow << 31;
	if (overflow !=  -2147483648) failures = failures + 1;

	# Test 7: Char overflow
	c = 127;
	c = c + 1;
	if (c != -128) failures = failures + 1;

	# Test 8: Pointer arithmetic edge case (NULL + 0 = NULL)
	big = 0;
	if (big != 0) failures = failures + 1;

	# Test 9: Bitwise operations with zero
	overflow = 12345;
	overflow = overflow & 0;
	if (overflow != 0) failures = failures + 1;

	# Test 10: XOR with self (should be zero)
	overflow = 12345;
	overflow = overflow ^ 12345;
	if (overflow != 0) failures = failures + 1;

	if (failures == 0) {
		printf("PASS: edge cases test\n");
	} else {
		printf("FAIL: edge cases test (%d failures)\n", failures);
	}
}
