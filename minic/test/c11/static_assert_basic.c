# Test basic _Static_assert functionality
# This test should pass

_Static_assert(1, "This assertion should pass");
_Static_assert(2 + 2, "Arithmetic expressions should work");
_Static_assert(100, "Non-zero values are true");

main() {
    # Local _Static_assert
    _Static_assert(1, "Local assertion");

    # In function body
    _Static_assert(42, "In function body");

    return 0;
}
