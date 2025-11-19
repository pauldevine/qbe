# Debug switch - case with statement before break
main() {
	int x;

	x = 1;
	switch (x) {
	case 1:
		printf("matched\n");
		break;
	}
	printf("after switch\n");
}
