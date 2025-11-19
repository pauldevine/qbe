# Comprehensive test for switch/case/default

main() {
	int x;
	int result;
	int failures;

	failures = 0;

	# Test case 1: exact match
	x = 2;
	result = 0;
	switch (x) {
	case 1:
		result = 100;
		break;
	case 2:
		result = 200;
		break;
	case 3:
		result = 300;
		break;
	default:
		result = 999;
		break;
	}
	if (result != 200) failures = failures + 1;

	# Test case 2: default case
	x = 99;
	result = 0;
	switch (x) {
	case 1:
		result = 100;
		break;
	case 2:
		result = 200;
		break;
	default:
		result = 999;
		break;
	}
	if (result != 999) failures = failures + 1;

	# Test case 3: fall-through
	x = 1;
	result = 0;
	switch (x) {
	case 1:
		result = result + 1;
	case 2:
		result = result + 10;
		break;
	case 3:
		result = 300;
		break;
	}
	if (result != 11) failures = failures + 1;

	if (failures == 0)
		printf("PASS: switch test\n");
	else
		printf("FAIL: switch test\n");
}
