/* Test compound literals - comprehensive test
 *
 * Note: Struct compound literals work for structs up to 8 bytes.
 * Larger structs have a limitation in MiniC's struct assignment.
 */

struct Point {
    int x;
    int y;
};

union Number {
    int i;
    long l;
};

/* Test 1: Scalar compound literal */
test_scalar() {
    int x;
    x = (int){42};
    return x;
}

/* Test 2: Struct compound literal assignment */
test_struct_assign() {
    struct Point p;
    p = (struct Point){10, 20};
    return p.x + p.y;
}

/* Test 3: Taking address of compound literal */
test_address() {
    struct Point *ptr;
    ptr = &(struct Point){5, 15};
    return ptr->x + ptr->y;
}

/* Test 4: Compound literal in expression */
test_expression() {
    int result;
    result = (int){100} + (int){23};
    return result;
}

/* Test 5: Multiple compound literals */
test_multiple() {
    struct Point p1;
    struct Point p2;
    p1 = (struct Point){1, 2};
    p2 = (struct Point){3, 4};
    return p1.x + p1.y + p2.x + p2.y;
}

/* Test 6: Union compound literal */
test_union() {
    union Number n;
    n = (union Number){42};
    return n.i;
}

/* Test 7: Nested struct access via pointer */
test_nested_access() {
    struct Point *p;
    int sum;
    p = &(struct Point){7, 8};
    sum = p->x;
    sum = sum + p->y;
    return sum;
}

/* Test 8: Compound literal with char type */
test_char() {
    char c;
    c = (char){65};
    return c;
}

main() {
    int result;

    /* Test 1: Scalar compound literal */
    result = test_scalar();
    if (result != 42)
        return 1;

    /* Test 2: Struct compound literal assignment */
    result = test_struct_assign();
    if (result != 30)
        return 2;

    /* Test 3: Address of compound literal */
    result = test_address();
    if (result != 20)
        return 3;

    /* Test 4: Compound literal in expression */
    result = test_expression();
    if (result != 123)
        return 4;

    /* Test 5: Multiple compound literals */
    result = test_multiple();
    if (result != 10)
        return 5;

    /* Test 6: Union compound literal */
    result = test_union();
    if (result != 42)
        return 6;

    /* Test 7: Nested struct access */
    result = test_nested_access();
    if (result != 15)
        return 7;

    /* Test 8: Char compound literal */
    result = test_char();
    if (result != 65)
        return 8;

    return 0;  /* All tests passed */
}
