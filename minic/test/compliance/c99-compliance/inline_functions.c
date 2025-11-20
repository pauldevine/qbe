# C99: inline functions
# Tests inline function specifier

inline get42() {
	return 42;
}

main() {
	int result;
	result = get42();

	if (result == 42) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
