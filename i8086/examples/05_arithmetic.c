/* Example 5: Arithmetic operations
 * Tests basic arithmetic: addition, subtraction, multiplication
 * Expected output: Returns 15 to DOS ((10 + 5) * 3 - 30 = 15)
 */

main() {
	int a, b, c;

	a = 10;
	b = 5;
	c = a + b;    /* c = 15 */
	c = c * 3;    /* c = 45 */
	c = c - 30;   /* c = 15 */

	return c;
}
