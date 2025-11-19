# Test char type

main() {
	char c;
	int i;
	char *s;

	# Test basic char
	c = 65;
	printf("char c = 65: c = %d\n", c);

	# Test char arithmetic
	c = 10;
	i = c + 5;
	printf("char c = 10; int i = c + 5: i = %d (should be 15)\n", i);

	# Test char comparison
	c = 65;
	if (c == 65)
		printf("char comparison works\n");

	# Test char array (string)
	printf("Char type test complete\n");
}
