main() {
	int x;
	int result;

	result = 0;
	x = 1;

	switch (x) {
	case 1:
		result = result + 1;
	case 2:
		result = result + 10;
		break;
	}

	printf("result = %d\n", result);
}
