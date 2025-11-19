# Test do-while loops

main() {
	int i;
	int j;
	int sum;

	# Test 1: Basic do-while
	i = 0;
	sum = 0;
	do {
		sum = sum + i;
		i++;
	} while (i < 10);
	printf("Sum 0-9: %d (should be 45)\n", sum);

	# Test 2: do-while executes at least once
	i = 100;
	do {
		printf("Executed once even though i=%d >= 10\n", i);
		i = i + 1;
	} while (i < 10);

	# Test 3: Nested do-while
	i = 0;
	sum = 0;
	do {
		j = 0;
		do {
			sum++;
			j++;
		} while (j < 3);
		i++;
	} while (i < 4);
	printf("Nested do-while sum: %d (should be 12)\n", sum);
}
