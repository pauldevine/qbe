# C89: const qualifier
# Tests const type qualifier

main() {
	const int x;
	int y;

	x = 42;
	y = x;

	if (y == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
