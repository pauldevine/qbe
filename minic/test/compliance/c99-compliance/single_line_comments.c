# SKIP: single-line comments not yet implemented
# C99: single-line comments
# Tests // style comments

main() {
	int x;
	x = 42;

	if (x == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
