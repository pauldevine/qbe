# C89: long type
# Tests basic long type support

main() {
	long x;
	long y;

	x = 1000000000;
	y = -1000000000;

	if (x == 1000000000 && y == -1000000000) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
