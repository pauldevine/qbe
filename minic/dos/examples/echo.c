# Echo Program - Demonstrates character I/O
# Reads characters and echoes them back until ESC is pressed

main() {
    int ch;

    dos_puts("Echo Program - Press ESC to exit\r\n");
    dos_puts("Type something: ");

    while (1) {
        ch = dos_getch();  # Read without echo

        if (ch == 27) {    # ESC key
            dos_puts("\r\nGoodbye!\r\n");
            break;
        }

        # Echo the character
        dos_putchar(ch);

        # Handle Enter key
        if (ch == 13) {
            dos_putchar(10);  # Add linefeed
        }
    }

    return 0;
}
