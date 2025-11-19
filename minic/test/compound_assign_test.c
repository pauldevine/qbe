# Test compound assignment operators

main() {
	int a;
	int b;

	# Test +=
	a = 10;
	a += 5;
	printf("10 += 5 = %d (should be 15)\n", a);

	# Test -=
	a = 20;
	a -= 8;
	printf("20 -= 8 = %d (should be 12)\n", a);

	# Test *=
	a = 6;
	a *= 7;
	printf("6 *= 7 = %d (should be 42)\n", a);

	# Test /=
	a = 100;
	a /= 5;
	printf("100 /= 5 = %d (should be 20)\n", a);

	# Test modulo-equals
	a = 17;
	a %= 5;
	printf("17 mod= 5 = %d (should be 2)\n", a);

	# Test &=
	a = 12;
	a &= 10;
	printf("12 &= 10 = %d (should be 8)\n", a);

	# Test |=
	a = 12;
	a |= 10;
	printf("12 |= 10 = %d (should be 14)\n", a);

	# Test ^=
	a = 12;
	a ^= 10;
	printf("12 ^= 10 = %d (should be 6)\n", a);

	# Test <<=
	a = 3;
	a <<= 2;
	printf("3 <<= 2 = %d (should be 12)\n", a);

	# Test >>=
	a = 24;
	a >>= 2;
	printf("24 >>= 2 = %d (should be 6)\n", a);

	# Test chained compound assignments
	a = 5;
	b = 10;
	a += b *= 2;
	printf("a = 5; b = 10; a += b *= 2: a = %d, b = %d (should be 25, 20)\n", a, b);
}
