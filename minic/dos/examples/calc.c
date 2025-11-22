# Simple Calculator - Integer arithmetic
# Demonstrates interactive input and numeric operations

main() {
    int num1;
    int num2;
    int result;
    int ch;
    int op;
    char input[16];
    int i;

    dos_puts("=== Simple Calculator ===\r\n");
    dos_puts("Enter first number (0-99): ");

    # Read first number
    i = 0;
    while (1) {
        ch = dos_getch();
        if (ch == 13) break;  # Enter
        if (ch >= 48 && ch <= 57) {  # '0'-'9'
            if (i < 15) {
                input[i] = ch;
                dos_putchar(ch);
                i = i + 1;
            }
        }
    }
    input[i] = 0;
    num1 = atoi(input);
    dos_puts("\r\n");

    # Get operation
    dos_puts("Operation (+,-,*,/): ");
    op = dos_getch();
    dos_putchar(op);
    dos_puts("\r\n");

    # Read second number
    dos_puts("Enter second number (0-99): ");
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
    dos_puts("\r\n");

    # Perform operation
    if (op == 43) {  # '+'
        result = num1 + num2;
    } else if (op == 45) {  # '-'
        result = num1 - num2;
    } else if (op == 42) {  # '*'
        result = num1 * num2;
    } else if (op == 47) {  # '/'
        if (num2 != 0) {
            result = num1 / num2;
        } else {
            dos_puts("Error: Division by zero!\r\n");
            return 1;
        }
    } else {
        dos_puts("Error: Unknown operation!\r\n");
        return 1;
    }

    # Display result
    dos_puts("Result: ");
    itoa(result, input, 10);
    dos_puts(input);
    dos_puts("\r\n");

    return 0;
}
