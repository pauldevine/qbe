/* Long (32-bit) Example for i8086
 * Demonstrates 32-bit arithmetic operations on 16-bit hardware
 */

/* Test 32-bit addition
 * 65536 + 65536 = 131072 (requires carry between words)
 */
test_add() {
    long a;
    long b;
    long c;

    a = 65536;   /* 0x10000 */
    b = 65536;   /* 0x10000 */
    c = a + b;   /* 131072 (0x20000) */

    /* Check low bits match (using bitwise AND to get low 16 bits) */
    if ((c & 65535) != 0) return 1;

    /* Check that it's greater than 65535 (proves high word is set) */
    if (c <= 65535) return 2;

    return 0;
}

/* Test 32-bit subtraction */
test_sub() {
    long a;
    long b;
    long c;

    a = 200000;
    b = 100000;
    c = a - b;   /* 100000 */

    if (c != 100000) return 10;

    return 0;
}

/* Test 32-bit multiplication
 * 1000 * 1000 = 1000000 (exceeds 16-bit)
 */
test_mul() {
    long a;
    long b;
    long c;

    a = 1000;
    b = 1000;
    c = a * b;   /* 1000000 */

    if (c != 1000000) return 20;

    return 0;
}

/* Test 32-bit comparison */
test_cmp() {
    long a;
    long b;

    a = 100000;
    b = 200000;

    if (a >= b) return 30;   /* Should not be true */
    if (b <= a) return 31;   /* Should not be true */
    if (a == b) return 32;   /* Should not be true */
    if (a > b) return 33;    /* Should not be true */

    if (a < b) {
        /* This should be true - do nothing */
    } else {
        return 34;
    }

    return 0;
}

/* Test 32-bit bitwise AND */
test_and() {
    long a;
    long result;

    a = 4294901760;  /* 0xFFFF0000 */
    result = a & 4278255360;  /* & 0xFF00FF00 = 0xFF000000 */

    if (result != 4278190080) return 40;  /* 0xFF000000 */

    return 0;
}

/* Test 32-bit bitwise OR */
test_or() {
    long a;
    long result;

    a = 4278190080;  /* 0xFF000000 */
    result = a | 255;  /* | 0xFF = 0xFF0000FF */

    if (result != 4278190335) return 50;  /* 0xFF0000FF */

    return 0;
}

/* Test left shift across word boundary */
test_shl() {
    long a;
    long result;

    a = 1;
    result = a << 20;  /* 0x100000 = 1048576 */

    if (result != 1048576) return 60;

    return 0;
}

/* Test right shift across word boundary */
test_shr() {
    long a;
    long result;

    a = 268435456;  /* 0x10000000 */
    result = a >> 8;  /* 0x100000 = 1048576 */

    if (result != 1048576) return 70;

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

    r = test_cmp();
    if (r != 0) return r;

    r = test_and();
    if (r != 0) return r;

    r = test_or();
    if (r != 0) return r;

    r = test_shl();
    if (r != 0) return r;

    r = test_shr();
    if (r != 0) return r;

    return 0;  /* All tests passed */
}
