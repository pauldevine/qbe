# Performance Benchmark Utility
# Tests various operations and measures performance

int counter;

benchmark_arith() {
    int i;
    int result;

    dos_puts("Running arithmetic benchmark...\r\n");

    result = 0;
    i = 0;
    while (i < 10000) {
        result = result + i;
        result = result - (i / 2);
        result = result * 2;
        result = result / 3;
        i = i + 1;
    }

    dos_puts("  Arithmetic: 10000 iterations complete\r\n");
    return result;
}

benchmark_string() {
    char buffer[64];
    char temp[32];
    int i;

    dos_puts("Running string benchmark...\r\n");

    i = 0;
    while (i < 1000) {
        strcpy(buffer, "Performance test string ");
        itoa(i, temp, 10);
        strcat(buffer, temp);
        i = i + 1;
    }

    dos_puts("  String ops: 1000 iterations complete\r\n");
    return strlen(buffer);
}

benchmark_memory() {
    char buffer[256];
    int i;
    int j;

    dos_puts("Running memory benchmark...\r\n");

    i = 0;
    while (i < 1000) {
        j = 0;
        while (j < 256) {
            buffer[j] = i + j;
            j = j + 1;
        }
        i = i + 1;
    }

    dos_puts("  Memory ops: 256000 bytes written\r\n");
    return buffer[255];
}

main() {
    int r1;
    int r2;
    int r3;
    char result[16];

    dos_puts("=== Performance Benchmark ===\r\n");
    dos_puts("Testing CPU performance...\r\n\r\n");

    r1 = benchmark_arith();
    r2 = benchmark_string();
    r3 = benchmark_memory();

    dos_puts("\r\n=== Results ===\r\n");

    dos_puts("Arithmetic result: ");
    itoa(r1, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    dos_puts("String result: ");
    itoa(r2, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    dos_puts("Memory result: ");
    itoa(r3, result, 10);
    dos_puts(result);
    dos_puts("\r\n");

    dos_puts("\r\nBenchmark complete!\r\n");

    return 0;
}
