# SKIP: inline keyword not yet implemented
# C99: inline functions
# Tests inline function specifier

inline int add(int a, int b) {
	return a + b;
}

main() {
	int result;
	result = add(10, 20);

	if (result == 30) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
