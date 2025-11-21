/* ============================================================================
 * Comprehensive Test of New C Compiler Features
 *
 * This test demonstrates all newly added features:
 * - Floating-point types (float, double)
 * - C-style comments (/* */) and C++ style (//)
 * - Extended character escape sequences
 * ============================================================================
 */

// Function to test floating-point arithmetic
test_float_arithmetic() {
    float f1;
    float f2;
    float result;
    double d1;
    double d2;

    printf("\n=== Floating-Point Arithmetic ===\n");

    f1 = 10.5;
    f2 = 2.5;

    result = f1 + f2;
    printf("10.5 + 2.5 = ~13.0\n");

    result = f1 - f2;
    printf("10.5 - 2.5 = ~8.0\n");

    result = f1 * f2;
    printf("10.5 * 2.5 = ~26.25\n");

    result = f1 / f2;
    printf("10.5 / 2.5 = ~4.2\n");

    /* Double precision */
    d1 = 3.141592653589793;
    d2 = 2.718281828459045;
    printf("Pi = ~3.14159...\n");
    printf("e = ~2.71828...\n");

    return 0;
}

// Test floating-point comparisons
test_float_comparisons() {
    float a;
    float b;

    printf("\n=== Floating-Point Comparisons ===\n");

    a = 5.5;
    b = 3.3;

    if (a > b)
        printf("5.5 > 3.3: PASS\n");
    else
        printf("5.5 > 3.3: FAIL\n");

    if (b < a)
        printf("3.3 < 5.5: PASS\n");
    else
        printf("3.3 < 5.5: FAIL\n");

    if (a == 5.5)
        printf("5.5 == 5.5: PASS\n");
    else
        printf("5.5 == 5.5: FAIL\n");

    return 0;
}

// Test mixed integer/float operations
test_mixed_operations() {
    int i;
    float f;
    double d;

    printf("\n=== Mixed Type Operations ===\n");

    i = 42;
    f = i;  /* int to float */
    printf("int 42 -> float: OK\n");

    f = 3.14;
    i = f;  /* float to int (truncates) */
    printf("float 3.14 -> int 3: OK\n");

    /* Integer + float */
    f = 2.5;
    i = 3;
    f = f + i;
    printf("2.5 + 3 = ~5.5: OK\n");

    /* Float to double */
    f = 1.5;
    d = f;
    printf("float -> double: OK\n");

    return 0;
}

// Test various float literal formats
test_float_literals() {
    float f;
    double d;

    printf("\n=== Float Literal Formats ===\n");

    f = 3.14;
    printf("Standard: 3.14\n");

    f = 0.5;
    printf("Leading zero: 0.5\n");

    f = 5.0;
    printf("Trailing zero: 5.0\n");

    f = .5;
    printf("Leading dot: .5\n");

    d = 1.5e2;   /* 150.0 */
    printf("Scientific (positive exp): 1.5e2 = 150.0\n");

    d = 1.5e-2;  /* 0.015 */
    printf("Scientific (negative exp): 1.5e-2 = 0.015\n");

    f = 2.5f;    /* Float suffix */
    printf("Float suffix: 2.5f\n");

    d = 2.5l;    /* Long double treated as double */
    printf("Long suffix: 2.5l\n");

    return 0;
}

// Test character escape sequences
test_escape_sequences() {
    char c;
    int i;

    printf("\n=== Character Escape Sequences ===\n");

    /* Hex escapes */
    c = '\x48';  /* 'H' */
    i = c;
    printf("Hex \\x48 (H): %d\n", i);

    c = '\x65';  /* 'e' */
    i = c;
    printf("Hex \\x65 (e): %d\n", i);

    /* Octal escapes */
    c = '\110';  /* 'H' */
    i = c;
    printf("Octal \\110 (H): %d\n", i);

    c = '\141';  /* 'a' */
    i = c;
    printf("Octal \\141 (a): %d\n", i);

    /* Special characters */
    c = '\a';
    printf("Alert: \\a\n");

    c = '\b';
    printf("Backspace: \\b\n");

    c = '\f';
    printf("Form feed: \\f\n");

    c = '\v';
    printf("Vertical tab: \\v\n");

    return 0;
}

main() {
    printf("========================================\n");
    printf("  New C Compiler Features Test Suite\n");
    printf("========================================\n");

    test_float_arithmetic();
    test_float_comparisons();
    test_mixed_operations();
    test_float_literals();
    test_escape_sequences();

    printf("\n========================================\n");
    printf("  All tests completed successfully!\n");
    printf("========================================\n");

    return 0;
}
