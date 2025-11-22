# Hex Dump Utility
# Displays file contents in hexadecimal format

char buffer[16];
char filename[64];
char hex_chars[17];

int to_hex_char(int n) {
    if (n < 10) {
        return 48 + n;  # '0' + n
    }
    return 65 + (n - 10);  # 'A' + (n - 10)
}

print_hex_byte(int byte) {
    int high;
    int low;

    high = (byte >> 4) & 15;
    low = byte & 15;

    dos_putchar(to_hex_char(high));
    dos_putchar(to_hex_char(low));
}

print_ascii(int byte) {
    if (byte >= 32 && byte <= 126) {
        dos_putchar(byte);
    } else {
        dos_putchar(46);  # '.'
    }
}

main() {
    int fd;
    int bytes;
    int i;
    int offset;
    char offset_str[16];

    dos_puts("=== Hex Dump Utility ===\r\n\r\n");

    # For demo, use hardcoded filename
    strcpy(filename, "TEST.BIN");

    dos_puts("File: ");
    dos_puts(filename);
    dos_puts("\r\n\r\n");

    fd = dos_open(filename, 0);  # Read mode
    if (fd == -1) {
        dos_puts("Error: Cannot open file\r\n");
        dos_puts("Creating sample data...\r\n\r\n");

        # Create sample file for demo
        fd = dos_create(filename);
        if (fd == -1) {
            dos_puts("Error: Cannot create file\r\n");
            return 1;
        }

        # Write sample data
        i = 0;
        while (i < 256) {
            buffer[0] = i;
            dos_write(fd, buffer, 1);
            i = i + 1;
        }
        dos_close(fd);

        # Reopen for reading
        fd = dos_open(filename, 0);
        if (fd == -1) {
            dos_puts("Error: Cannot reopen file\r\n");
            return 1;
        }
    }

    offset = 0;

    # Read and display in hex
    while (1) {
        bytes = dos_read(fd, buffer, 16);
        if (bytes <= 0) break;

        # Print offset
        itoa(offset, offset_str, 16);
        i = strlen(offset_str);
        while (i < 4) {
            dos_putchar(48);  # '0'
            i = i + 1;
        }
        dos_puts(offset_str);
        dos_puts(": ");

        # Print hex bytes
        i = 0;
        while (i < 16) {
            if (i < bytes) {
                print_hex_byte(buffer[i]);
            } else {
                dos_puts("  ");
            }
            dos_putchar(32);  # Space

            if (i == 7) {
                dos_putchar(32);  # Extra space in middle
            }

            i = i + 1;
        }

        dos_puts(" |");

        # Print ASCII representation
        i = 0;
        while (i < bytes) {
            print_ascii(buffer[i]);
            i = i + 1;
        }

        dos_puts("|\r\n");

        offset = offset + bytes;
    }

    dos_close(fd);

    dos_puts("\r\nTotal bytes: ");
    itoa(offset, offset_str, 10);
    dos_puts(offset_str);
    dos_puts("\r\n");

    return 0;
}
