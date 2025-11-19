# Test comma operator

main() {
	int a;
	int b;
	int c;

	# Test basic comma
	a = (b = 5, b + 10);
	printf("a = (b = 5, b + 10): a = %d, b = %d (should be 15, 5)\n", a, b);

	# Test in for loop
	for (a = 0, b = 10; a < 5; a++, b--)
		c = a + b;
	printf("After loop: a = %d, b = %d, c = %d (should be 5, 5, 9)\n", a, b, c);

	# Test multiple commas
	a = (b = 1, c = 2, b + c);
	printf("a = (b = 1, c = 2, b + c): a = %d (should be 3)\n", a);
}
