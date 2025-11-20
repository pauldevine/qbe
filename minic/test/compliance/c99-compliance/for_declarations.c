# SKIP: for-loop declarations not yet implemented
# C99: for-loop initial declarations
# Tests declaring variables in for-loop initializer

main() {
	int sum;
	sum = 0;

	for (int i = 0; i < 5; i = i + 1) {
		sum = sum + i;
	}

	if (sum == 10) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
