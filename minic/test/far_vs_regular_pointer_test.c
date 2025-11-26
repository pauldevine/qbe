int regular_int_ptr(void) {
    int x;
    int *p;
    x = 42;
    p = &x;
    *p = 100;
    return x;
}

int use_far_ptr(int far *fp) {
    *fp = 100;
    return 0;
}

int main(void) {
    regular_int_ptr();
    return 0;
}
