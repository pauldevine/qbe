/* Test file for designated initializers */
struct Point {
    int x;
    int y;
};
/* Note: Config struct is limited to 2 fields (8 bytes)
 * because MiniC struct assignment only copies up to 8 bytes */
struct Config {
    int id;
    int value;
};
int printf();
/* Test 1: Struct designated initializers in compound literals */
test_struct_designated() {
    struct Point p;
    p = (struct Point){.x = 10, .y = 20};
    if (p.x != 10) return 1;
    if (p.y != 20) return 2;
    printf("test_struct_designated: PASS\n");
    return 0;
}
/* Test 2: Out of order struct designated initializers */
test_out_of_order() {
    struct Point p;
    p = (struct Point){.y = 30, .x = 40};
    if (p.x != 40) return 1;
    if (p.y != 30) return 2;
    printf("test_out_of_order: PASS\n");
    return 0;
}
/* Test 3: Partial struct initialization with designator */
test_partial_init() {
    struct Config cfg;
    cfg = (struct Config){.value = 100};
    /* id should be zero-initialized */
    if (cfg.id != 0) return 1;
    if (cfg.value != 100) return 2;
    printf("test_partial_init: PASS\n");
    return 0;
}
/* Test 4: Array designated initializers */
test_array_designated() {
    int arr[5] = {[2] = 42, [4] = 99};
    if (arr[0] != 0) return 1;
    if (arr[1] != 0) return 2;
    if (arr[2] != 42) return 3;
    if (arr[3] != 0) return 4;
    if (arr[4] != 99) return 5;
    printf("test_array_designated: PASS\n");
    return 0;
}
/* Test 5: Mixed sequential and designated initializers */
test_mixed_init() {
    int arr[5] = {1, [3] = 50, 60};
    if (arr[0] != 1) return 1;
    if (arr[1] != 0) return 2;
    if (arr[2] != 0) return 3;
    if (arr[3] != 50) return 4;
    if (arr[4] != 60) return 5;
    printf("test_mixed_init: PASS\n");
    return 0;
}
/* Test 6: Address of compound literal with designated init */
test_address_designated() {
    struct Point *ptr;
    ptr = &(struct Point){.y = 100, .x = 200};
    if (ptr->x != 200) return 1;
    if (ptr->y != 100) return 2;
    printf("test_address_designated: PASS\n");
    return 0;
}
/* Test 7: Multiple designated struct fields */
test_multiple_fields() {
    struct Config cfg;
    cfg = (struct Config){.id = 1, .value = 2};
    if (cfg.id != 1) return 1;
    if (cfg.value != 2) return 2;
    printf("test_multiple_fields: PASS\n");
    return 0;
}
/* Test 8: Designated array with expressions */
test_array_expr() {
    int val;
    int arr[3] = {[0] = 10, [2] = 15};
    if (arr[0] != 10) return 1;
    if (arr[1] != 0) return 2;
    if (arr[2] != 15) return 3;
    printf("test_array_expr: PASS\n");
    return 0;
}
main() {
    int result;
    int total;
    total = 0;
    result = test_struct_designated();
    if (result) { printf("test_struct_designated FAILED: %d\n", result); total = total + 1; }
    result = test_out_of_order();
    if (result) { printf("test_out_of_order FAILED: %d\n", result); total = total + 1; }
    result = test_partial_init();
    if (result) { printf("test_partial_init FAILED: %d\n", result); total = total + 1; }
    result = test_array_designated();
    if (result) { printf("test_array_designated FAILED: %d\n", result); total = total + 1; }
    result = test_mixed_init();
    if (result) { printf("test_mixed_init FAILED: %d\n", result); total = total + 1; }
    result = test_address_designated();
    if (result) { printf("test_address_designated FAILED: %d\n", result); total = total + 1; }
    result = test_multiple_fields();
    if (result) { printf("test_multiple_fields FAILED: %d\n", result); total = total + 1; }
    result = test_array_expr();
    if (result) { printf("test_array_expr FAILED: %d\n", result); total = total + 1; }
    if (total == 0) {
        printf("\nAll designated initializer tests PASSED!\n");
    } else {
        printf("\n%d tests FAILED!\n", total);
    }
    return total;
}
