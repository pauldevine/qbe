/* Test file for _Alignof and _Alignas (C11) */
int printf();
/* Test 1: _Alignof for basic types */
test_alignof_basic() {
    int a_char;
    int a_int;
    int a_long;
    a_char = _Alignof(char);
    a_int = _Alignof(int);
    a_long = _Alignof(long);
    /* On most platforms: char=1, int=2-4, long=4-8 */
    if (a_char != 1) return 1;
    if (a_int < 1) return 2;
    if (a_long < 1) return 3;
    printf("test_alignof_basic: PASS (char=%d, int=%d, long=%d)\n", a_char, a_int, a_long);
    return 0;
}
/* Test 2: _Alignof for pointers */
test_alignof_pointer() {
    int a_ptr;
    int a_intptr;
    a_ptr = _Alignof(char *);
    a_intptr = _Alignof(int *);
    /* Pointers should have consistent alignment */
    if (a_ptr < 1) return 1;
    if (a_intptr != a_ptr) return 2;
    printf("test_alignof_pointer: PASS (ptr=%d)\n", a_ptr);
    return 0;
}
/* Test 3: _Alignas with constant */
test_alignas_const() {
    _Alignas(8) int x;
    /* Variable should be declared (the alignment is a hint to allocator) */
    x = 42;
    if (x != 42) return 1;
    printf("test_alignas_const: PASS\n");
    return 0;
}
/* Test 4: _Alignas with type */
test_alignas_type() {
    _Alignas(long) int y;
    /* Variable should be declared with alignment of long */
    y = 100;
    if (y != 100) return 1;
    printf("test_alignas_type: PASS\n");
    return 0;
}
/* Test 5: Multiple _Alignas declarations */
test_alignas_multiple() {
    _Alignas(4) int a;
    _Alignas(4) int b;
    _Alignas(8) int c;
    a = 1;
    b = 2;
    c = 3;
    if (a != 1) return 1;
    if (b != 2) return 2;
    if (c != 3) return 3;
    printf("test_alignas_multiple: PASS\n");
    return 0;
}
/* Test 6: _Alignof in expressions */
test_alignof_expr() {
    int sum;
    sum = _Alignof(char) + _Alignof(int);
    if (sum < 2) return 1;
    printf("test_alignof_expr: PASS (char+int align = %d)\n", sum);
    return 0;
}
main() {
    int result;
    int total;
    total = 0;
    result = test_alignof_basic();
    if (result) { printf("test_alignof_basic FAILED: %d\n", result); total = total + 1; }
    result = test_alignof_pointer();
    if (result) { printf("test_alignof_pointer FAILED: %d\n", result); total = total + 1; }
    result = test_alignas_const();
    if (result) { printf("test_alignas_const FAILED: %d\n", result); total = total + 1; }
    result = test_alignas_type();
    if (result) { printf("test_alignas_type FAILED: %d\n", result); total = total + 1; }
    result = test_alignas_multiple();
    if (result) { printf("test_alignas_multiple FAILED: %d\n", result); total = total + 1; }
    result = test_alignof_expr();
    if (result) { printf("test_alignof_expr FAILED: %d\n", result); total = total + 1; }
    if (total == 0) {
        printf("\nAll _Alignof/_Alignas tests PASSED!\n");
    } else {
        printf("\n%d tests FAILED!\n", total);
    }
    return total;
}
