# C99: _Bool type
# Tests boolean type support

main() {
	_Bool x;
	_Bool y;

	x = 1;
	y = 0;

	if (x && !y) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
