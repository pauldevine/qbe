/* Example 4: Global variables
 * Tests global variable storage and retrieval
 * Expected output: Returns 42 to DOS
 */

int result;

set_result(int value) {
	result = value;
}

main() {
	set_result(42);
	return result;
}
