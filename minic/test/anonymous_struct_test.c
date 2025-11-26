/* Test file for anonymous struct/union feature (C11)
 * Note: Each struct definition can have only one anonymous struct/union
 * due to a naming limitation in the current implementation.
 */
int printf();
/* Test 1: Anonymous union in struct */
struct Variant {
    int type;
    union {
        int i;
        long l;
    };
};
test_anon_union() {
    struct Variant v;
    v.type = 1;
    v.i = 42;
    if (v.type != 1) return 1;
    if (v.i != 42) return 2;
    /* Access long member */
    v.l = 1000000;
    if (v.l != 1000000) return 3;
    printf("test_anon_union: PASS\n");
    return 0;
}
/* Test 2: Anonymous struct in struct */
struct Record {
    int id;
    struct {
        int x;
        int y;
    };
};
test_anon_struct() {
    struct Record r;
    r.id = 100;
    r.x = 10;
    r.y = 20;
    if (r.id != 100) return 1;
    if (r.x != 10) return 2;
    if (r.y != 20) return 3;
    printf("test_anon_struct: PASS\n");
    return 0;
}
/* Test 3: Access through pointer */
test_pointer_access() {
    struct Variant v;
    struct Variant *pv;
    v.type = 2;
    v.l = 999999;
    pv = &v;
    if (pv->type != 2) return 1;
    if (pv->l != 999999) return 2;
    pv->i = 77;
    if (pv->i != 77) return 3;
    printf("test_pointer_access: PASS\n");
    return 0;
}
/* Test 4: Modification via anonymous member */
test_modification() {
    struct Record r;
    r.id = 50;
    r.x = 5;
    r.y = 10;
    r.x = r.x + r.y;
    if (r.x != 15) return 1;
    r.id = r.id + r.x;
    if (r.id != 65) return 2;
    printf("test_modification: PASS\n");
    return 0;
}
main() {
    int result;
    int total;
    total = 0;
    result = test_anon_union();
    if (result) { printf("test_anon_union FAILED: %d\n", result); total = total + 1; }
    result = test_anon_struct();
    if (result) { printf("test_anon_struct FAILED: %d\n", result); total = total + 1; }
    result = test_pointer_access();
    if (result) { printf("test_pointer_access FAILED: %d\n", result); total = total + 1; }
    result = test_modification();
    if (result) { printf("test_modification FAILED: %d\n", result); total = total + 1; }
    if (total == 0) {
        printf("\nAll anonymous struct/union tests PASSED!\n");
    } else {
        printf("\n%d tests FAILED!\n", total);
    }
    return total;
}
