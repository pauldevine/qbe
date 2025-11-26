/* Bitfield tests for MiniC compiler */

/* Simple bitfield struct */
struct Flags {
    int a : 3;
    int b : 4;
    int c : 5;
};

/* Bitfield packing test - these should all fit in one int */
struct Packed {
    int x : 8;
    int y : 8;
    int z : 8;
};

/* Test basic bitfield read/write */
test_basic() {
    struct Flags f;

    f.a = 5;
    f.b = 10;
    f.c = 25;

    /* Test read */
    if (f.a != 5) return 1;
    if (f.b != 10) return 2;
    if (f.c != 25) return 3;

    return 0;
}

/* Test bitfield overwrite */
test_overwrite() {
    struct Flags f;

    f.a = 7;
    f.b = 15;
    f.c = 31;

    /* Overwrite with new values */
    f.a = 2;
    f.b = 8;
    f.c = 16;

    if (f.a != 2) return 10;
    if (f.b != 8) return 11;
    if (f.c != 16) return 12;

    return 0;
}

/* Test bitfield masking (value overflow) */
test_masking() {
    struct Flags f;

    /* Write value larger than field can hold */
    f.a = 255;
    f.b = 255;
    f.c = 255;

    if (f.a != 7) return 20;
    if (f.b != 15) return 21;
    if (f.c != 31) return 22;

    return 0;
}

/* Test that bitfields don't interfere with each other */
test_independence() {
    struct Flags f;

    f.a = 3;
    f.b = 9;
    f.c = 17;

    /* Modify one field, others should stay same */
    f.b = 5;

    if (f.a != 3) return 30;
    if (f.b != 5) return 31;
    if (f.c != 17) return 32;

    /* Modify another field */
    f.a = 1;

    if (f.a != 1) return 33;
    if (f.b != 5) return 34;
    if (f.c != 17) return 35;

    return 0;
}

/* Test packed bitfields */
test_packed() {
    struct Packed p;

    p.x = 100;
    p.y = 150;
    p.z = 200;

    if (p.x != 100) return 40;
    if (p.y != 150) return 41;
    if (p.z != 200) return 42;

    return 0;
}

main() {
    int r;

    r = test_basic();
    if (r != 0) return r;

    r = test_overwrite();
    if (r != 0) return r;

    r = test_masking();
    if (r != 0) return r;

    r = test_independence();
    if (r != 0) return r;

    r = test_packed();
    if (r != 0) return r;

    return 0;
}
