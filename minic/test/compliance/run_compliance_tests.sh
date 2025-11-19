#!/bin/bash

# MiniC C Standard Compliance Test Runner
# Tests C89, C99, C11, C17 feature compliance

# set -e  # Disabled temporarily for debugging
shopt -s nullglob  # Enable nullglob globally

DIR="$(cd "$(dirname "$0")" && pwd)"
MINIC_DIR="$(cd "$DIR/../.." && pwd)"
MCC="$MINIC_DIR/mcc"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
total_c89=0
passed_c89=0
failed_c89=0
skipped_c89=0

total_c99=0
passed_c99=0
failed_c99=0
skipped_c99=0

total_c11=0
passed_c11=0
failed_c11=0
skipped_c11=0

# Feature tracking arrays
declare -A c89_features
declare -A c99_features
declare -A c11_features

# Log file
LOG_FILE="$DIR/compliance_results.log"
REPORT_FILE="$DIR/compliance_report.md"

echo "Starting compliance test run: $(date)" > "$LOG_FILE"

print_header() {
	echo ""
	echo -e "${BLUE}========================================${NC}"
	echo -e "${BLUE}  MiniC C Standard Compliance Tests${NC}"
	echo -e "${BLUE}========================================${NC}"
	echo ""
}

print_section() {
	echo ""
	echo -e "${YELLOW}--- $1 ---${NC}"
	echo ""
}

run_test() {
	local test_file="$1"
	local test_name=$(basename "$test_file" .c)
	local standard="$2"

	# Check if test should be skipped (has SKIP marker)
	if head -5 "$test_file" | grep -q "SKIP:"; then
		local skip_reason=$(head -5 "$test_file" | grep "SKIP:" | sed 's/.*SKIP://')
		echo -e "  ${YELLOW}SKIP${NC} $test_name - $skip_reason"
		echo "SKIP: $test_name - $skip_reason" >> "$LOG_FILE"
		return 2
	fi

	# Try to compile
	if ! "$MCC" "$test_file" > /dev/null 2>&1; then
		echo -e "  ${RED}FAIL${NC} $test_name - compilation failed"
		echo "FAIL: $test_name - compilation failed" >> "$LOG_FILE"
		return 1
	fi

	# Try to run
	if ! ./a.out > /tmp/test_output.txt 2>&1; then
		echo -e "  ${RED}FAIL${NC} $test_name - runtime error"
		echo "FAIL: $test_name - runtime error" >> "$LOG_FILE"
		rm -f ./a.out
		return 1
	fi

	# Check output
	if grep -q "^PASS" /tmp/test_output.txt; then
		echo -e "  ${GREEN}PASS${NC} $test_name"
		echo "PASS: $test_name" >> "$LOG_FILE"
		rm -f ./a.out
		return 0
	else
		echo -e "  ${RED}FAIL${NC} $test_name - test failed"
		echo "FAIL: $test_name - $(cat /tmp/test_output.txt)" >> "$LOG_FILE"
		rm -f ./a.out
		return 1
	fi
}

