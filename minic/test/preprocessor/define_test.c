/* Test #define macros */
#define MAX 100
#define MIN(a,b) ((a) < (b) ? (a) : (b))

main() {
    int x;
    x = MIN(MAX, 50);
    return x;
}
