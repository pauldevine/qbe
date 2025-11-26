# Test function pointers with runtime choice

add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

compute(int (*op)(int, int), int x, int y) {
    return (*op)(x, y);
}

main() {
    int result1;
    int result2;

    result1 = compute(add, 10, 3);
    result2 = compute(sub, 10, 3);

    return result1 + result2;
}
