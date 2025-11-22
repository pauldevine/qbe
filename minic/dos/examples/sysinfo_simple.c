main() {
    int version;
    int major, minor;

    dos_puts("=== System Information ===\r\n\r\n");

    version = dos_get_version();
    major = version / 256;
    minor = version - (major * 256);

    dos_puts("DOS Version: ");
    dos_putchar(48 + major);  # '0' + digit
    dos_putchar(46);  # '.'
    if (minor >= 10) {
        dos_putchar(48 + (minor / 10));
        dos_putchar(48 + (minor - ((minor / 10) * 10)));
    } else {
        dos_putchar(48 + minor);
    }

    dos_puts("\r\n\r\nPress any key to exit...");
    dos_getch();
    dos_puts("\r\n");

    return 0;
}
