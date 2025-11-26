/* Test file for _Generic (C11 type-generic selection) */
int printf();
/* Test 1: Select int type */
test_select_int() {
    int x;
    int result;
    x = 42;
    result = _Generic(x, int: 1, long: 2, default: 0);
    if (result != 1) return 1;
    printf("test_select_int: PASS\n");
    return 0;
}
/* Test 2: Select long type */
test_select_long() {
    long x;
    int result;
    x = 42;
    result = _Generic(x, int: 1, long: 2, default: 0);
    if (result != 2) return 1;
    printf("test_select_long: PASS\n");
    return 0;
}
/* Test 3: Use default when no match */
test_default() {
    float x;
    int result;
    x = 42.0;
    result = _Generic(x, int: 1, long: 2, default: 99);
    if (result != 99) return 1;
    printf("test_default: PASS\n");
    return 0;
}
/* Test 4: Select char type */
test_select_char() {
    char c;
    int result;
    c = 65;
    result = _Generic(c, char: 10, int: 20, default: 0);
    if (result != 10) return 1;
    printf("test_select_char: PASS\n");
    return 0;
}
/* Test 5: _Generic with pointer type */
test_select_pointer() {
    int *ptr;
    int val;
    int result;
    val = 100;
    ptr = &val;
    result = _Generic(ptr, int *: 50, char *: 60, default: 0);
    if (result != 50) return 1;
    printf("test_select_pointer: PASS\n");
    return 0;
}
/* Test 6: _Generic selecting expressions (not just constants) */
test_expr_selection() {
    int x;
    int y;
    int result;
    x = 10;
    y = 5;
    result = _Generic(x, int: x + y, long: x - y, default: 0);
    if (result != 15) return 1;
    printf("test_expr_selection: PASS\n");
    return 0;
}
/* Test 7: _Generic with float type */
test_select_float() {
    float f;
    int result;
    f = 3.14;
    result = _Generic(f, int: 1, float: 2, double: 3, default: 0);
    if (result != 2) return 1;
    printf("test_select_float: PASS\n");
    return 0;
}
/* Test 8: _Generic with double type */
test_select_double() {
    double d;
    int result;
    d = 3.14;
    result = _Generic(d, int: 1, float: 2, double: 3, default: 0);
    if (result != 3) return 1;
    printf("test_select_double: PASS\n");
    return 0;
}
main() {
    int result;
    int total;
    total = 0;
    result = test_select_int();
    if (result) { printf("test_select_int FAILED: %d\n", result); total = total + 1; }
    result = test_select_long();
    if (result) { printf("test_select_long FAILED: %d\n", result); total = total + 1; }
    result = test_default();
    if (result) { printf("test_default FAILED: %d\n", result); total = total + 1; }
    result = test_select_char();
    if (result) { printf("test_select_char FAILED: %d\n", result); total = total + 1; }
    result = test_select_pointer();
    if (result) { printf("test_select_pointer FAILED: %d\n", result); total = total + 1; }
    result = test_expr_selection();
    if (result) { printf("test_expr_selection FAILED: %d\n", result); total = total + 1; }
    result = test_select_float();
    if (result) { printf("test_select_float FAILED: %d\n", result); total = total + 1; }
    result = test_select_double();
    if (result) { printf("test_select_double FAILED: %d\n", result); total = total + 1; }
    if (total == 0) {
        printf("\nAll _Generic tests PASSED!\n");
    } else {
        printf("\n%d tests FAILED!\n", total);
    }
    return total;
}
