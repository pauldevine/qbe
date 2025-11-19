# SKIP: const qualifier not yet implemented
# C89: const qualifier
# Tests const type qualifier

main() {
	const int x = 42;
	int y;

	y = x;

	if (y == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
