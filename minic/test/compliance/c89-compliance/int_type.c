# C89: int type
# Tests basic int type support

main() {
	int x;
	int y;
	int z;

	x = 42;
	y = -100;
	z = 0;

	if (x == 42 && y == -100 && z == 0) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
