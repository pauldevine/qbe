main() {
	int failures;
	int hex;
	int oct;
	char a;
	char b;
	int result;

	failures = 0;

	# Test 1: Character literals with ternary operator
	a = 'A';
	b = 'Z';
	result = a < b ? 1 : 0;
	if (result != 1) failures = failures + 1;

	# Test 2: Hex literals with compound assignment
	hex = 0xFF;
	hex += 0x1;
	if (hex != 256) failures = failures + 1;

	# Test 3: Octal literals with bitwise operations
	oct = 0755;
	oct &= 0xFF;
	if (oct != 237) failures = failures + 1;

	# Test 4: Character escape sequences with comparisons
	a = '\n';
	if (a != 10) failures = failures + 1;

	# Test 5: Mixed operators and literals
	result = (0x10 + 0x20) * 2;
	result -= 'A';
	if (result != 31) failures = failures + 1;

	# Test 6: Ternary with compound assignment
	result = 100;
	result += result > 50 ? 10 : 5;
	if (result != 110) failures = failures + 1;

	# Test 7: Character literals in expressions
	result = 'Z' - 'A';
	if (result != 25) failures = failures + 1;

	if (failures == 0) {
		printf("PASS: integration test\n");
	} else {
		printf("FAIL: integration test (%d failures)\n", failures);
	}
}
