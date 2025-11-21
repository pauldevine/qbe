# C89 Extended: block comments
# Tests /* */ style comments

main() {
	int x;
	/* This is a block comment */
	x = 42;
	/* Multi-line
	   block comment
	   test */
	if (x == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n"); /* inline block comment */
	}
}
