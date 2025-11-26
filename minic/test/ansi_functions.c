/* Test ANSI C style function definitions - basic version */

int global_counter;

/* Test 1: int return type with void parameters */
int get_ten(void) {
    return 10;
}

/* Test 2: int return type with typed parameters */
int add(int a, int b) {
    return a + b;
}

/* Test 3: long return type */
long get_large(void) {
    return 1000000;
}

/* Test 4: void function with no return */
void increment_counter(void) {
    global_counter = global_counter + 1;
}

/* Test 5: void function with explicit return */
void set_counter(int val) {
    global_counter = val;
    return;
}

/* Test 6: multiple typed parameters */
int sum_four(int a, int b, int c, int d) {
    return a + b + c + d;
}

/* Test 7: K&R style still works (backwards compatibility) */
knr_style() {
    return 42;
}

int main(void) {
    int result;
    long lval;

    /* Test get_ten */
    result = get_ten();
    if (result != 10) {
        printf("FAIL: get_ten returned %d, expected 10\n", result);
        return 1;
    }
    printf("PASS: get_ten() = %d\n", result);

    /* Test add */
    result = add(3, 7);
    if (result != 10) {
        printf("FAIL: add(3, 7) returned %d, expected 10\n", result);
        return 1;
    }
    printf("PASS: add(3, 7) = %d\n", result);

    /* Test get_large */
    lval = get_large();
    if (lval != 1000000) {
        printf("FAIL: get_large returned %ld, expected 1000000\n", lval);
        return 1;
    }
    printf("PASS: get_large() = %ld\n", lval);

    /* Test void functions */
    global_counter = 0;
    increment_counter();
    increment_counter();
    increment_counter();
    if (global_counter != 3) {
        printf("FAIL: global_counter is %d, expected 3\n", global_counter);
        return 1;
    }
    printf("PASS: increment_counter() worked, counter = %d\n", global_counter);

    set_counter(100);
    if (global_counter != 100) {
        printf("FAIL: set_counter(100) failed, counter = %d\n", global_counter);
        return 1;
    }
    printf("PASS: set_counter(100) worked\n");

    /* Test sum_four */
    result = sum_four(1, 2, 3, 4);
    if (result != 10) {
        printf("FAIL: sum_four(1,2,3,4) returned %d, expected 10\n", result);
        return 1;
    }
    printf("PASS: sum_four(1, 2, 3, 4) = %d\n", result);

    /* Test K&R style */
    result = knr_style();
    if (result != 42) {
        printf("FAIL: knr_style() returned %d, expected 42\n", result);
        return 1;
    }
    printf("PASS: knr_style() = %d (K&R backwards compatibility)\n", result);

    printf("\nAll tests passed!\n");
    return 0;
}
