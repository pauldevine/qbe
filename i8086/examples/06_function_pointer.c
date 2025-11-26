# Function Pointer Example for i8086
# Demonstrates function pointers with indirect calls

typedef int (*operation_t)(int, int);

add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

mul(int a, int b) {
    return a * b;
}

# Higher-order function that applies an operation
calculate(operation_t op, int x, int y) {
    return op(x, y);
}

main() {
    operation_t op;
    int result;

    # Test 1: Addition via function pointer
    op = add;
    result = op(10, 5);  # result = 15

    # Test 2: Subtraction via function pointer
    op = sub;
    result = result + op(10, 3);  # result = 15 + 7 = 22

    # Test 3: Higher-order function
    result = result + calculate(mul, 4, 5);  # result = 22 + 20 = 42

    return result;  # Should return 42
}
