/*
 * Test far pointer support in MiniC
 * Far pointers are 32-bit segment:offset pairs for i8086
 * Used to access memory outside the 64KB data segment
 */

/* Test far pointer declaration syntax variants */
int far *fptr1;         /* int far *ptr syntax */
int * far fptr2;        /* int * far ptr syntax */
char far *cfptr;        /* char far pointer */

/* Test far pointer as function parameter */
int read_far(int far *p) {
    return *p;
}

/* Test far pointer assignment and dereference */
void write_far(int far *p, int val) {
    *p = val;
}

/* Test far pointer arithmetic */
int far *advance_far(int far *p, int n) {
    return p + n;
}

/* Test local far pointer variable */
int test_local_far_ptr(void) {
    int far *local_far;
    int val;

    /* Simulate assigning a far pointer value */
    /* In real code, this would be: local_far = (int far *)0xB8000000L; */
    local_far = (int far *)0;

    /* Test that we can use the pointer (won't actually access memory in test) */
    if (local_far == (int far *)0)
        return 0;  /* Success - far pointer comparison works */

    return 1;  /* Fail */
}

/* Test sizeof far pointer - should be 4 bytes */
int test_sizeof_far(void) {
    /* Far pointers are 32-bit (4 bytes) */
    if (sizeof(int far *) == 4)
        return 0;  /* Success */
    return 1;  /* Fail */
}

/* Test far pointer with __far keyword (alternative syntax) */
int test_alt_syntax(void) {
    int __far *p;
    p = (int __far *)0;
    if (p == (int __far *)0)
        return 0;  /* Success */
    return 1;  /* Fail */
}

/* Main test function */
int test(void) {
    int result;

    /* Test 1: local far pointer */
    result = test_local_far_ptr();
    if (result != 0) return 1;

    /* Test 2: sizeof far pointer */
    result = test_sizeof_far();
    if (result != 0) return 2;

    /* Test 3: alternate syntax */
    result = test_alt_syntax();
    if (result != 0) return 3;

    return 0;  /* All tests passed */
}

int main(void) {
    return test();
}
