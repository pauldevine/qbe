# Comprehensive test for unsigned types

main() {
	unsigned int ux;
	unsigned long uy;
	int failures;

	failures = 0;

	ux = 100;
	uy = 200;

	if (ux > 50) {
		failures = failures + 0;
	} else {
		failures = failures + 1;
	}

	if (uy > 100) {
		failures = failures + 0;
	} else {
		failures = failures + 1;
	}

	if (failures == 0) {
		printf("PASS: unsigned test\n");
	} else {
		printf("FAIL: unsigned test\n");
	}
}
