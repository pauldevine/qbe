# Function Pointer Test Suite for MiniC
# Tests all function pointer features

typedef int (*binop_t)(int, int);
typedef int (*unaryop_t)(int);

# Global result variable
int test_passed;

# Binary operations
add(int a, int b) {
    return a + b;
}

sub(int a, int b) {
    return a - b;
}

mul(int a, int b) {
    return a * b;
}

# Unary operations
negate(int x) {
    return 0 - x;
}

double_val(int x) {
    return x + x;
}

# Higher-order functions
apply_binary(binop_t op, int x, int y) {
    return op(x, y);
}

apply_unary(unaryop_t op, int x) {
    return op(x);
}

# Test 1: Basic function pointer with (*fptr) syntax
test_basic_fptr() {
    int (*fptr)(int, int);
    int result;

    fptr = add;
    result = (*fptr)(10, 5);

    if (result == 15) {
        return 1;
    }
    return 0;
}

# Test 2: Simplified fptr(args) syntax
test_simple_syntax() {
    int (*fptr)(int, int);
    int result;

    fptr = sub;
    result = fptr(20, 8);

    if (result == 12) {
        return 1;
    }
    return 0;
}

# Test 3: Typedef function pointer
test_typedef() {
    binop_t op;
    int result;

    op = mul;
    result = op(6, 7);

    if (result == 42) {
        return 1;
    }
    return 0;
}

# Test 4: Function pointer as parameter
test_higher_order() {
    int result;

    result = apply_binary(add, 100, 23);

    if (result == 123) {
        return 1;
    }
    return 0;
}

# Test 5: Change function pointer at runtime
test_runtime_change() {
    binop_t op;
    int r1;
    int r2;

    op = add;
    r1 = op(10, 5);

    op = sub;
    r2 = op(10, 5);

    if (r1 == 15) {
        if (r2 == 5) {
            return 1;
        }
    }
    return 0;
}

# Test 6: Unary function pointer
test_unary_fptr() {
    unaryop_t op;
    int result;

    op = negate;
    result = apply_unary(op, 42);

    if (result == -42) {
        return 1;
    }
    return 0;
}

# Test 7: Pass function directly (not through variable)
test_direct_pass() {
    int result;

    result = apply_binary(mul, 8, 8);

    if (result == 64) {
        return 1;
    }
    return 0;
}

# Test 8: Chain of function pointer calls
test_chain() {
    binop_t op1;
    binop_t op2;
    int r1;
    int r2;
    int final;

    op1 = add;
    op2 = mul;

    r1 = op1(10, 20);
    r2 = op2(r1, 2);
    final = apply_binary(sub, r2, 10);

    if (final == 50) {
        return 1;
    }
    return 0;
}

main() {
    int passed;
    int total;

    passed = 0;
    total = 8;

    # Run all tests
    if (test_basic_fptr()) {
        passed = passed + 1;
    }

    if (test_simple_syntax()) {
        passed = passed + 1;
    }

    if (test_typedef()) {
        passed = passed + 1;
    }

    if (test_higher_order()) {
        passed = passed + 1;
    }

    if (test_runtime_change()) {
        passed = passed + 1;
    }

    if (test_unary_fptr()) {
        passed = passed + 1;
    }

    if (test_direct_pass()) {
        passed = passed + 1;
    }

    if (test_chain()) {
        passed = passed + 1;
    }

    # Return 0 if all tests passed (success), non-zero otherwise
    if (passed == total) {
        return 0;
    }
    return 1;
}
