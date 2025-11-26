/*
 * Basic far pointer test for MiniC
 * Tests far pointer declaration syntax
 */

/* Global far pointer */
int far *gfptr;

/* Test far pointer function parameter */
int far_func(int far *p) {
    if (p == (int far *)0)
        return 1;
    return 0;
}

int test(void) {
    int far *local;

    /* Initialize to null */
    local = (int far *)0;

    /* Test pointer assignment */
    gfptr = local;

    /* Test comparison */
    if (gfptr == local)
        return 0;

    return 1;
}

int main(void) {
    return test();
}
