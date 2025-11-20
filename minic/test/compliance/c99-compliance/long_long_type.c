# SKIP: long long type not yet implemented
# C99: long long type
# Tests 64-bit long long integer support

main() {
	long long x;
	long long y;
	unsigned long long z;

	x = 1000000000000;
	y = -1000000000000;
	z = 18446744073709551615;

	if (x == 1000000000000 && y == -1000000000000) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
