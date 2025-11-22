# Test _Alignas with type specifiers

main() {
    _Alignas(int) char a;
    _Alignas(long) int b;
    _Alignas(char) int c;

    a = 10;
    b = 20;
    c = 30;

    return a + b + c;
}
