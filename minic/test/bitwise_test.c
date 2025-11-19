# Test bitwise operators

main() {
	int a;
	int b;
	int result;

	# Test 1: Bitwise AND
	a = 12;  # 1100 in binary
	b = 10;  # 1010 in binary
	result = a & b;  # should be 8 (1000)
	printf("12 & 10 = %d (should be 8)\n", result);

	# Test 2: Bitwise OR
	result = a | b;  # should be 14 (1110)
	printf("12 | 10 = %d (should be 14)\n", result);

	# Test 3: Bitwise XOR
	result = a ^ b;  # should be 6 (0110)
	printf("12 ^ 10 = %d (should be 6)\n", result);

	# Test 4: Bitwise NOT
	a = 0;
	result = ~a;  # should be -1
	printf("~0 = %d (should be -1)\n", result);

	a = 255;
	result = ~a;
	printf("~255 = %d\n", result);

	# Test 5: Left shift
	a = 3;
	result = a << 2;  # should be 12
	printf("3 << 2 = %d (should be 12)\n", result);

	# Test 6: Right shift
	a = 24;
	result = a >> 2;  # should be 6
	printf("24 >> 2 = %d (should be 6)\n", result);

	# Test 7: Complex expression
	a = 15;
	b = 7;
	result = (a | b) & (a ^ b);
	printf("(15 | 7) & (15 ^ 7) = %d (should be 8)\n", result);

	# Test 8: Shift and mask
	a = 4660;  # 0x1234
	result = (a >> 4) & 255;
	printf("(4660 >> 4) & 255 = %d (should be 35)\n", result);
}
