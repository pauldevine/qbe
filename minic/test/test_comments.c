# Test C-style comments

/* This is a C-style block comment */

main() {
    int x;
    /* Another comment */
    x = 10; /* inline comment */

    // C++-style comment
    x = x + 1;  // another C++ comment

    /*
     * Multi-line
     * comment
     * block
     */

    x = x * 2;  # Shell-style still works too

    printf("All comment styles work! x = %d\n", x);
    return 0;
}
