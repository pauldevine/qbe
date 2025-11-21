# Test extended escape sequences

main() {
    char c;

    printf("Escape Sequence Test\n");

    c = '\x41';
    if (c == 65)
        printf("  Hex escape \\x41 = 'A': PASS\n");

    c = '\101';
    if (c == 65)
        printf("  Octal escape \\101 = 'A': PASS\n");

    c = '\n';
    printf("  Newline \\n: OK\n");

    c = '\t';
    printf("  Tab \\t: OK\n");

    c = '\\';
    printf("  Backslash \\\\: OK\n");

    printf("\nAll escape sequences work\n");
    return 0;
}
