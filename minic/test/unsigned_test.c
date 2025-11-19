# Test unsigned types
main() {
	unsigned int ux;
	unsigned long uy;
	unsigned char uc;
	int sx;

	ux = 100;
	uy = 200;
	uc = 50;
	sx = -5;

	if (ux > 50)
		printf("unsigned int works\n");

	if (uy < 300)
		printf("unsigned long works\n");

	if (uc > 25)
		printf("unsigned char works\n");

	if (sx < 0)
		printf("signed int works\n");

	printf("done\n");
}
