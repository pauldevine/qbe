/* Test that regular pointers don't get FAR operations.
 * Before the fix, int* had value 20 (binary 00010100) which has bit 4 set,
 * causing ISFAR() to incorrectly return true for regular int pointers.
 */

int test_int_ptr(void) {
    int x;
    int *p;

    x = 42;
    p = &x;
    *p = 100;

    return x;
}

long test_long_ptr(void) {
    long x;
    long *p;

    x = 123456789;
    p = &x;
    *p = 987654321;

    return x;
}

int main(void) {
    int result1;
    long result2;

    result1 = test_int_ptr();
    result2 = test_long_ptr();

    printf("int pointer test: %d (expected 100)\n", result1);
    printf("long pointer test: %ld (expected 987654321)\n", result2);

    if (result1 == 100 && result2 == 987654321) {
        printf("PASS: Regular pointers work correctly\n");
        return 0;
    }
    printf("FAIL: Regular pointers are broken\n");
    return 1;
}
