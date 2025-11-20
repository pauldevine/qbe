# C89 Extended: function pointers
# Tests function pointer declarations and calls

add(int a, int b) {
	return a + b;
}

main() {
	int result;
	int (*fptr)(int, int);

	fptr = add;
	result = (*fptr)(10, 20);

	if (result == 30) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
