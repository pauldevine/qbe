/* Test conditional compilation */
#define DOS_BUILD

#ifdef DOS_BUILD
main() {
    return 42;
}
#else
main() {
    return 0;
}
#endif
