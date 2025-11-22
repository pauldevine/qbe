# System Information Utility
# Displays DOS version and other system information

main() {
    int version;
    int major;
    int minor;
    char ver_str[16];

    dos_puts("=== System Information ===\r\n\r\n");

    # Get DOS version
    version = dos_get_version();
    major = (version >> 8) & 255;  # High byte
    minor = version & 255;          # Low byte

    dos_puts("DOS Version: ");
    itoa(major, ver_str, 10);
    dos_puts(ver_str);
    dos_putchar(46);  # '.'
    itoa(minor, ver_str, 10);
    dos_puts(ver_str);
    dos_puts("\r\n");

    dos_puts("\r\nPress any key to exit...");
    dos_getch();
    dos_puts("\r\n");

    return 0;
}
