# Float comparison test

main() {
    float a;
    float b;
    int pass;
    int fail;

    pass = 0;
    fail = 0;

    printf("=== Float Comparison Tests ===\n");

    a = 10.0;
    b = 5.0;

    printf("a = 10.0, b = 5.0\n\n");

    if (a > b) {
        printf("  a > b: PASS\n");
        pass = pass + 1;
    } else {
        printf("  a > b: FAIL\n");
        fail = fail + 1;
    }

    if (b < a) {
        printf("  b < a: PASS\n");
        pass = pass + 1;
    } else {
        printf("  b < a: FAIL\n");
        fail = fail + 1;
    }

    if (a >= b) {
        printf("  a >= b: PASS\n");
        pass = pass + 1;
    } else {
        printf("  a >= b: FAIL\n");
        fail = fail + 1;
    }

    if (b <= a) {
        printf("  b <= a: PASS\n");
        pass = pass + 1;
    } else {
        printf("  b <= a: FAIL\n");
        fail = fail + 1;
    }

    a = 5.0;
    if (a == b) {
        printf("  a == b (both 5.0): PASS\n");
        pass = pass + 1;
    } else {
        printf("  a == b (both 5.0): FAIL\n");
        fail = fail + 1;
    }

    a = 10.0;
    if (a != b) {
        printf("  a != b: PASS\n");
        pass = pass + 1;
    } else {
        printf("  a != b: FAIL\n");
        fail = fail + 1;
    }

    printf("\nPassed: %d / 6 tests\n", pass);
    printf("Failed: %d tests\n", fail);

    return 0;
}
