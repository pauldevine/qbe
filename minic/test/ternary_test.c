# Test ternary operator

main() {
	int a;
	int b;
	int max;

	# Test basic ternary
	a = 10;
	b = 20;
	max = a > b ? a : b;
	printf("max(10, 20) = %d (should be 20)\n", max);

	# Test nested ternary
	a = 5;
	b = 10;
	max = a > b ? a : b > 15 ? b : 15;
	printf("Result: %d\n", max);

	# Test ternary in expression
	a = 7;
	b = (a > 5 ? 100 : 50) + 23;
	printf("(7 > 5 ? 100 : 50) + 23 = %d (should be 123)\n", b);

	# Test with different types
	a = 1;
	b = a ? 42 : 0;
	printf("1 ? 42 : 0 = %d (should be 42)\n", b);
}
