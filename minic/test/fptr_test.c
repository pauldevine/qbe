# Test function pointers

add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

main() {
    int (*fptr)(int, int);
    int result;

    fptr = add;
    result = (*fptr)(10, 20);

    return result;
}
