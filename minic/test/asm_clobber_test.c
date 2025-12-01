/* Test file for inline assembly clobber lists */

int printf();

int global_var;

/* Test 1: Basic clobber-only syntax */
test_clobbers_only() {
    __asm__ volatile("nop" ::: "ax");
    printf("test_clobbers_only: PASS\n");
    return 0;
}

/* Test 2: Multiple clobbers */
test_multiple_clobbers() {
    __asm__ volatile("nop" ::: "ax", "bx", "memory");
    printf("test_multiple_clobbers: PASS\n");
    return 0;
}

/* Test 3: Inputs with clobbers */
test_inputs_with_clobbers() {
    int x;
    x = 42;
    __asm__ volatile("mov %0, %%ax" :: "m"(x) : "ax");
    printf("test_inputs_with_clobbers: PASS\n");
    return 0;
}

/* Test 4: Outputs with clobbers */
test_outputs_with_clobbers() {
    int result;
    __asm__ volatile("mov $5, %%ax\n\tmov %%ax, %0" : "=m"(result) :: "ax");
    printf("test_outputs_with_clobbers: result=%d\n", result);
    return 0;
}

/* Test 5: Full extended asm with outputs, inputs, and clobbers */
test_full_extended_asm() {
    int input;
    int output;
    input = 10;
    __asm__ volatile("mov %1, %%ax\n\tadd $5, %%ax\n\tmov %%ax, %0"
        : "=m"(output)
        : "m"(input)
        : "ax");
    printf("test_full_extended_asm: input=%d, output=%d\n", input, output);
    return 0;
}

/* Test 6: Memory clobber */
test_memory_clobber() {
    int x;
    x = 100;
    __asm__ volatile("" ::: "memory");  /* Memory barrier */
    printf("test_memory_clobber: x=%d PASS\n", x);
    return 0;
}

/* Test 7: Empty inputs with outputs and clobbers */
test_out_empty_in_clob() {
    int result;
    __asm__ volatile("mov $99, %%ax\n\tmov %%ax, %0" : "=m"(result) : : "ax");
    printf("test_out_empty_in_clob: result=%d\n", result);
    return 0;
}

main() {
    int result;
    int total;

    total = 0;
    printf("Testing inline assembly clobber lists...\n\n");

    result = test_clobbers_only();
    if (result) { printf("test_clobbers_only FAILED\n"); total = total + 1; }

    result = test_multiple_clobbers();
    if (result) { printf("test_multiple_clobbers FAILED\n"); total = total + 1; }

    result = test_inputs_with_clobbers();
    if (result) { printf("test_inputs_with_clobbers FAILED\n"); total = total + 1; }

    result = test_outputs_with_clobbers();
    if (result) { printf("test_outputs_with_clobbers FAILED\n"); total = total + 1; }

    result = test_full_extended_asm();
    if (result) { printf("test_full_extended_asm FAILED\n"); total = total + 1; }

    result = test_memory_clobber();
    if (result) { printf("test_memory_clobber FAILED\n"); total = total + 1; }

    result = test_out_empty_in_clob();
    if (result) { printf("test_out_empty_in_clob FAILED\n"); total = total + 1; }

    if (total == 0) {
        printf("\nAll clobber tests PASSED!\n");
    } else {
        printf("\n%d tests FAILED!\n", total);
    }

    return total;
}
