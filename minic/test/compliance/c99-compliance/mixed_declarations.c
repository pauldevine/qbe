# C99: mixed declarations and statements
# Tests declaring variables after statements

main() {
	int x;
	x = 10;

	int y;
	y = 20;

	if (x == 10 && y == 20) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
