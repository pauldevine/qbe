# Bitfield Example for i8086
# Demonstrates struct bitfields for memory-efficient data packing

# Status register with packed flags
struct StatusReg {
    int enabled : 1;    # 1 bit for on/off flag
    int priority : 3;   # 3 bits: 0-7 priority levels
    int mode : 2;       # 2 bits: 0-3 modes
    int reserved : 2;   # 2 bits reserved
};

# Bitfield for DOS file attributes (example)
struct FileAttr {
    int readonly : 1;
    int hidden : 1;
    int system : 1;
    int reserved : 5;
};

# Test basic bitfield operations
test_status_register() {
    struct StatusReg status;

    # Set all fields
    status.enabled = 1;
    status.priority = 5;
    status.mode = 3;
    status.reserved = 0;

    # Verify reads
    if (status.enabled != 1) return 1;
    if (status.priority != 5) return 2;
    if (status.mode != 3) return 3;

    # Test modification
    status.priority = 7;
    if (status.priority != 7) return 4;

    # Verify other fields unchanged
    if (status.enabled != 1) return 5;
    if (status.mode != 3) return 6;

    return 0;
}

# Test masking behavior (overflow truncation)
test_overflow() {
    struct StatusReg status;

    # Write value larger than field can hold
    status.priority = 15;  # 4 bits, but field is only 3 bits

    # Should be masked to 7 (0b111)
    if (status.priority != 7) return 10;

    return 0;
}

# Test DOS file attributes simulation
test_file_attr() {
    struct FileAttr attr;

    # Clear all flags
    attr.readonly = 0;
    attr.hidden = 0;
    attr.system = 0;
    attr.reserved = 0;

    # Set read-only flag
    attr.readonly = 1;
    if (attr.readonly != 1) return 20;
    if (attr.hidden != 0) return 21;

    # Set hidden flag
    attr.hidden = 1;
    if (attr.readonly != 1) return 22;
    if (attr.hidden != 1) return 23;

    return 0;
}

main() {
    int r;

    r = test_status_register();
    if (r != 0) return r;

    r = test_overflow();
    if (r != 0) return r;

    r = test_file_attr();
    if (r != 0) return r;

    return 0;  # All tests passed
}
