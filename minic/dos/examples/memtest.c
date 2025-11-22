# Memory Test Utility
# Demonstrates memory operations and array manipulation

char test_buffer[512];
char backup_buffer[512];
int pattern_data[64];

fill_pattern(char *buf, int size, int pattern) {
    int i;

    i = 0;
    while (i < size) {
        buf[i] = pattern + i;
        i = i + 1;
    }

    return size;
}

verify_pattern(char *buf, int size, int pattern) {
    int i;
    int errors;

    errors = 0;
    i = 0;
    while (i < size) {
        if (buf[i] != (pattern + i)) {
            errors = errors + 1;
        }
        i = i + 1;
    }

    return errors;
}

memory_copy_test() {
    int i;
    int errors;
    char result[16];

    dos_puts("Test 1: Memory Copy\r\n");

    # Fill source buffer
    fill_pattern(test_buffer, 256, 0);

    # Copy to backup
    memcpy(backup_buffer, test_buffer, 256);

    # Verify copy
    errors = 0;
    i = 0;
    while (i < 256) {
        if (backup_buffer[i] != test_buffer[i]) {
            errors = errors + 1;
        }
        i = i + 1;
    }

    dos_puts("  Copied 256 bytes, errors: ");
    itoa(errors, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    return errors;
}

memory_set_test() {
    int i;
    int errors;
    char result[16];

    dos_puts("Test 2: Memory Set\r\n");

    # Set memory to pattern
    memset(test_buffer, 170, 256);  # 0xAA pattern

    # Verify
    errors = 0;
    i = 0;
    while (i < 256) {
        if (test_buffer[i] != 170) {
            errors = errors + 1;
        }
        i = i + 1;
    }

    dos_puts("  Set 256 bytes to 0xAA, errors: ");
    itoa(errors, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    return errors;
}

pattern_test() {
    int i;
    int errors;
    char result[16];

    dos_puts("Test 3: Pattern Fill/Verify\r\n");

    # Test multiple patterns
    fill_pattern(test_buffer, 512, 0);
    errors = verify_pattern(test_buffer, 512, 0);

    dos_puts("  Pattern 0: errors = ");
    itoa(errors, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    fill_pattern(test_buffer, 512, 100);
    errors = verify_pattern(test_buffer, 512, 100);

    dos_puts("  Pattern 100: errors = ");
    itoa(errors, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    return errors;
}

array_test() {
    int i;
    int sum;
    char result[16];

    dos_puts("Test 4: Integer Array\r\n");

    # Fill array with sequence
    i = 0;
    while (i < 64) {
        pattern_data[i] = i * i;
        i = i + 1;
    }

    # Calculate sum
    sum = 0;
    i = 0;
    while (i < 64) {
        sum = sum + pattern_data[i];
        i = i + 1;
    }

    dos_puts("  Sum of squares 0-63: ");
    itoa(sum, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    return sum;
}

main() {
    int total_errors;
    char result[16];

    dos_puts("=== Memory Test Utility ===\r\n\r\n");

    total_errors = 0;

    total_errors = total_errors + memory_copy_test();
    total_errors = total_errors + memory_set_test();
    total_errors = total_errors + pattern_test();
    array_test();

    dos_puts("\r\n=== Summary ===\r\n");
    dos_puts("Total errors: ");
    itoa(total_errors, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    if (total_errors == 0) {
        dos_puts("All tests PASSED!\r\n");
    } else {
        dos_puts("Some tests FAILED!\r\n");
    }

    return total_errors;
}
