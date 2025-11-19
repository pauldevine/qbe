#!/bin/bash

# MiniC Test Runner
# Runs all test files and reports results

DIR="$(cd "$(dirname "$0")" && pwd)"
MINIC_DIR="$(dirname "$DIR")"
MCC="$MINIC_DIR/mcc"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
total=0
passed=0
failed=0

echo "================================"
echo "MiniC Test Suite"
echo "================================"
echo ""

# Find all test_*.c files
for test_file in "$DIR"/test_*.c; do
	if [ ! -f "$test_file" ]; then
		continue
	fi

	test_name=$(basename "$test_file" .c)
	total=$((total + 1))

	echo -n "Running $test_name... "

	# Compile the test
	if ! "$MCC" "$test_file" >/dev/null 2>&1; then
		echo -e "${RED}COMPILATION FAILED${NC}"
		failed=$((failed + 1))
		continue
	fi

	# Run the test and capture output
	output=$(./a.out 2>&1)
	exit_code=$?

	# Check if test passed
	if [ $exit_code -eq 0 ] && echo "$output" | grep -q "^PASS"; then
		echo -e "${GREEN}PASS${NC}"
		passed=$((passed + 1))
	else
		echo -e "${RED}FAIL${NC}"
		echo "  Output: $output"
		failed=$((failed + 1))
	fi
done

# Clean up
rm -f ./a.out

echo ""
echo "================================"
echo "Test Results"
echo "================================"
echo "Total:  $total"
echo -e "Passed: ${GREEN}$passed${NC}"
echo -e "Failed: ${RED}$failed${NC}"
echo ""

if [ $failed -eq 0 ]; then
	echo -e "${GREEN}All tests passed!${NC}"
	exit 0
else
	echo -e "${RED}Some tests failed.${NC}"
	exit 1
fi
