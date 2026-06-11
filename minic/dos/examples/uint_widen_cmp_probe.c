#include <stdio.h>

struct State {
	unsigned int limit;
};

static struct State st;

static int le_field(unsigned long usage)
{
	return st.limit <= usage;
}

static int le_local(unsigned int limit, unsigned long usage)
{
	return limit <= usage;
}

int main(void)
{
	st.limit = 50000u;
	printf("field_hi=%d\n", le_field(60000UL));
	printf("field_lo=%d\n", le_field(40000UL));
	printf("local_hi=%d\n", le_local(50000u, 60000UL));
	printf("local_lo=%d\n", le_local(50000u, 40000UL));
	return 0;
}
