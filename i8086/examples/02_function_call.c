/* Example 2: Function calls with return values
 * Tests that function calls and return values work
 * Expected output: Returns 42 to DOS
 */

get_value() {
	return 42;
}

main() {
	int result;
	result = get_value();
	return result;
}
