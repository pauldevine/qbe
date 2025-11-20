# C89: goto and labels
# Tests goto statement and label support

main() {
	int x;

	x = 0;

	if (x == 0)
		goto skip;

	x = 999;

skip:
	x = x + 1;

	if (x == 1) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
