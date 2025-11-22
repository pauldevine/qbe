# File Copy Utility - Demonstrates file I/O
# Usage: filecopy source.txt dest.txt

char buffer[512];
char msg_usage[64];
char msg_copying[64];
char msg_done[64];
char msg_error[64];

main() {
    int src, dst;
    int bytes;
    int total;

    # Initialize messages (since we don't have string literals in data section)
    strcpy(msg_usage, "Usage: filecopy source dest\r\n");
    strcpy(msg_copying, "Copying file...\r\n");
    strcpy(msg_done, "Done! Bytes copied: ");
    strcpy(msg_error, "Error: Cannot open file\r\n");

    # For now, use hardcoded filenames
    # (Command-line args would require more complex startup code)

    src = dos_open("INPUT.TXT", 0);  # Read mode
    if (src == -1) {
        dos_puts(msg_error);
        return 1;
    }

    dst = dos_create("OUTPUT.TXT");
    if (dst == -1) {
        dos_puts(msg_error);
        dos_close(src);
        return 1;
    }

    dos_puts(msg_copying);
    total = 0;

    while (1) {
        bytes = dos_read(src, buffer, 512);
        if (bytes <= 0) break;

        dos_write(dst, buffer, bytes);
        total = total + bytes;
    }

    dos_close(src);
    dos_close(dst);

    dos_puts(msg_done);
    # Would print total here with printf if available

    return 0;
}
