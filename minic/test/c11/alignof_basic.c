# Test _Alignof functionality

main() {
    int a_char;
    int a_int;
    int a_long;
    int a_ptr;

    a_char = _Alignof(char);
    a_int = _Alignof(int);
    a_long = _Alignof(long);
    a_ptr = _Alignof(char *);

    return a_char + a_int + a_long + a_ptr;
}
