/*
 * struct_copy_probe.c -- aggregate assignment forms that Stevie depends on.
 */

#include <stdio.h>

struct P {
	void *p;
	int i;
};

static int gx;
static int gy;

static struct P
make_p(void *p, int i)
{
	struct P r;
	r.p = p;
	r.i = i;
	return r;
}

static void
copy_ptr(struct P *dst, struct P *src)
{
	*dst = *src;
}

static void
swap_ptr(struct P *a, struct P *b)
{
	struct P tmp;
	tmp = *a;
	*a = *b;
	*b = tmp;
}

int
main(void)
{
	struct P a;
	struct P b;
	struct P c;
	struct P d;

	a.p = &gx;
	a.i = 1111;
	b.p = &gy;
	b.i = 2222;

	c = a;
	printf("local=%d,%d\r\n", c.p == &gx, c.i);

	d.p = 0;
	d.i = 0;
	copy_ptr(&d, &a);
	printf("ptrcopy=%d,%d\r\n", d.p == &gx, d.i);

	c = make_p(&gy, 3333);
	printf("retcopy=%d,%d\r\n", c.p == &gy, c.i);

	swap_ptr(&a, &b);
	printf("swap_a=%d,%d\r\n", a.p == &gy, a.i);
	printf("swap_b=%d,%d\r\n", b.p == &gx, b.i);

	return 0;
}
