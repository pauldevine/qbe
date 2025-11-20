# C99: long long type
# Tests 64-bit long long integer support

main() {
	long long x;
	long long y;
	unsigned long long z;

	x = 42;
	y = -100;
	z = 65535;

	if (x == 42 && y == -100 && z == 65535) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
