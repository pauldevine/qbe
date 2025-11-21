# Simple float comparison test

main() {
    float a;
    float b;

    printf("Float Comparison Test\n\n");

    a = 10.0;
    b = 5.0;

    if (a > b)
        printf("  10.0 > 5.0: PASS\n");
    else
        printf("  10.0 > 5.0: FAIL\n");

    if (b < a)
        printf("  5.0 < 10.0: PASS\n");
    else
        printf("  5.0 < 10.0: FAIL\n");

    if (a >= b)
        printf("  10.0 >= 5.0: PASS\n");
    else
        printf("  10.0 >= 5.0: FAIL\n");

    if (b <= a)
        printf("  5.0 <= 10.0: PASS\n");
    else
        printf("  5.0 <= 10.0: FAIL\n");

    a = 5.0;
    if (a == b)
        printf("  5.0 == 5.0: PASS\n");
    else
        printf("  5.0 == 5.0: FAIL\n");

    a = 10.0;
    if (a != b)
        printf("  10.0 != 5.0: PASS\n");
    else
        printf("  10.0 != 5.0: FAIL\n");

    printf("\nAll comparison tests completed\n");
    return 0;
}
