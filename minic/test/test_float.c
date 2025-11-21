# Test floating-point support

test_float_basics() {
    float f1;
    float f2;
    float f3;
    double d1;
    double d2;
    double d3;

    f1 = 3.14;
    f2 = 2.0;
    d1 = 3.141592653;
    d2 = 2.718281828;

    printf("Float basics:\n");
    printf("  f1 = 3.14\n");
    printf("  f2 = 2.0\n");

    # Arithmetic
    f3 = f1 + f2;
    printf("  f1 + f2 (expect ~5.14)\n");

    f3 = f1 - f2;
    printf("  f1 - f2 (expect ~1.14)\n");

    f3 = f1 * f2;
    printf("  f1 * f2 (expect ~6.28)\n");

    f3 = f1 / f2;
    printf("  f1 / f2 (expect ~1.57)\n");

    # Double arithmetic
    d3 = d1 + d2;
    printf("  d1 + d2 (expect ~5.86)\n");

    d3 = d1 * d2;
    printf("  d1 * d2 (expect ~8.54)\n");

    return 0;
}

test_float_comparisons() {
    float f1;
    float f2;

    f1 = 3.14;
    f2 = 2.71;

    printf("\nFloat comparisons:\n");

    if (f1 > f2)
        printf("  3.14 > 2.71: PASS\n");
    else
        printf("  3.14 > 2.71: FAIL\n");

    if (f2 < f1)
        printf("  2.71 < 3.14: PASS\n");
    else
        printf("  2.71 < 3.14: FAIL\n");

    return 0;
}

test_mixed_types() {
    int i;
    float f;

    i = 42;
    f = 3.14;

    printf("\nMixed type operations:\n");
    printf("  int i = 42\n");

    # Int to float
    f = i;
    printf("  f = i (expect 42.0)\n");

    # Float to int (truncates)
    f = 3.14;
    i = f;
    printf("  i = f (expect 3)\n");

    # Int + float
    f = 3.0;
    i = 2;
    f = f + i;
    printf("  f + i (expect 5.0)\n");

    return 0;
}

test_float_literals() {
    float f;
    double d;

    printf("\nFloat literal formats:\n");

    # Standard decimal
    f = 3.14;
    printf("  3.14 works\n");

    # Leading zero
    f = 0.5;
    printf("  0.5 works\n");

    # Trailing zero
    d = 5.0;
    printf("  5.0 works\n");

    # Scientific notation
    d = 1.5e2;
    printf("  1.5e2 works (150.0)\n");

    d = 1.5e-2;
    printf("  1.5e-2 works (0.015)\n");

    # Leading dot
    f = .5;
    printf("  .5 works\n");

    return 0;
}

main() {
    printf("===== Floating-Point Test Suite =====\n");
    test_float_basics();
    test_float_comparisons();
    test_mixed_types();
    test_float_literals();
    printf("\n===== All tests completed =====\n");
    return 0;
}
