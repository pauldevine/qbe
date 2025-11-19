# Test complex typedef scenarios
typedef int *intptr;
typedef intptr *intptrptr;
typedef char byte;

main() {
	int x;
	intptr p;
	intptrptr pp;
	byte b;

	x = 100;
	p = &x;
	pp = &p;
	b = 65;

	printf("typedef with pointers works\n");
}
