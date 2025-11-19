# Test typedef declarations
typedef int myint;
typedef long mylong;
typedef char *string;

main() {
	myint x;
	mylong y;
	string s;

	x = 42;
	y = 1000;

	printf("x is myint\n");
	printf("y is mylong\n");
	printf("typedef works\n");
}
