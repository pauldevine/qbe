main() {
	int arr1[5] = {1, 2, 3, 4, 5};
	int arr2[3];
	int i;

	arr2[0] = 10;
	arr2[1] = 20;
	arr2[2] = 30;

	i = arr1[2];
	if (i > 2)
		printf("arr1[2] correct\n");

	i = arr2[1];
	if (i > 15)
		printf("arr2[1] correct\n");

	printf("done\n");
}
