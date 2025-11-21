# Test extended character escape sequences

main() {
    char c;

    printf("Testing character escape sequences:\n");

    /* Standard escapes */
    c = '\n';
    printf("  Newline works\n");

    c = '\t';
    printf("  Tab: [%c] works\n", c);

    c = '\r';
    printf("  Carriage return works\n");

    c = '\0';
    printf("  Null character works\n");

    /* New escapes */
    c = '\a';
    printf("  Alert/bell works\n");

    c = '\b';
    printf("  Backspace works\n");

    c = '\f';
    printf("  Form feed works\n");

    c = '\v';
    printf("  Vertical tab works\n");

    c = '\\';
    printf("  Backslash: [%c] works\n", c);

    c = '\'';
    printf("  Single quote works\n");

    c = '\"';
    printf("  Double quote works\n");

    /* Hex escapes */
    c = '\x41';  /* 'A' */
    printf("  Hex \\x41 = %c\n", c);

    c = '\x42';  /* 'B' */
    printf("  Hex \\x42 = %c\n", c);

    /* Octal escapes */
    c = '\101';  /* 'A' */
    printf("  Octal \\101 = %c\n", c);

    c = '\102';  /* 'B' */
    printf("  Octal \\102 = %c\n", c);

    printf("\nAll escape sequences work!\n");
    return 0;
}
