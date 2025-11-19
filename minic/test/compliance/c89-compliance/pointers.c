# C89: Pointer support
# Tests pointer declaration, address-of, and dereference

main() {
	int x;
	int *p;
	int y;

	x = 42;
	p = &x;
	y = *p;

	*p = 100;

	if (y == 42 && x == 100) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
