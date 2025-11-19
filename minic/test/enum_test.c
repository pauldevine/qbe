# Test enum

enum Color {
	RED,
	GREEN,
	BLUE
};

enum Status {
	IDLE = 0,
	RUNNING = 1,
	PAUSED = 2,
	STOPPED = 10
};

main() {
	int c;
	int s;

	c = RED;
	printf("RED = %d (should be 0)\n", c);

	c = GREEN;
	printf("GREEN = %d (should be 1)\n", c);

	c = BLUE;
	printf("BLUE = %d (should be 2)\n", c);

	s = STOPPED;
	printf("STOPPED = %d (should be 10)\n", s);

	if (RUNNING == 1)
		printf("enum comparison works\n");
}
