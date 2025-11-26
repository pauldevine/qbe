# Comprehensive function pointer test suite

# Define a function pointer typedef
typedef int (*binary_op_t)(int, int);
typedef int (*unary_op_t)(int);

# Basic arithmetic operations
add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

mul(int a, int b) {
    return a * b;
}

negate(int x) {
    return 0 - x;
}

double_it(int x) {
    return x + x;
}

# Higher-order function: apply binary operation
apply_binary(binary_op_t op, int x, int y) {
    return op(x, y);
}

# Higher-order function: apply unary operation
apply_unary(unary_op_t op, int x) {
    return op(x);
}

# Callback pattern: process with callback
process_array(int a, int b, int c, binary_op_t combine) {
    int result;
    result = combine(a, b);
    result = combine(result, c);
    return result;
}

main() {
    # Test 1: Local function pointer variable with (*fptr) syntax
    int (*fptr)(int, int);
    int result1;

    fptr = add;
    result1 = (*fptr)(10, 5);  # Should be 15

    # Test 2: Direct call syntax (fptr(args))
    fptr = sub;
    result1 = result1 + fptr(10, 5);  # 15 + 5 = 20

    # Test 3: Typedef with apply function
    binary_op_t binop;
    binop = mul;
    result1 = result1 + apply_binary(binop, 3, 4);  # 20 + 12 = 32

    # Test 4: Unary function pointer
    unary_op_t uop;
    uop = negate;
    result1 = result1 + apply_unary(uop, -8);  # 32 + 8 = 40

    # Test 5: Change function pointer mid-execution
    uop = double_it;
    result1 = result1 + uop(5);  # 40 + 10 = 50

    # Test 6: Callback pattern
    result1 = result1 + process_array(1, 2, 3, add);  # 50 + 6 = 56

    # Test 7: Pass function directly to higher-order function
    result1 = result1 + apply_binary(sub, 20, 16);  # 56 + 4 = 60

    return result1;  # Should return 60
}
