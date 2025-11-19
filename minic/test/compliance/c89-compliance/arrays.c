# C89: Array support
# Tests array declaration and subscripting

main() {
	int arr[5];
	int sum;
	int i;

	arr[0] = 10;
	arr[1] = 20;
	arr[2] = 30;
	arr[3] = 40;
	arr[4] = 50;

	sum = 0;
	for (i = 0; i < 5; i = i + 1) {
		sum = sum + arr[i];
	}

	if (sum == 150) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
	}
}
