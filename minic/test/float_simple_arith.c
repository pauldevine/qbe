# Simple float arithmetic test

main() {
    float f1;
    float f2;
    float result;

    printf("Float Arithmetic Test\n");

    f1 = 10.0;
    f2 = 5.0;
    result = f1 + f2;
    printf("10.0 + 5.0 = 15.0\n");

    result = f1 - f2;
    printf("10.0 - 5.0 = 5.0\n");

    result = f1 * f2;
    printf("10.0 * 5.0 = 50.0\n");

    result = f1 / f2;
    printf("10.0 / 5.0 = 2.0\n");

    printf("All basic operations work!\n");
    return 0;
}
