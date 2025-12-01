/* Test file for GCC __attribute__ support */

int printf();
int global_counter;

/* Test 1: Interrupt handler with attribute before type */
__attribute__((interrupt))
void timer_isr(void) {
    global_counter = global_counter + 1;
    /* Should end with iret, not ret */
}

/* Test 2: Interrupt handler with attribute after type */
void __attribute__((interrupt)) keyboard_isr(void) {
    global_counter = global_counter + 2;
    /* Should end with iret, not ret */
}

/* Test 3: Regular function (no attribute) */
void regular_function(void) {
    global_counter = global_counter + 10;
    /* Should end with normal ret */
}

/* Test 4: Multiple attributes */
__attribute__((interrupt, weak))
void multi_attr_isr(void) {
    global_counter = global_counter + 100;
}

/* Test 5: K&R style with attribute */
__attribute__((interrupt))
knr_isr()
{
    global_counter = global_counter + 1000;
}

/* Test 6: Weak attribute only */
__attribute__((weak))
void weak_function(void) {
    global_counter = 0;
}

main() {
    global_counter = 0;
    printf("Attribute test compilation successful!\n");
    printf("global_counter = %d\n", global_counter);
    return 0;
}
