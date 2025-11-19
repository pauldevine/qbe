# SKIP: short type not yet implemented
# C89: short type
# Tests basic short type support

main() {
	short x;
	short y;

	x = 100;
	y = -100;

	if (x == 100 && y == -100) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
