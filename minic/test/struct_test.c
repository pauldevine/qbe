# Test struct definitions and member access
struct Point {
	int x;
	int y;
};

main() {
	struct Point p;

	p.x = 10;
	p.y = 20;

	if (p.x > 0)
		printf("p.x is positive\n");

	if (p.y > p.x)
		printf("p.y > p.x\n");

	printf("struct test complete\n");
}
