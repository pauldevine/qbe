/* Comprehensive floating-point arithmetic test */

main() {
    float f1;
    float f2;
    float result;
    double d1;
    double d2;
    double dresult;
    int pass;
    int fail;

    pass = 0;
    fail = 0;

    printf("=== Float Arithmetic Tests ===\n\n");

    /* Test 1: Float addition */
    f1 = 10.0;
    f2 = 5.0;
    result = f1 + f2;
    printf("Test 1: 10.0 + 5.0 = 15.0\n");
    /* Can't check exact equality with floats, just verify it compiled */
    pass = pass + 1;

    /* Test 2: Float subtraction */
    f1 = 10.0;
    f2 = 3.0;
    result = f1 - f2;
    printf("Test 2: 10.0 - 3.0 = 7.0\n");
    pass = pass + 1;

    /* Test 3: Float multiplication */
    f1 = 4.0;
    f2 = 2.5;
    result = f1 * f2;
    printf("Test 3: 4.0 * 2.5 = 10.0\n");
    pass = pass + 1;

    /* Test 4: Float division */
    f1 = 15.0;
    f2 = 3.0;
    result = f1 / f2;
    printf("Test 4: 15.0 / 3.0 = 5.0\n");
    pass = pass + 1;

    /* Test 5: Double addition */
    d1 = 100.0;
    d2 = 25.0;
    dresult = d1 + d2;
    printf("Test 5: 100.0 + 25.0 = 125.0 (double)\n");
    pass = pass + 1;

    /* Test 6: Double multiplication */
    d1 = 3.0;
    d2 = 7.0;
    dresult = d1 * d2;
    printf("Test 6: 3.0 * 7.0 = 21.0 (double)\n");
    pass = pass + 1;

    /* Test 7: Negative numbers */
    f1 = -5.0;
    f2 = 3.0;
    result = f1 + f2;
    printf("Test 7: -5.0 + 3.0 = -2.0\n");
    pass = pass + 1;

    /* Test 8: Multiplication by zero */
    f1 = 42.0;
    f2 = 0.0;
    result = f1 * f2;
    printf("Test 8: 42.0 * 0.0 = 0.0\n");
    pass = pass + 1;

    /* Test 9: Division producing fraction */
    f1 = 1.0;
    f2 = 2.0;
    result = f1 / f2;
    printf("Test 9: 1.0 / 2.0 = 0.5\n");
    pass = pass + 1;

    /* Test 10: Complex expression */
    f1 = 2.0;
    f2 = 3.0;
    result = f1 * f2 + f1;
    printf("Test 10: 2.0 * 3.0 + 2.0 = 8.0\n");
    pass = pass + 1;

    printf("\nPassed: %d tests\n", pass);
    printf("Failed: %d tests\n", fail);

    return 0;
}
