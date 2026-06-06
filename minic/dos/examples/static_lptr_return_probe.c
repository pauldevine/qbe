/*
 * static_lptr_return_probe.c -- copy from a static aggregate returned by ptr.
 */

#include <stdio.h>

struct P {
	void *p;
	int i;
};

static int gx;
static int gy;
static struct P cur;

static struct P *
point_at(void *p, int i)
{
	static struct P pos;
	pos.p = p;
	pos.i = i;
	return &pos;
}

int
main(void)
{
	struct P *rp;
	struct P local;

	rp = point_at(&gx, 17);
	cur = *rp;
	printf("global=%d,%d\r\n", cur.p == &gx, cur.i);

	rp = point_at(&gy, 29);
	local = *rp;
	printf("local=%d,%d\r\n", local.p == &gy, local.i);

	cur = local;
	printf("again=%d,%d\r\n", cur.p == &gy, cur.i);

	return 0;
}
