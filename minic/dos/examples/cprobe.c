#include <stdio.h>
int main() {
    int x;
    int *p;
    x = 42;
    p = &x;
    printf("x=%d (want 42)\r\n", *p);
    *p = 99;
    printf("x=%d (want 99)\r\n", x);
    return 0;
}
