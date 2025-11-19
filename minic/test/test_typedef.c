# Comprehensive test for typedef

typedef int myint;
typedef long mylong;
typedef char *string;
typedef myint *intptr;

main() {
	myint x;
	mylong y;
	int z;
	intptr p;
	int failures;

	failures = 0;

	x = 42;
	y = 1000;
	z = 123;

	if (x != 42) failures = failures + 1;
	if (y != 1000) failures = failures + 1;

	# Test that typedef and base type are compatible
	z = x;
	if (z != 42) failures = failures + 1;

	if (failures == 0)
		printf("PASS: typedef test\n");
	else
		printf("FAIL: typedef test\n");
}
