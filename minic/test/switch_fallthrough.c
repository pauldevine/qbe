# Test switch fall-through behavior
main() {
	int x;

	# Test fall-through: case 1 should execute both case 1 and case 2
	printf("Test 1: x=1 with fall-through\n");
	x = 1;
	switch (x) {
	case 1:
		printf("  case 1 executed\n");
	case 2:
		printf("  case 2 executed\n");
		break;
	case 3:
		printf("  case 3 executed\n");
		break;
	}

	# Test with break: case 2 should only execute case 2
	printf("Test 2: x=2 with break\n");
	x = 2;
	switch (x) {
	case 1:
		printf("  case 1 executed\n");
		break;
	case 2:
		printf("  case 2 executed\n");
		break;
	case 3:
		printf("  case 3 executed\n");
		break;
	}

	# Test default
	printf("Test 3: x=99 goes to default\n");
	x = 99;
	switch (x) {
	case 1:
		printf("  case 1 executed\n");
		break;
	case 2:
		printf("  case 2 executed\n");
		break;
	default:
		printf("  default executed\n");
		break;
	}

	printf("All tests complete\n");
}
