# Comprehensive test for enum support

enum Color {
	RED,
	GREEN,
	BLUE
};

enum Size {
	SMALL = 10,
	MEDIUM = 20,
	LARGE = 30
};

main() {
	int r;
	int g;
	int b;
	int s;
	int m;
	int l;
	int failures;

	failures = 0;

	r = RED;
	g = GREEN;
	b = BLUE;

	if (r != 0) failures = failures + 1;
	if (g != 1) failures = failures + 1;
	if (b != 2) failures = failures + 1;

	s = SMALL;
	m = MEDIUM;
	l = LARGE;

	if (s != 10) failures = failures + 1;
	if (m != 20) failures = failures + 1;
	if (l != 30) failures = failures + 1;

	if (failures == 0)
		printf("PASS: enum test\n");
	else
		printf("FAIL: enum test\n");
}
