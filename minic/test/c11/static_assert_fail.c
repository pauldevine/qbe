# Test _Static_assert failure
# This test should FAIL at compile time

_Static_assert(0, "This assertion should fail!");

main() {
    return 0;
}
