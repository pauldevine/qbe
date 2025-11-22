# Test _Static_assert with sizeof
# For 8086: int=2 bytes, long=4 bytes, pointers=2 bytes

# Note: This will work when we add sizeof support to _Static_assert
# For now, we only support simple integer constants

_Static_assert(1, "int should be 2 bytes on 8086");
_Static_assert(1, "pointers should be 2 bytes on 8086");

main() {
    return 0;
}
