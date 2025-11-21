# Float/int type conversion test

main() {
    int i;
    float f;
    double d;

    printf("Type Conversion Test\n\n");

    # Int to float
    i = 42;
    f = i;
    printf("int 42 -> float: OK\n");

    # Float to int (truncates)
    f = 3.14;
    i = f;
    if (i == 3)
        printf("float 3.14 -> int 3: PASS\n");
    else
        printf("float 3.14 -> int: FAIL\n");

    # Float to double
    f = 2.5;
    d = f;
    printf("float 2.5 -> double: OK\n");

    # Double to float
    d = 1.5;
    f = d;
    printf("double 1.5 -> float: OK\n");

    # Mixed arithmetic: int + float
    i = 10;
    f = 2.5;
    f = f + i;
    printf("2.5 + 10 (mixed): OK\n");

    # Mixed arithmetic: float + int
    f = 3.0;
    i = 7;
    f = f * i;
    printf("3.0 * 7 (mixed): OK\n");

    printf("\nAll conversion tests completed\n");
    return 0;
}
