# C89 Extended: extern storage class
# Tests extern keyword parsing

main() {
	extern int x;
	int y;

	y = 42;

	if (y == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
