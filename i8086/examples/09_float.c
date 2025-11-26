/* Floating Point Example for i8086 with 8087 FPU
 * Demonstrates float arithmetic using the 8087 coprocessor
 * Note: For simplicity, we use direct assignment for type conversion
 */

/* Test float addition */
test_add() {
    float a;
    float b;
    float c;
    int result;

    a = 3.0;
    b = 2.0;
    c = a + b;  /* 5.0 */

    result = c;  /* implicit float to int */
    if (result != 5) return 1;

    return 0;
}

/* Test float subtraction */
test_sub() {
    float a;
    float b;
    float c;
    int result;

    a = 7.0;
    b = 4.0;
    c = a - b;  /* 3.0 */

    result = c;
    if (result != 3) return 2;

    return 0;
}

/* Test float multiplication */
test_mul() {
    float a;
    float b;
    float c;
    int result;

    a = 3.0;
    b = 4.0;
    c = a * b;  /* 12.0 */

    result = c;
    if (result != 12) return 3;

    return 0;
}

/* Test float division */
test_div() {
    float a;
    float b;
    float c;
    int result;

    a = 20.0;
    b = 4.0;
    c = a / b;  /* 5.0 */

    result = c;
    if (result != 5) return 4;

    return 0;
}

/* Test int to float and back conversion */
test_conv() {
    int i;
    float f;
    int back;

    i = 42;
    f = i;      /* int to float */
    back = f;   /* float to int */

    if (back != 42) return 5;

    return 0;
}

/* Test float comparison */
test_cmp() {
    float a;
    float b;

    a = 2.0;
    b = 5.0;

    if (a >= b) return 10;  /* 2.0 >= 5.0 should be false */
    if (b <= a) return 11;  /* 5.0 <= 2.0 should be false */
    if (a > b) return 12;   /* 2.0 > 5.0 should be false */

    if (a < b) {
        /* 2.0 < 5.0 should be true - pass */
    } else {
        return 13;
    }

    return 0;
}

main() {
    int r;

    r = test_add();
    if (r != 0) return r;

    r = test_sub();
    if (r != 0) return r;

    r = test_mul();
    if (r != 0) return r;

    r = test_div();
    if (r != 0) return r;

    r = test_conv();
    if (r != 0) return r;

    r = test_cmp();
    if (r != 0) return r;

    return 0;  /* All tests passed */
}
