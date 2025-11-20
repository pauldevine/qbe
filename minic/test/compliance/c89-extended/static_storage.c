# C89 Extended: static storage class
# Tests static keyword parsing

main() {
	static int x;
	x = 42;

	if (x == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
