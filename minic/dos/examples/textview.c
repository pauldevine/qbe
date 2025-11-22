# Simple Text File Viewer
# Displays contents of a text file

char buffer[512];
char filename[64];

main() {
    int fd;
    int bytes;
    int i;

    dos_puts("Simple Text Viewer\r\n");
    dos_puts("==================\r\n\r\n");

    # For demo, use hardcoded filename
    strcpy(filename, "README.TXT");

    fd = dos_open(filename, 0);  # Read mode
    if (fd == -1) {
        dos_puts("Error: Cannot open ");
        dos_puts(filename);
        dos_puts("\r\n");
        return 1;
    }

    # Read and display file contents
    while (1) {
        bytes = dos_read(fd, buffer, 512);
        if (bytes <= 0) break;

        # Display buffer contents
        i = 0;
        while (i < bytes) {
            dos_putchar(buffer[i]);
            i = i + 1;
        }
    }

    dos_close(fd);

    dos_puts("\r\n--- End of file ---\r\n");

    return 0;
}
