# Test function pointer typedef

typedef int (*binop_t)(int, int);

add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

apply(binop_t op, int x, int y) {
    return op(x, y);
}

main() {
    binop_t myop;
    int result;

    myop = add;
    result = apply(myop, 10, 3);

    myop = sub;
    result = result + apply(myop, 10, 3);

    return result;
}
