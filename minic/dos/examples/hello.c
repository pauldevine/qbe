# Simple DOS Hello World (Phase 0 - Integer-only)
# This is a minimal test program for the QBE i8086 backend

main() {
	int result;

	# Test basic integer operations
	result = 42;

	# Test arithmetic
	result = result + 8;  # Should be 50
	result = result - 10; # Should be 40
	result = result * 2;  # Should be 80
	result = result / 4;  # Should be 20

	# Return success code
	return 0;
}
