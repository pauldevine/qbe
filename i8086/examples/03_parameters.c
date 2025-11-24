/* Example 3: Function calls with parameters
 * Tests that parameter passing works correctly
 * Expected output: Returns 42 to DOS (10 + 32 = 42)
 */

add(int a, int b) {
	return a + b;
}

main() {
	int result;
	result = add(10, 32);
	return result;
}
