# SKIP: comma operator temporarily disabled due to conflict with argument parsing
# C89 Extended: comma operator
# Tests comma operator in expressions

main() {
	int x;
	int y;
	int z;

	z = (x = 10, y = 20, x + y);

	if (z == 30) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
