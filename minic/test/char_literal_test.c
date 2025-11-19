# Test character literals

main() {
	char c;
	int i;

	# Test basic character literal
	c = 'A';
	printf("'A' = %d (should be 65)\n", c);

	# Test escape sequences
	c = '\n';
	printf("'\\n' = %d (should be 10)\n", c);

	c = '\t';
	printf("'\\t' = %d (should be 9)\n", c);

	c = '0';
	printf("'0' = %d (should be 48)\n", c);

	# Test hex literal
	i = 0x10;
	printf("0x10 = %d (should be 16)\n", i);

	i = 0xFF;
	printf("0xFF = %d (should be 255)\n", i);

	# Test octal literal
	i = 010;
	printf("010 (octal) = %d (should be 8)\n", i);

	i = 0777;
	printf("0777 (octal) = %d (should be 511)\n", i);

	# Test in expression
	if (c >= '0' && c <= '9')
		printf("c is a digit\n");
	else
		printf("c is not a digit\n");
}