run_standard_tests() {
	local standard="$1"
	local test_dir="$DIR/${standard}-compliance"

	if [ ! -d "$test_dir" ]; then
		echo "Directory $test_dir does not exist, skipping..."
		return
	fi

	print_section "$standard Compliance Tests"

	# Check if any test files exist
	tests=("$test_dir"/*.c)

	if [ ${#tests[@]} -eq 0 ]; then
		echo "  No tests found in $test_dir"
		return
	fi

	for test_file in "${tests[@]}"; do

		case "$standard" in
			c89)
				((total_c89++))
				;;
			c99)
				((total_c99++))
				;;
			c11)
				((total_c11++))
				;;
		esac

		run_test "$test_file" "$standard"
		result=$?

		case "$standard" in
			c89)
				if [ $result -eq 0 ]; then
					((passed_c89++))
				elif [ $result -eq 2 ]; then
					((skipped_c89++))
				else
					((failed_c89++))
				fi
				;;
			c99)
				if [ $result -eq 0 ]; then
					((passed_c99++))
				elif [ $result -eq 2 ]; then
					((skipped_c99++))
				else
					((failed_c99++))
				fi
				;;
			c11)
				if [ $result -eq 0 ]; then
					((passed_c11++))
				elif [ $result -eq 2 ]; then
					((skipped_c11++))
				else
					((failed_c11++))
				fi
				;;
		esac
	done
}

generate_report() {
	cat > "$REPORT_FILE" << EOF
# MiniC C Standard Compliance Report

**Generated:** $(date)

## Summary

| Standard | Total Tests | Passed | Failed | Skipped | Pass Rate |
|----------|-------------|--------|--------|---------|-----------|
| **C89**  | $total_c89 | $passed_c89 | $failed_c89 | $skipped_c89 | $([ $total_c89 -gt 0 ] && echo "scale=1; $passed_c89 * 100 / $total_c89" | bc || echo "0")% |
| **C99**  | $total_c99 | $passed_c99 | $failed_c99 | $skipped_c99 | $([ $total_c99 -gt 0 ] && echo "scale=1; $passed_c99 * 100 / $total_c99" | bc || echo "0")% |
| **C11**  | $total_c11 | $passed_c11 | $failed_c11 | $skipped_c11 | $([ $total_c11 -gt 0 ] && echo "scale=1; $passed_c11 * 100 / $total_c11" | bc || echo "0")% |

## Detailed Results

### C89/ANSI C (1989)

**Target: 100% compliance with C89 standard**

EOF

	if [ $total_c89 -gt 0 ]; then
		echo "#### Passed Tests ($passed_c89)" >> "$REPORT_FILE"
		grep "^PASS:" "$LOG_FILE" | grep -v "c99-compliance\|c11-compliance" | sed 's/PASS: /- /' >> "$REPORT_FILE" || echo "None" >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"

		echo "#### Failed Tests ($failed_c89)" >> "$REPORT_FILE"
		grep "^FAIL:" "$LOG_FILE" | grep -v "c99-compliance\|c11-compliance" | sed 's/FAIL: /- /' >> "$REPORT_FILE" || echo "None" >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"

		echo "#### Skipped Tests ($skipped_c89)" >> "$REPORT_FILE"
		grep "^SKIP:" "$LOG_FILE" | grep -v "c99-compliance\|c11-compliance" | sed 's/SKIP: /- /' >> "$REPORT_FILE" || echo "None" >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"
	fi

	cat >> "$REPORT_FILE" << EOF

### C99 (1999)

**Target: Future implementation**

EOF

	if [ $total_c99 -gt 0 ]; then
		echo "- Total tests: $total_c99" >> "$REPORT_FILE"
		echo "- Passed: $passed_c99" >> "$REPORT_FILE"
		echo "- Failed: $failed_c99" >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"
	else
		echo "No C99 tests yet." >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"
	fi

	cat >> "$REPORT_FILE" << EOF

### C11 (2011)

**Target: Future implementation**

EOF

	if [ $total_c11 -gt 0 ]; then
		echo "- Total tests: $total_c11" >> "$REPORT_FILE"
		echo "- Passed: $passed_c11" >> "$REPORT_FILE"
		echo "- Failed: $failed_c11" >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"
	else
		echo "No C11 tests yet." >> "$REPORT_FILE"
		echo "" >> "$REPORT_FILE"
	fi

	cat >> "$REPORT_FILE" << EOF

## Next Steps

### Immediate (C89 Completion)
1. Implement missing C89 features
2. Add tests for untested features
3. Achieve 100% C89 compliance

### Future (C99)
1. Implement \`//\` comments
2. Add \`long long\` type
3. Support designated initializers
4. Enable mixed declarations and code

## Full Log

See \`compliance_results.log\` for detailed test output.
EOF
}

print_results() {
	echo ""
	echo -e "${BLUE}========================================${NC}"
	echo -e "${BLUE}  Test Results Summary${NC}"
	echo -e "${BLUE}========================================${NC}"
	echo ""

	if [ $total_c89 -gt 0 ]; then
		echo -e "${YELLOW}C89/ANSI C:${NC}"
		echo "  Total:   $total_c89"
		echo -e "  Passed:  ${GREEN}$passed_c89${NC}"
		echo -e "  Failed:  ${RED}$failed_c89${NC}"
		echo -e "  Skipped: ${YELLOW}$skipped_c89${NC}"
		if [ $total_c89 -gt 0 ]; then
			percent=$(echo "scale=1; $passed_c89 * 100 / $total_c89" | bc)
			echo "  Pass rate: $percent%"
		fi
		echo ""
	fi

	if [ $total_c99 -gt 0 ]; then
		echo -e "${YELLOW}C99:${NC}"
		echo "  Total:   $total_c99"
		echo -e "  Passed:  ${GREEN}$passed_c99${NC}"
		echo -e "  Failed:  ${RED}$failed_c99${NC}"
		echo -e "  Skipped: ${YELLOW}$skipped_c99${NC}"
		echo ""
	fi

	if [ $total_c11 -gt 0 ]; then
		echo -e "${YELLOW}C11:${NC}"
		echo "  Total:   $total_c11"
		echo -e "  Passed:  ${GREEN}$passed_c11${NC}"
		echo -e "  Failed:  ${RED}$failed_c11${NC}"
		echo -e "  Skipped: ${YELLOW}$skipped_c11${NC}"
		echo ""
	fi

	echo "Detailed results saved to:"
	echo "  - Log: $LOG_FILE"
	echo "  - Report: $REPORT_FILE"
	echo ""
}

# Main execution
print_header

# Run tests based on argument, or all if no argument
if [ $# -eq 0 ]; then
	# Run all standards
	run_standard_tests "c89"
	run_standard_tests "c99"
	run_standard_tests "c11"
else
	# Run specific standard
	run_standard_tests "$1"
fi

# Clean up
rm -f ./a.out /tmp/test_output.txt

# Generate report
generate_report

# Print results
print_results

# Exit with error if any tests failed
if [ $failed_c89 -gt 0 ] || [ $failed_c99 -gt 0 ] || [ $failed_c11 -gt 0 ]; then
	exit 1
fi

exit 0
