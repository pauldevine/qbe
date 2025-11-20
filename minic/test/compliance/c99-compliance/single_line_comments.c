# C99: single-line comments
# Tests // style comments

main() {
	int x; // declare x
	x = 42; // set to 42
	// this is a full line comment
	if (x == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n"); // should not reach here
	}
}
