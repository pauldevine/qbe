# Test _Alignas with numeric constants

main() {
    _Alignas(16) int buffer;
    _Alignas(4) char aligned_char;
    _Alignas(8) int x;

    buffer = 100;
    aligned_char = 42;
    x = 50;

    return buffer + aligned_char + x;
}
