# Test continue statement

main() {
	int i;
	int sum;

	# Test 1: Continue in while loop
	i = 0;
	sum = 0;
	while (i < 10) {
		i++;
		if (i % 2 == 0)
			continue;
		sum = sum + i;
	}
	printf("Sum of odd numbers 1-9: %d (should be 25)\n", sum);

	# Test 2: Continue in do-while loop
	i = 0;
	sum = 0;
	do {
		i++;
		if (i == 5)
			continue;
		sum = sum + i;
	} while (i < 10);
	printf("Sum 1-10 except 5: %d (should be 50)\n", sum);

	# Test 3: Continue in for loop
	sum = 0;
	for (i = 1; i <= 20; i++) {
		if (i % 3 == 0)
			continue;
		sum = sum + i;
	}
	printf("Sum 1-20 except multiples of 3: %d (should be 140)\n", sum);
}
