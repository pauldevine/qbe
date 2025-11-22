# Simple floating-point test
# Test basic FP operations with 8087

float test_add() {
	float a;
	float b;
	float c;

	a = 2.0;
	b = 3.0;
	c = a + b;  # Should be 5.0

	return c;
}

main() {
	float result;
	result = test_add();
	# For now, just return 0 - we'll verify assembly generation
	return 0;
}
