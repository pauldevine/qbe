# Simple switch test with explicit breaks
main() {
	int x;

	x = 2;
	switch (x) {
	case 1:
		printf("one\n");
		break;
	case 2:
		printf("two\n");
		break;
	default:
		printf("other\n");
		break;
	}
	printf("done\n");
}
