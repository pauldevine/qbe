# Test prefix increment and decrement

main() {
	int i;
	int j;

	# Test prefix increment
	i = 5;
	j = ++i;
	printf("i = 5; j = ++i: i = %d, j = %d (should be 6, 6)\n", i, j);

	# Test prefix decrement
	i = 10;
	j = --i;
	printf("i = 10; j = --i: i = %d, j = %d (should be 9, 9)\n", i, j);

	# Compare with postfix
	i = 5;
	j = i++;
	printf("i = 5; j = i++: i = %d, j = %d (should be 6, 5)\n", i, j);

	i = 10;
	j = i--;
	printf("i = 10; j = i--: i = %d, j = %d (should be 9, 10)\n", i, j);

	# Test in expression
	i = 5;
	j = ++i + ++i;
	printf("i = 5; j = ++i + ++i: j = %d\n", j);
}
