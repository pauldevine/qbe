# Comprehensive test for struct and union

struct Point {
	int x;
	int y;
};

union Data {
	int i;
	long l;
};

main() {
	struct Point p;
	union Data d;
	int failures;

	failures = 0;

	p.x = 10;
	p.y = 20;

	if (p.x != 10) failures = failures + 1;
	if (p.y != 20) failures = failures + 1;

	d.i = 42;
	if (d.i != 42) failures = failures + 1;

	d.l = 100;
	if (d.l != 100) failures = failures + 1;

	if (failures == 0) {
		printf("PASS: struct and union test\n");
	} else {
		printf("FAIL: struct and union test\n");
	}
}
