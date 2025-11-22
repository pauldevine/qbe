# Interactive Menu System
# Demonstrates menu-driven program structure

show_menu() {
    dos_puts("\r\n");
    dos_puts("=== Main Menu ===\r\n");
    dos_puts("1. Display system information\r\n");
    dos_puts("2. Show memory usage\r\n");
    dos_puts("3. Run calculator\r\n");
    dos_puts("4. Display date/time\r\n");
    dos_puts("5. File operations\r\n");
    dos_puts("Q. Quit\r\n");
    dos_puts("\r\nSelect option: ");
}

show_sysinfo() {
    int version;
    int major;
    int minor;
    char ver_str[16];

    dos_puts("\r\n--- System Information ---\r\n");

    version = dos_get_version();
    major = (version >> 8) & 255;
    minor = version & 255;

    dos_puts("DOS Version: ");
    itoa(major, ver_str, 10);
    dos_puts(ver_str);
    dos_putchar(46);  # '.'
    itoa(minor, ver_str, 10);
    dos_puts(ver_str);
    dos_puts("\r\n");

    dos_puts("Program: MENU.COM\r\n");
    dos_puts("Compiled with QBE i8086 backend\r\n");
}

show_memory() {
    dos_puts("\r\n--- Memory Usage ---\r\n");
    dos_puts("Stack size: ~4096 bytes\r\n");
    dos_puts("Data segment: ~64KB max\r\n");
    dos_puts("Code segment: .COM format\r\n");
    dos_puts("Free memory: (DOS dependent)\r\n");
}

run_calculator() {
    int num1;
    int num2;
    int result;
    char input[16];
    int ch;
    int i;

    dos_puts("\r\n--- Quick Calculator ---\r\n");
    dos_puts("Enter first number: ");

    i = 0;
    while (1) {
        ch = dos_getch();
        if (ch == 13) break;
        if (ch >= 48 && ch <= 57) {
            if (i < 15) {
                input[i] = ch;
                dos_putchar(ch);
                i = i + 1;
            }
        }
    }
    input[i] = 0;
    num1 = atoi(input);

    dos_puts("\r\nEnter second number: ");

    i = 0;
    while (1) {
        ch = dos_getch();
        if (ch == 13) break;
        if (ch >= 48 && ch <= 57) {
            if (i < 15) {
                input[i] = ch;
                dos_putchar(ch);
                i = i + 1;
            }
        }
    }
    input[i] = 0;
    num2 = atoi(input);

    result = num1 + num2;

    dos_puts("\r\nSum: ");
    itoa(result, input, 10);
    dos_puts(input);
    dos_puts("\r\n");
}

show_datetime() {
    dos_puts("\r\n--- Date/Time ---\r\n");
    dos_puts("(Date/time functions require additional DOS calls)\r\n");
    dos_puts("INT 21h AH=2Ah - Get date\r\n");
    dos_puts("INT 21h AH=2Ch - Get time\r\n");
    dos_puts("Not implemented in this demo.\r\n");
}

file_menu() {
    dos_puts("\r\n--- File Operations ---\r\n");
    dos_puts("Available operations:\r\n");
    dos_puts("- Open/Close files\r\n");
    dos_puts("- Read/Write data\r\n");
    dos_puts("- Create/Delete files\r\n");
    dos_puts("\r\nSee FILECOPY.COM and TEXTVIEW.COM for examples.\r\n");
}

main() {
    int running;
    int choice;

    dos_puts("=== DOS Menu System ===\r\n");
    dos_puts("Interactive menu demonstration\r\n");

    running = 1;

    while (running) {
        show_menu();

        choice = dos_getch();
        dos_putchar(choice);
        dos_puts("\r\n");

        if (choice == 49) {  # '1'
            show_sysinfo();
        } else if (choice == 50) {  # '2'
            show_memory();
        } else if (choice == 51) {  # '3'
            run_calculator();
        } else if (choice == 52) {  # '4'
            show_datetime();
        } else if (choice == 53) {  # '5'
            file_menu();
        } else if (choice == 81 || choice == 113) {  # 'Q' or 'q'
            running = 0;
            dos_puts("\r\nGoodbye!\r\n");
        } else {
            dos_puts("\r\nInvalid option. Try again.\r\n");
        }

        if (running) {
            dos_puts("\r\nPress any key to continue...");
            dos_getch();
        }
    }

    return 0;
}
