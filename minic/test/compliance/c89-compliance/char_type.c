# C89: char type
# Tests basic char type support

main() {
	char x;
	char y;
	char z;

	x = 'A';
	y = 65;
	z = '\n';

	if (x == 65 && y == 'A' && z == 10) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
