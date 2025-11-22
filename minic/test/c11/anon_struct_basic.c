# Test anonymous struct (basic case)
# Based on FreeDOS kernel patterns

struct Point {
    int x;
    struct {
        int y;
        int z;
    };  # Anonymous struct - y and z accessible directly
    int w;
};

main() {
    struct Point p;

    p.x = 10;
    p.y = 20;  # Direct access to anonymous struct member
    p.z = 30;  # Direct access to anonymous struct member
    p.w = 40;

    return p.x + p.y + p.z + p.w;  # Should return 100
}
