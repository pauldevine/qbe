# Test arithmetic operations for i8086 backend
# Phase 0 - Integer-only tests

add(int a, int b) {
	return a + b;
}

subtract(int a, int b) {
	return a - b;
}

multiply(int a, int b) {
	return a * b;
}

divide(int a, int b) {
	return a / b;
}

main() {
	int x;
	int y;
	int result;

	# Test addition
	x = 10;
	y = 20;
	result = add(x, y);

	# Test subtraction
	result = subtract(result, 5);

	# Test multiplication
	result = multiply(result, 2);

	# Test division
	result = divide(result, 10);

	# Return the final result (should be 5)
	return result;
}
