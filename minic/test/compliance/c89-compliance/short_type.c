# C89: short type
# Tests basic short type support

main() {
	short x;
	short y;
	unsigned short z;

	x = 100;
	y = -100;
	z = 65535;

	if (x == 100 && y == -100 && z == 65535) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
